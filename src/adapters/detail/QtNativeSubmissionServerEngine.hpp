#pragma once

// Private implementation for the two fixed, independently configured native
// ingress wrappers. Policy is selected by C++ types, never by peer JSON/argv.
#include "adapters/NativeDownloadSession.hpp"
#include "adapters/NativeHostLaunch.hpp"
#include "adapters/WindowsNativeDownloadLease.hpp"
#include "adapters/WindowsNativeDownloadPeerCheck.hpp"
#include "adapters/WindowsNativeDownloadPipeReservation.hpp"

#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace music_player::m0::native_submission_server_detail {
constexpr quint64 timeout_ms = 5'000;
constexpr qint64 pending_read_buffer_bytes = 1;
inline bool expired(const qint64 start, const qint64 now) {
    return now < start || static_cast<quint64>(now) - static_cast<quint64>(start) >= timeout_ms;
}
class BoundedServer final : public QLocalServer {
  protected:
    void incomingConnection(const quintptr descriptor) override {
        auto* socket = new QLocalSocket(this);
        socket->setReadBufferSize(pending_read_buffer_bytes);
        // In the pinned Windows Qt implementation, WriteOnly adopts the HANDLE
        // without starting the reader. No writes are exposed before eligibility.
        if (!socket->setSocketDescriptor(static_cast<qintptr>(descriptor), QLocalSocket::ConnectedState,
                                         QIODevice::WriteOnly)) {
            delete socket;
            return;
        }
        addPendingConnection(socket);
    }
};

template <class Policy> struct QtNativeSubmissionServerEngine {
    using Request = typename Policy::Request;
    using Submission = typename Policy::Submission;
    using Session = typename Policy::Session;
    using Options = typename Policy::Options;
    static constexpr qint64 active_read_buffer_bytes = Policy::preamble_bytes + 4 + Policy::max_payload_bytes + 1;

    QObject& owner;
    NativeHostConfiguration configuration;
    Submission& submission;
    Options options;
    // Declared before the listener so it is destroyed after all pipe resources.
    // Successful owners retain it even after ingress is physically stopped.
    WindowsNativeDownloadLease lease;
    WindowsNativeDownloadPipeReservation pipe_reservation;
    BoundedServer server;
    QTimer timer;
    QTimer peer_timer;
    QElapsedTimer elapsed;
    qint64 last_now{0};
    bool started{false};
    bool closing{false};
    bool polling{false};
    bool checking_peers{false};
    bool pruning{false};
    bool shutting_down{false};
    bool admitting{false};
    bool prune_scheduled{false};
    qsizetype callback_depth{0};

    class Gate final : public Submission {
      public:
        explicit Gate(QtNativeSubmissionServerEngine& owner) : owner_(owner) {}
        DownloadAdmissionResult begin(const Request& request) override {
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
        QtNativeSubmissionServerEngine& owner_;
    } gate;

    class Connection final : public QObject {
      public:
        Connection(QtNativeSubmissionServerEngine& owner, QLocalSocket* socket, const qint64 start)
            : owner_(owner), socket_(socket), started_ms_(start) {
            socket_->setParent(this);
            socket_->setReadBufferSize(active_read_buffer_bytes);
            connect(socket_, &QLocalSocket::readyRead, this, [this] { consume(); });
            connect(socket_, &QLocalSocket::bytesWritten, this, [this](qint64) { disconnectAfterWrite(); });
            connect(socket_, &QLocalSocket::disconnected, this, [this] { close(); });
            connect(socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { close(); });
        }
        void beginPeerCheck() {
            try {
                const auto factory = owner_.options.peer_check_factory;
                peer_ = factory ? factory() : std::make_unique<WindowsNativeDownloadPeerCheck>();
                if (closed_ || owner_.closing || !peer_ ||
                    !peer_->begin(socket_->socketDescriptor(), NativeDownloadPeerSide::Client,
                                  owner_.configuration.expected_host_images)) {
                    if (peer_)
                        peer_->shutdown();
                    close();
                }
            } catch (...) {
                close();
            }
            owner_.peer_timer.start();
        }
        void checkPeer(const qint64 now) {
            if (closed_ || peer_ready_)
                return;
            if (owner_.closing || expired(started_ms_, now)) {
                close();
                return;
            }
            try {
                const auto decision = peer_->poll();
                if (closed_ || owner_.closing)
                    return;
                if (decision == NativeDownloadPeerState::Eligible) {
                    peer_ready_ = true;
                    // Re-adopt the same owned HANDLE only after the check. Qt
                    // starts its first asynchronous read in this transition.
                    if (!socket_->setSocketDescriptor(socket_->socketDescriptor(), QLocalSocket::ConnectedState,
                                                      QIODevice::ReadWrite))
                        close();
                    else
                        consume();
                } else if (decision != NativeDownloadPeerState::Pending) {
                    close();
                }
            } catch (...) {
                close();
            }
        }
        void consume() {
            if (closed_ || !peer_ready_)
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
                const auto remaining = Policy::preamble_bytes - preamble_.size();
                preamble_ += socket_->read(remaining);
                if (preamble_.size() != Policy::preamble_bytes)
                    return;
                if (preamble_ != Policy::preamble(owner_.configuration.allowed_origin)) {
                    close();
                    return;
                }
                session_ = std::make_unique<Session>(owner_.configuration.allowed_origin,
                                                     owner_.configuration.allowed_origin, owner_.gate, started_ms_);
                preamble_.clear();
            }
            if (socket_->bytesAvailable() == 0)
                return;
            // One bounded delivery catches a complete frame plus an excess byte
            // before dispatch; no unbounded readAll or length-driven allocation.
            const auto bytes = socket_->read(Policy::max_payload_bytes + 4 + 1);
            const auto result = session_->receive(bytes, *now);
            if (!closed_)
                handle(result, *now);
        }
        void poll(const qint64 now) {
            if (closed_)
                return;
            ++owner_.callback_depth;
            const auto release = qScopeGuard([&] { --owner_.callback_depth; });
            if (!peer_ready_) {
                checkPeer(now);
                return;
            }
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
            ++owner_.callback_depth;
            const auto release = qScopeGuard([&] { --owner_.callback_depth; });
            closed_ = true;
            if (peer_)
                peer_->shutdown();
            socket_->abort();
            owner_.peer_timer.start();
            owner_.schedulePrune();
        }
        bool closed() const { return closed_; }
        void observeDrain() {
            if (!closed_ || drain_ready_)
                return;
            try {
                drain_ready_ = !peer_ || peer_->stopped();
            } catch (...) {
                // Unknown retirement keeps the slot and owner reservation.
            }
        }
        bool drained() const { return closed_ && drain_ready_; }
        bool needsPeerWork() const { return closed_ ? !drained() : !peer_ready_; }

      private:
        void handle(const NativeSessionResult& result, const qint64 now) {
            if (closed_)
                return;
            if (result.status == NativeSessionStatus::Awaiting)
                return;
            if (result.status != NativeSessionStatus::Replied || result.response_frame.size() < 5 ||
                result.response_frame.size() > Policy::response_max_payload_bytes + 4) {
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
        QtNativeSubmissionServerEngine& owner_;
        QLocalSocket* socket_;
        qint64 started_ms_;
        qint64 response_started_ms_{0};
        QByteArray preamble_;
        std::unique_ptr<Session> session_;
        std::unique_ptr<NativeDownloadPeerCheck> peer_;
        bool peer_ready_{false};
        bool drain_ready_{false};
        bool responding_{false};
        bool closed_{false};
    };
    std::vector<std::unique_ptr<Connection>> connections;

    QtNativeSubmissionServerEngine(QObject& object, NativeHostConfiguration config, Submission& port, Options settings)
        : owner(object), configuration(std::move(config)), submission(port), options(std::move(settings)), gate(*this) {
        elapsed.start();
        timer.setInterval(10);
        peer_timer.setInterval(1);
        connections.reserve(8);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        server.setListenBacklogSize(8);
        server.setMaxPendingConnections(8);
        QObject::connect(&server, &QLocalServer::newConnection, &owner, [this] { accept(); });
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { poll(); });
        QObject::connect(&peer_timer, &QTimer::timeout, &owner, [this] { pollPeers(); });
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
            pointer->beginPeerCheck();
        }
    }
    void pollPeers() {
        if (callback_depth != 0 || polling || checking_peers)
            return;
        checking_peers = true;
        ++callback_depth;
        const auto release = qScopeGuard([&] {
            --callback_depth;
            checking_peers = false;
            prune();
        });
        if (closing)
            return; // prune below still observes cancelled worker retirement.
        const auto pending = std::any_of(connections.begin(), connections.end(),
                                         [](const auto& item) { return !item->closed() && item->needsPeerWork(); });
        if (!pending)
            return;
        const auto current = now();
        if (!current || closing)
            return;
        const auto count = connections.size();
        for (std::size_t i = 0; i < count; ++i)
            connections[i]->checkPeer(*current);
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
        if (callback_depth != 0 || polling || pruning)
            return;
        pruning = true;
        ++callback_depth;
        const auto release = qScopeGuard([&] {
            --callback_depth;
            pruning = false;
        });
        // Observe outward ports while the vector is intact. No port is called
        // during compaction, when a callback could see moved/null entries.
        const auto count = connections.size();
        for (std::size_t i = 0; i < count; ++i)
            connections[i]->observeDrain();
        std::array<std::unique_ptr<Connection>, 8> retired;
        std::size_t next = 0;
        for (auto& item : connections) {
            if (item->drained())
                retired[next++] = std::move(item);
        }
        std::erase_if(connections, [](const auto& item) { return !item; });
        if (std::none_of(connections.begin(), connections.end(),
                         [](const auto& item) { return item->needsPeerWork(); }))
            peer_timer.stop();
        // Retired objects die before the guard, after vector compaction.
    }
    void shutdown() {
        if (shutting_down)
            return;
        shutting_down = true;
        ++callback_depth;
        const auto release = qScopeGuard([&] {
            --callback_depth;
            shutting_down = false;
            prune();
        });
        closing = true;
        timer.stop();
        server.close();
        for (const auto& connection : connections)
            connection->close();
        // The release guard prunes only after every shutdown callback unwinds.
    }

    bool start() {
        if (QThread::currentThread() != owner.thread() || started || closing)
            return false;
        started = true;
        if (!isValidNativeHostConfiguration(configuration) ||
            !validNativePeerImages(configuration.expected_host_images) || options.max_active_connections < 1 ||
            options.max_active_connections > 8 || !now() || closing)
            return false;
        if (!lease.acquire(configuration.endpoint_name))
            return false;
        if (!pipe_reservation.acquire(configuration.endpoint_name)) {
            lease.reset();
            return false;
        }
        // Qt can deliver a pending connection synchronously during listen. A trusted
        // callback may close ingress; never report success or restart its timer then.
        if (!server.listen(configuration.endpoint_name)) {
            shutdown();
            pipe_reservation.reset();
            lease.reset();
            return false;
        }
        if (closing || !server.isListening()) {
            // A synchronous listen callback may already have admitted owner work.
            // Close ingress, but retain its lease through the owner's teardown.
            shutdown();
            return false;
        }
        if (options.automatic_polling)
            timer.start();
        return true;
    }
    void poll() {
        if (QThread::currentThread() != owner.thread() || closing || polling || callback_depth != 0)
            return;
        // The trusted clock is still an outward callback. Publish the guard before
        // observing it, and keep drain readiness false until every stack unwinds.
        polling = true;
        const auto release = qScopeGuard([&] {
            polling = false;
            prune();
        });
        const auto current = now();
        if (!current || closing)
            return;
        const auto count = connections.size();
        for (std::size_t i = 0; i < count; ++i)
            connections[i]->poll(*current);
    }
    void beginShutdown() {
        if (QThread::currentThread() == owner.thread())
            shutdown();
    }
    bool stopped() {
        if (!closing || polling || checking_peers || callback_depth != 0 || server.isListening())
            return false;
        ++callback_depth;
        const auto release = qScopeGuard([&] { --callback_depth; });
        const auto count = connections.size();
        for (std::size_t i = 0; i < count; ++i)
            connections[i]->observeDrain();
        return std::all_of(connections.begin(), connections.end(), [](const auto& item) { return item->drained(); });
    }
    qsizetype activeConnectionCount() const {
        return std::count_if(connections.begin(), connections.end(), [](const auto& item) { return !item->closed(); });
    }
};
} // namespace music_player::m0::native_submission_server_detail
