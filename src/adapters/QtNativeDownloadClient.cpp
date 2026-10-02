#include "adapters/QtNativeDownloadClient.hpp"

#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QThread>
#include <QtEndian>

#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr quint64 exchange_timeout_ms = 5'000;
constexpr qsizetype maximum_response_bytes = native_download_ipc::response_max_payload_bytes + 4 + 1;
} // namespace
struct QtNativeDownloadClient::Impl {
    enum class State { Idle, Active, Completed, Uncertain, Closed };
    QtNativeDownloadClient& owner;
    NativeHostConfiguration configuration;
    QtNativeDownloadClientOptions options;
    QLocalSocket socket;
    QElapsedTimer elapsed;
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
        QObject::connect(&socket, &QLocalSocket::connected, &owner, [this] { writeRequest(); });
        QObject::connect(&socket, &QLocalSocket::readyRead, &owner, [this] { readAvailable(); });
        QObject::connect(&socket, &QLocalSocket::disconnected, &owner, [this] { finish(); });
        QObject::connect(&socket, &QLocalSocket::errorOccurred, &owner,
                         [this](const QLocalSocket::LocalSocketError error) {
                             if (error != QLocalSocket::PeerClosedError)
                                 fail();
                         });
    }
    std::optional<qint64> now() {
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
        if (!current || state != State::Active)
            return false;
        if (*current < started_ms ||
            static_cast<quint64>(*current) - static_cast<quint64>(started_ms) >= exchange_timeout_ms) {
            fail();
            return false;
        }
        return true;
    }
    void fail() {
        if (state != State::Active)
            return;
        state = sent ? State::Uncertain : State::Completed;
        result = {};
        socket.abort();
        request_frame.clear();
        response.clear();
    }
    void writeRequest() {
        if (!timing())
            return;
        // A partial/error write may already have transferred a request. Publish
        // uncertainty before entering the socket; never retransmit its bytes.
        sent = true;
        if (socket.write(request_frame) != request_frame.size())
            fail();
        request_frame.clear();
    }
    void readAvailable() {
        if (!timing())
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
    impl_->used = true;
    if (!isValidNativeHostConfiguration(impl_->configuration))
        return {};
    const auto payload = encodeNativeDownloadRequest(request);
    if (payload.isEmpty())
        return {.result = {.error = DownloadSubmissionError::InvalidRequest}};
    impl_->request_id = request.request_id;
    impl_->item_count = request.items.size();
    impl_->request_frame =
        native_download_ipc::preamble(impl_->configuration.allowed_origin) + encodeNativeMessageFrame(payload);
    impl_->state = Impl::State::Active;
    const auto current = impl_->now();
    if (!current || impl_->state != Impl::State::Active)
        return {};
    impl_->started_ms = *current;
    impl_->socket.connectToServer(impl_->configuration.endpoint_name);
    if (impl_->state == Impl::State::Completed)
        return {.result = impl_->result};
    return {.state = DownloadAdmissionState::Pending};
}
std::optional<DownloadSubmissionResult> QtNativeDownloadClient::poll(const std::string_view id) {
    if (QThread::currentThread() != thread() || id != impl_->request_id)
        return std::nullopt;
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
    impl_->state = Impl::State::Closed;
    impl_->socket.abort();
    impl_->request_frame.clear();
    impl_->response.clear();
}
bool QtNativeDownloadClient::stopped() const {
    return impl_->state == Impl::State::Closed && impl_->socket.state() == QLocalSocket::UnconnectedState;
}
} // namespace music_player::m0
