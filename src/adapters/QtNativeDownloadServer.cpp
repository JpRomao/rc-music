#include "adapters/QtNativeDownloadServer.hpp"

#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeDownloadSession.hpp"
#include "adapters/WindowsNativeDownloadLease.hpp"
#include "adapters/WindowsNativeDownloadPipeReservation.hpp"

#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
constexpr quint64 timeout_ms = 5'000;
constexpr qint64 pending_read_buffer_bytes = 1;
constexpr qint64 active_read_buffer_bytes =
    native_download_ipc::preamble_bytes + 4 + native_message_max_payload_bytes + 1;
bool expired(const qint64 start, const qint64 now) {
    return now < start || static_cast<quint64>(now) - static_cast<quint64>(start) >= timeout_ms;
}
class BoundedServer final : public QLocalServer {
  protected:
    void incomingConnection(const quintptr descriptor) override {
        auto* socket = new QLocalSocket(this);
        socket->setReadBufferSize(pending_read_buffer_bytes);
        if (!socket->setSocketDescriptor(static_cast<qintptr>(descriptor))) {
            delete socket;
            return;
        }
        addPendingConnection(socket);
    }
};
} // namespace

struct QtNativeDownloadServer::Impl {
    QtNativeDownloadServer& owner;
    NativeHostConfiguration configuration;
    AsyncDownloadSubmission& submission;
    QtNativeDownloadServerOptions options;
    // Declared before the listener so it is destroyed after all pipe resources.
    // Successful owners retain it even after ingress is physically stopped.
    WindowsNativeDownloadLease lease;
    WindowsNativeDownloadPipeReservation pipe_reservation;
    BoundedServer server;
    QTimer timer;
    QElapsedTimer elapsed;
    qint64 last_now{0};
    bool started{false};
    bool closing{false};
    bool polling{false};
    bool admitting{false};
    bool prune_scheduled{false};
    qsizetype callback_depth{0};

    class Gate final : public AsyncDownloadSubmission {
      public:
        explicit Gate(Impl& owner) : owner_(owner) {}
        DownloadAdmissionResult begin(const DownloadRequest& request) override {
            if (owner_.closing || owner_.admitting || QThread::currentThread() != owner_.owner.thread())
                return {};
            owner_.admitting = true;
            const auto release = qScopeGuard([&] { owner_.admitting = false; });
            const auto available = owner_.options.available;
            if ((available && !available()) || owner_.closing)
                return {};
            return owner_.submission.begin(request);
        }
        std::optional<DownloadSubmissionResult> poll(const std::string_view id) override {
            if (owner_.closing || owner_.admitting || QThread::currentThread() != owner_.owner.thread())
                throw std::runtime_error{"native admission observer unavailable"};
            owner_.admitting = true;
            const auto release = qScopeGuard([&] { owner_.admitting = false; });
            return owner_.submission.poll(id);
        }

      private:
        Impl& owner_;
    } gate;

    class Connection final : public QObject {
      public:
        Connection(Impl& owner, QLocalSocket* socket, const qint64 start)
            : owner_(owner), socket_(socket), started_ms_(start) {
            socket_->setParent(this);
            socket_->setReadBufferSize(active_read_buffer_bytes);
            connect(socket_, &QLocalSocket::readyRead, this, [this] { consume(); });
            connect(socket_, &QLocalSocket::bytesWritten, this, [this](qint64) { disconnectAfterWrite(); });
            connect(socket_, &QLocalSocket::disconnected, this, [this] { close(); });
            connect(socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { close(); });
        }
        void consume() {
            if (closed_)
                return;
            ++owner_.callback_depth;
            const auto release = qScopeGuard([&] {
                --owner_.callback_depth;
                owner_.schedulePrune();
            });
            const auto now = owner_.now();
            if (!now || closed_)
                return;
            if (responding_) {
                if (socket_->bytesAvailable() > 0)
                    close();
                return;
            }
            if (!session_) {
                if (expired(started_ms_, *now)) {
                    close();
                    return;
                }
                const auto remaining = native_download_ipc::preamble_bytes - preamble_.size();
                preamble_ += socket_->read(remaining);
                if (preamble_.size() != native_download_ipc::preamble_bytes)
                    return;
                if (preamble_ != native_download_ipc::preamble(owner_.configuration.allowed_origin)) {
                    close();
                    return;
                }
                session_ = std::make_unique<NativeDownloadSession>(
                    owner_.configuration.allowed_origin, owner_.configuration.allowed_origin, owner_.gate, started_ms_);
                preamble_.clear();
            }
            if (socket_->bytesAvailable() == 0)
                return;
            // One bounded delivery catches a complete frame plus an excess byte
            // before dispatch; no unbounded readAll or length-driven allocation.
            const auto bytes = socket_->read(native_message_max_payload_bytes + 4 + 1);
            const auto result = session_->receive(bytes, *now);
            if (!closed_)
                handle(result, *now);
        }
        void poll(const qint64 now) {
            if (closed_)
                return;
            ++owner_.callback_depth;
            const auto release = qScopeGuard([&] { --owner_.callback_depth; });
            if (responding_) {
                if (expired(response_started_ms_, now))
                    close();
                else
                    disconnectAfterWrite();
            } else if (session_)
                handle(session_->poll(now), now);
            else if (expired(started_ms_, now))
                close();
        }
        void close() {
            if (closed_)
                return;
            closed_ = true;
            socket_->abort();
            owner_.schedulePrune();
        }
        bool closed() const { return closed_; }

      private:
        void handle(const NativeSessionResult& result, const qint64 now) {
            if (closed_)
                return;
            if (result.status == NativeSessionStatus::Awaiting)
                return;
            if (result.status != NativeSessionStatus::Replied || result.response_frame.size() < 5 ||
                result.response_frame.size() > native_download_ipc::response_max_payload_bytes + 4) {
                close();
                return;
            }
            responding_ = true;
            response_started_ms_ = now;
            if (socket_->write(result.response_frame) != result.response_frame.size()) {
                close();
                return;
            }
            disconnectAfterWrite();
        }
        void disconnectAfterWrite() {
            if (closed_ || !responding_ || socket_->bytesToWrite() != 0)
                return;
            socket_->disconnectFromServer();
            if (socket_->state() == QLocalSocket::UnconnectedState)
                close();
        }
        Impl& owner_;
        QLocalSocket* socket_;
        qint64 started_ms_;
        qint64 response_started_ms_{0};
        QByteArray preamble_;
        std::unique_ptr<NativeDownloadSession> session_;
        bool responding_{false};
        bool closed_{false};
    };
    std::vector<std::unique_ptr<Connection>> connections;

    Impl(QtNativeDownloadServer& object, NativeHostConfiguration config, AsyncDownloadSubmission& port,
         QtNativeDownloadServerOptions settings)
        : owner(object), configuration(std::move(config)), submission(port), options(std::move(settings)), gate(*this) {
        elapsed.start();
        timer.setInterval(10);
        connections.reserve(8);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        server.setListenBacklogSize(8);
        server.setMaxPendingConnections(8);
        QObject::connect(&server, &QLocalServer::newConnection, &owner, [this] { accept(); });
        QObject::connect(&timer, &QTimer::timeout, &owner, &QtNativeDownloadServer::poll);
    }
    std::optional<qint64> now() {
        try {
            const auto clock = options.now_ms;
            const qint64 current = clock ? clock() : elapsed.elapsed();
            if (current < 0 || current < last_now)
                throw std::runtime_error{"native ingress clock invalid"};
            last_now = current;
            return current;
        } catch (...) {
            shutdown();
            return std::nullopt;
        }
    }
    void accept() {
        ++callback_depth;
        const auto release = qScopeGuard([&] {
            --callback_depth;
            schedulePrune();
        });
        while (server.hasPendingConnections()) {
            auto* socket = server.nextPendingConnection();
            if (!socket)
                continue;
            const auto current = now();
            // Closed objects are retained until active callback stacks unwind;
            // count them conservatively to keep aggregate allocations bounded.
            if (closing || !current || static_cast<qsizetype>(connections.size()) >= options.max_active_connections) {
                socket->abort();
                delete socket;
                continue;
            }
            auto connection = std::make_unique<Connection>(*this, socket, *current);
            auto* const pointer = connection.get();
            connections.push_back(std::move(connection));
            pointer->consume();
        }
    }
    void schedulePrune() {
        if (prune_scheduled)
            return;
        prune_scheduled = true;
        QMetaObject::invokeMethod(
            &owner,
            [this] {
                prune_scheduled = false;
                prune();
            },
            Qt::QueuedConnection);
    }
    void prune() {
        if (callback_depth != 0 || polling)
            return;
        std::erase_if(connections, [](const auto& item) { return item->closed(); });
    }
    void shutdown() {
        closing = true;
        timer.stop();
        server.close();
        for (const auto& connection : connections)
            connection->close();
        // Never destroy a session while its admission/poll callback is on stack.
        prune();
    }
};
QtNativeDownloadServer::QtNativeDownloadServer(NativeHostConfiguration configuration,
                                               AsyncDownloadSubmission& submission,
                                               QtNativeDownloadServerOptions options, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(configuration), submission, std::move(options))) {}
QtNativeDownloadServer::~QtNativeDownloadServer() { impl_->shutdown(); }
bool QtNativeDownloadServer::start() {
    if (QThread::currentThread() != thread() || impl_->started || impl_->closing)
        return false;
    impl_->started = true;
    if (!isValidNativeHostConfiguration(impl_->configuration) || impl_->options.max_active_connections < 1 ||
        impl_->options.max_active_connections > 8 || !impl_->now() || impl_->closing)
        return false;
    if (!impl_->lease.acquire(impl_->configuration.endpoint_name))
        return false;
    if (!impl_->pipe_reservation.acquire(impl_->configuration.endpoint_name)) {
        impl_->lease.reset();
        return false;
    }
    // Qt can deliver a pending connection synchronously during listen. A trusted
    // callback may close ingress; never report success or restart its timer then.
    if (!impl_->server.listen(impl_->configuration.endpoint_name)) {
        impl_->shutdown();
        impl_->pipe_reservation.reset();
        impl_->lease.reset();
        return false;
    }
    if (impl_->closing || !impl_->server.isListening()) {
        // A synchronous listen callback may already have admitted owner work.
        // Close ingress, but retain its lease through the owner's teardown.
        impl_->shutdown();
        return false;
    }
    if (impl_->options.automatic_polling)
        impl_->timer.start();
    return true;
}
void QtNativeDownloadServer::poll() {
    if (QThread::currentThread() != thread() || impl_->closing || impl_->polling || impl_->callback_depth != 0)
        return;
    // The trusted clock is still an outward callback. Publish the guard before
    // observing it, and keep drain readiness false until every stack unwinds.
    impl_->polling = true;
    const auto release = qScopeGuard([&] {
        impl_->polling = false;
        impl_->prune();
    });
    const auto current = impl_->now();
    if (!current || impl_->closing)
        return;
    const auto count = impl_->connections.size();
    for (std::size_t i = 0; i < count; ++i)
        impl_->connections[i]->poll(*current);
}
void QtNativeDownloadServer::beginShutdown() {
    if (QThread::currentThread() == thread())
        impl_->shutdown();
}
bool QtNativeDownloadServer::stopped() const {
    return impl_->closing && !impl_->polling && impl_->callback_depth == 0 && !impl_->server.isListening() &&
           activeConnectionCount() == 0;
}
qsizetype QtNativeDownloadServer::activeConnectionCount() const {
    return std::count_if(impl_->connections.begin(), impl_->connections.end(),
                         [](const auto& item) { return !item->closed(); });
}
} // namespace music_player::m0
