#include "adapters/QtNativeDownloadClient.hpp"

#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/WindowsNativeDownloadPeerCheck.hpp"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <qt_windows.h>

#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr quint64 exchange_timeout_ms = 5'000;
constexpr qsizetype maximum_response_bytes = native_download_ipc::response_max_payload_bytes + 4 + 1;
} // namespace
struct QtNativeDownloadClient::Impl {
    enum class State { Idle, CheckingPeer, Active, Completed, Uncertain, Closed };
    QtNativeDownloadClient& owner;
    NativeHostConfiguration configuration;
    QtNativeDownloadClientOptions options;
    QLocalSocket socket;
    QElapsedTimer elapsed;
    QTimer peer_timer;
    std::unique_ptr<NativeDownloadPeerCheck> peer;
    bool checking_peer{false};
    bool beginning{false};
    bool polling{false};
    bool shutting_down{false};
    qsizetype callback_depth{0};
    State state{State::Idle};
    bool used{false};
    bool sent{false};
    qint64 started_ms{0};
    qint64 last_ms{0};
    QByteArray request_frame;
    QByteArray response;
    std::optional<quint32> expected_bytes;
    std::string request_id;
    std::size_t item_count{0};
    DownloadSubmissionResult result{};

    Impl(QtNativeDownloadClient& object, NativeHostConfiguration config, QtNativeDownloadClientOptions settings)
        : owner(object), configuration(std::move(config)), options(std::move(settings)) {
        elapsed.start();
        socket.setReadBufferSize(maximum_response_bytes);
        peer_timer.setInterval(1);
        QObject::connect(&peer_timer, &QTimer::timeout, &owner, [this] { checkPeer(); });
        QObject::connect(&socket, &QLocalSocket::readyRead, &owner, [this] { readAvailable(); });
        QObject::connect(&socket, &QLocalSocket::disconnected, &owner, [this] { finish(); });
        QObject::connect(&socket, &QLocalSocket::errorOccurred, &owner,
                         [this](const QLocalSocket::LocalSocketError error) {
                             if (error != QLocalSocket::PeerClosedError)
                                 fail();
                         });
    }
    std::optional<qint64> now() {
        ++callback_depth;
        const auto release = qScopeGuard([&] { --callback_depth; });
        try {
            const auto clock = options.now_ms;
            const auto current = clock ? clock() : elapsed.elapsed();
            if (current < 0 || current < last_ms)
                throw std::runtime_error{"native exchange clock invalid"};
            last_ms = current;
            return current;
        } catch (...) {
            fail();
            return std::nullopt;
        }
    }
    bool timing() {
        const auto current = now();
        if (!current || (state != State::Active && state != State::CheckingPeer))
            return false;
        if (*current < started_ms ||
            static_cast<quint64>(*current) - static_cast<quint64>(started_ms) >= exchange_timeout_ms) {
            fail();
            return false;
        }
        return true;
    }
    void fail() {
        if (state != State::Active && state != State::CheckingPeer)
            return;
        ++callback_depth;
        const auto release = qScopeGuard([&] { --callback_depth; });
        state = sent ? State::Uncertain : State::Completed;
        result = {};
        peer_timer.stop();
        if (peer)
            peer->shutdown();
        socket.abort();
        request_frame.clear();
        response.clear();
    }
    void checkPeer() {
        if (state != State::CheckingPeer || checking_peer || beginning)
            return;
        checking_peer = true;
        const auto release = qScopeGuard([&] { checking_peer = false; });
        if (!timing() || state != State::CheckingPeer)
            return;
        try {
            ++callback_depth;
            const auto observation = qScopeGuard([&] { --callback_depth; });
            const auto decision = peer->poll();
            if (state != State::CheckingPeer)
                return;
            if (decision == NativeDownloadPeerState::Eligible) {
                peer_timer.stop();
                state = State::Active;
                if (!socket.setSocketDescriptor(socket.socketDescriptor(), QLocalSocket::ConnectedState,
                                                QIODevice::ReadWrite))
                    fail();
                else
                    writeRequest();
            } else if (decision != NativeDownloadPeerState::Pending) {
                fail();
            }
        } catch (...) {
            fail();
        }
    }
    void writeRequest() {
        if (state != State::Active || !timing() || state != State::Active)
            return;
        // A partial/error write may already have transferred a request. Publish
        // uncertainty before entering the socket; never retransmit its bytes.
        sent = true;
        if (socket.write(request_frame) != request_frame.size())
            fail();
        request_frame.clear();
    }
    void readAvailable() {
        if (state != State::Active || !timing() || state != State::Active)
            return;
        const auto remaining = maximum_response_bytes - response.size();
        if (remaining <= 0) {
            fail();
            return;
        }
        if (socket.bytesAvailable() > 0)
            response += socket.read(remaining);
        if (!expected_bytes && response.size() >= 4) {
            expected_bytes = qFromLittleEndian<quint32>(response.constData());
            if (*expected_bytes == 0 || *expected_bytes > native_download_ipc::response_max_payload_bytes) {
                fail();
                return;
            }
        }
        if (expected_bytes && response.size() > static_cast<qsizetype>(*expected_bytes) + 4)
            fail();
    }
    void finish() {
        if (state != State::Active)
            return;
        readAvailable();
        if (state != State::Active)
            return;
        if (!expected_bytes || response.size() != static_cast<qsizetype>(*expected_bytes) + 4) {
            fail();
            return;
        }
        const auto decoded = decodeNativeDownloadResponse(response.mid(4), request_id, item_count);
        if (!decoded) {
            fail();
            return;
        }
        result = *decoded;
        state = State::Completed;
        response.clear();
    }
};
QtNativeDownloadClient::QtNativeDownloadClient(NativeHostConfiguration configuration,
                                               QtNativeDownloadClientOptions options, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(configuration), std::move(options))) {}
QtNativeDownloadClient::~QtNativeDownloadClient() { shutdown(); }
DownloadAdmissionResult QtNativeDownloadClient::begin(const DownloadRequest& request) {
    if (QThread::currentThread() != thread() || impl_->used || impl_->state != Impl::State::Idle)
        return {};
    impl_->beginning = true;
    const auto release = qScopeGuard([&] { impl_->beginning = false; });
    impl_->used = true;
    if (!isValidNativeHostConfiguration(impl_->configuration) ||
        !validNativePeerImages(impl_->configuration.expected_owner_images))
        return {};
    const auto payload = encodeNativeDownloadRequest(request);
    if (payload.isEmpty())
        return {.result = {.error = DownloadSubmissionError::InvalidRequest}};
    impl_->request_id = request.request_id;
    impl_->item_count = request.items.size();
    impl_->request_frame =
        native_download_ipc::preamble(impl_->configuration.allowed_origin) + encodeNativeMessageFrame(payload);
    impl_->state = Impl::State::CheckingPeer;
    const auto current = impl_->now();
    if (!current || impl_->state != Impl::State::CheckingPeer)
        return {};
    impl_->started_ms = *current;
    // Qt's Windows connectToServer waits synchronously for up to five seconds
    // when every instance is busy. This fast port makes exactly one native open;
    // an occupied reservation or saturated listener must return without waiting.
    const auto name = (QStringLiteral("\\\\.\\pipe\\") + impl_->configuration.endpoint_name).toStdWString();
    const auto pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        impl_->fail();
    } else if (!impl_->socket.setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState,
                                                  QIODevice::WriteOnly)) {
        CloseHandle(pipe);
        impl_->fail();
    } else {
        // WriteOnly adopts ownership without starting the Windows Qt reader.
        // Neither the preamble nor request is written before the worker decision.
        try {
            const auto factory = impl_->options.peer_check_factory;
            impl_->peer = factory ? factory() : std::make_unique<WindowsNativeDownloadPeerCheck>();
            if (impl_->state != Impl::State::CheckingPeer || !impl_->peer ||
                !impl_->peer->begin(impl_->socket.socketDescriptor(), NativeDownloadPeerSide::Server,
                                    impl_->configuration.expected_owner_images)) {
                if (impl_->peer)
                    impl_->peer->shutdown();
                impl_->fail();
            } else if (impl_->state == Impl::State::CheckingPeer) {
                impl_->peer_timer.start();
            }
        } catch (...) {
            impl_->fail();
        }
    }
    if (impl_->state == Impl::State::Completed || impl_->state == Impl::State::Closed)
        return {.result = impl_->result};
    return {.state = DownloadAdmissionState::Pending};
}
std::optional<DownloadSubmissionResult> QtNativeDownloadClient::poll(const std::string_view id) {
    if (QThread::currentThread() != thread() || id != impl_->request_id || impl_->beginning || impl_->polling ||
        impl_->callback_depth != 0)
        return std::nullopt;
    impl_->polling = true;
    const auto release = qScopeGuard([&] { impl_->polling = false; });
    if (impl_->state == Impl::State::CheckingPeer)
        impl_->checkPeer();
    if (impl_->state == Impl::State::Active)
        static_cast<void>(impl_->timing());
    if (impl_->state == Impl::State::Uncertain)
        throw std::runtime_error{"native admission acknowledgement was not confirmed"};
    if (impl_->state == Impl::State::Completed)
        return impl_->result;
    return std::nullopt;
}
void QtNativeDownloadClient::shutdown() {
    if (QThread::currentThread() != thread())
        return;
    if (impl_->shutting_down)
        return;
    impl_->shutting_down = true;
    ++impl_->callback_depth;
    const auto release = qScopeGuard([&] {
        --impl_->callback_depth;
        impl_->shutting_down = false;
    });
    impl_->state = Impl::State::Closed;
    impl_->peer_timer.stop();
    if (impl_->peer)
        impl_->peer->shutdown();
    impl_->socket.abort();
    impl_->request_frame.clear();
    impl_->response.clear();
}
bool QtNativeDownloadClient::stopped() const {
    if (impl_->state != Impl::State::Closed || impl_->checking_peer || impl_->beginning || impl_->polling ||
        impl_->callback_depth != 0 || impl_->socket.state() != QLocalSocket::UnconnectedState)
        return false;
    ++impl_->callback_depth;
    const auto release = qScopeGuard([&] { --impl_->callback_depth; });
    try {
        return !impl_->peer || impl_->peer->stopped();
    } catch (...) {
        return false; // Unknown retirement never proves physical shutdown.
    }
}
} // namespace music_player::m0
