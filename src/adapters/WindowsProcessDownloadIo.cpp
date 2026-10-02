#include "WindowsProcessDownloadIo.hpp"

#include <QElapsedTimer>

#include <algorithm>
#include <limits>

namespace music_player::m0 {
namespace {
bool terminal(WindowsDownloadProcessState state) {
    return state == WindowsDownloadProcessState::Exited || state == WindowsDownloadProcessState::Failed ||
           state == WindowsDownloadProcessState::Cancelled;
}
DownloadFailure processFailure(WindowsDownloadProcessError error) {
    if (error == WindowsDownloadProcessError::Timeout)
        return DownloadFailure::Timeout;
    if (error == WindowsDownloadProcessError::LimitExceeded)
        return DownloadFailure::LimitExceeded;
    return DownloadFailure::Unavailable;
}
DownloadFailure workerFailure(DownloadWorkerError error) {
    if (error == DownloadWorkerError::InvalidRequest)
        return DownloadFailure::InvalidResult;
    if (error == DownloadWorkerError::Runtime)
        return DownloadFailure::Unavailable;
    return DownloadFailure::Network;
}
} // namespace

struct WindowsProcessDownloadIo::Impl final {
    WindowsProcessDownloadIoConfig config;
    QElapsedTimer clock{};
    std::unique_ptr<WindowsDownloadProcess> process{};
    std::optional<DownloadWorkerRequest> pending{};
    DownloadWorkerMode mode{DownloadWorkerMode::Resolve};
    DownloadResolution dns{};
    PinnedDownloadStreamUpdate stream{};
    DownloadWorkerFrameDecoder decoder{};
    QByteArray raw{};
    QByteArray readable{};
    QList<QHostAddress> candidates{};
    DownloadFailure error{DownloadFailure::None};
    std::optional<int> exit_code{};
    quint64 wire_bytes{0};
    bool native_stopped{true};
    bool natural_exit{false};
    bool cancelled{false};
    bool addresses_seen{false};
    bool terminal_seen{false};
    bool authenticated_eof{false};

    explicit Impl(WindowsProcessDownloadIoConfig value) : config(std::move(value)) { clock.start(); }
    void fail(DownloadFailure cause) {
        if (error == DownloadFailure::None)
            error = cause;
        dns = {DownloadResolutionState::Failed, {}};
        stream = {PinnedDownloadStreamState::Failed, PinnedDownloadStreamError::Connection};
        readable.clear();
        pending.reset();
        if (process && !native_stopped)
            process->cancel();
    }
    bool drained() const {
        return native_stopped && raw.isEmpty() && !decoder.ready() &&
               (cancelled || error != DownloadFailure::None || !decoder.incomplete());
    }
    bool begin(const DownloadWorkerRequest& request) {
        const auto input = encodeDownloadWorkerRequest(request);
        if (!config.process.environment || !config.process.arguments.isEmpty() ||
            !config.process.standard_input.isEmpty() || config.max_wire_bytes == 0 ||
            config.max_wire_bytes > (std::numeric_limits<quint64>::max() - 4096) / 6 || input.isEmpty()) {
            fail(DownloadFailure::Unavailable);
            return false;
        }
        // Previous object is destroyed only after native exit and pipe drain.
        process = std::make_unique<WindowsDownloadProcess>();
        mode = request.mode;
        decoder = {};
        raw.clear();
        readable.clear();
        candidates.clear();
        exit_code.reset();
        wire_bytes = 0;
        addresses_seen = terminal_seen = authenticated_eof = cancelled = natural_exit = false;
        native_stopped = false;
        dns = {};
        stream = {PinnedDownloadStreamState::Connecting};
        auto launch = config.process;
        launch.standard_input = input;
        // Worst case is one-byte Data per frame: 1 payload + 5 framing bytes.
        // Reserve bounded DNS/terminal controls; no optimistic full-frame ratio.
        launch.max_stdout_bytes = config.max_wire_bytes * 6 + 4096;
        if (!process->start(launch, clock.elapsed())) {
            fail(DownloadFailure::Unavailable);
            return false;
        }
        return true;
    }
    bool queue(DownloadWorkerRequest request) {
        if (error != DownloadFailure::None || pending)
            return false;
        // A redirect can arrive immediately after read()+cancel(), without
        // pollStop(). Validate the previous natural exit before begin() can
        // erase its terminal/exit evidence; live cancellations still drain.
        advance();
        if (error != DownloadFailure::None)
            return false;
        if (!drained()) {
            if (!cancelled || encodeDownloadWorkerRequest(request).isEmpty())
                return false;
            pending = std::move(request);
            dns = {};
            return true;
        }
        return begin(request);
    }
    void accept(DownloadWorkerFrame frame) {
        if (terminal_seen) {
            fail(DownloadFailure::InvalidResult);
            return;
        }
        if (frame.kind == DownloadWorkerFrameKind::Addresses) {
            if (mode != DownloadWorkerMode::Resolve || addresses_seen) {
                fail(DownloadFailure::InvalidResult);
                return;
            }
            addresses_seen = true;
            candidates = std::move(frame.addresses);
        } else if (frame.kind == DownloadWorkerFrameKind::Data) {
            if (mode != DownloadWorkerMode::Stream) {
                fail(DownloadFailure::InvalidResult);
                return;
            }
            if (static_cast<quint64>(frame.data.size()) > config.max_wire_bytes - wire_bytes) {
                fail(DownloadFailure::LimitExceeded);
                return;
            }
            wire_bytes += static_cast<quint64>(frame.data.size());
            if (!cancelled && error == DownloadFailure::None) {
                readable = std::move(frame.data);
                stream.state = PinnedDownloadStreamState::Receiving;
            }
        } else {
            terminal_seen = true;
            authenticated_eof = frame.authenticated_eof;
            if (frame.error != DownloadWorkerError::None)
                fail(workerFailure(frame.error));
            else if (mode == DownloadWorkerMode::Resolve && (!addresses_seen || authenticated_eof))
                fail(DownloadFailure::InvalidResult);
        }
    }
    void decodeAvailable() {
        // Bound controller CPU per call as well as retained bytes. Never retain
        // a frame queue: one readable slot backpressures the native child pipe.
        for (int iteration = 0; iteration < 64 && (!raw.isEmpty() || decoder.ready()); ++iteration) {
            if (!readable.isEmpty() && !cancelled && error == DownloadFailure::None)
                return;
            if (!decoder.ready()) {
                const auto consumed = decoder.append(QByteArrayView{raw});
                raw.remove(0, consumed);
                if (decoder.failed()) {
                    fail(DownloadFailure::InvalidResult);
                    raw.clear();
                    return;
                }
                if (!decoder.ready())
                    return;
            }
            auto frame = decoder.take();
            if (!frame) {
                fail(DownloadFailure::InvalidResult);
                raw.clear();
                return;
            }
            accept(std::move(*frame));
        }
    }
    void advance() {
        if (!process)
            return;
        decodeAvailable();
        if (raw.isEmpty() && (readable.isEmpty() || cancelled || error != DownloadFailure::None) && !native_stopped) {
            const auto update = process->poll(clock.elapsed());
            if (!update.stderr_chunk.isEmpty())
                fail(DownloadFailure::InvalidResult);
            if (update.error != WindowsDownloadProcessError::None)
                fail(processFailure(update.error));
            raw = update.stdout_chunk;
            if (raw.size() > download_process_poll_bytes) {
                fail(DownloadFailure::InvalidResult);
                raw.clear();
            }
            if (terminal(update.state)) {
                native_stopped = true;
                natural_exit = update.state == WindowsDownloadProcessState::Exited;
                exit_code = update.exit_code;
                if (!cancelled && update.state != WindowsDownloadProcessState::Exited && error == DownloadFailure::None)
                    fail(DownloadFailure::InvalidResult);
            }
            decodeAvailable();
        }
        if (!native_stopped || !raw.isEmpty() || !readable.isEmpty() || decoder.ready())
            return;
        // Cancellation may discard an unread payload, but cannot discard the
        // proof that a child had already exited naturally. Its complete IPC and
        // exit code still need validation after the readable slot is drained.
        if ((!cancelled || natural_exit) && error == DownloadFailure::None) {
            if (decoder.incomplete() || !terminal_seen || !exit_code || *exit_code != 0) {
                fail(DownloadFailure::InvalidResult);
                return;
            }
            if (!cancelled && mode == DownloadWorkerMode::Resolve)
                dns = {DownloadResolutionState::Resolved, candidates};
            else if (!cancelled)
                stream = {PinnedDownloadStreamState::Finished, PinnedDownloadStreamError::None, 0, authenticated_eof};
        }
        if (pending && drained() && error == DownloadFailure::None) {
            auto next = std::move(*pending);
            pending.reset();
            static_cast<void>(begin(next));
        }
    }
};
WindowsProcessDownloadIo::WindowsProcessDownloadIo(WindowsProcessDownloadIoConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
WindowsProcessDownloadIo::~WindowsProcessDownloadIo() = default;
bool WindowsProcessDownloadIo::resolve(const QString& host) {
    DownloadWorkerRequest request;
    request.runtime = impl_->config.runtime;
    request.host = host;
    return impl_->queue(std::move(request));
}
DownloadResolution WindowsProcessDownloadIo::resolution() const {
    impl_->advance();
    return impl_->dns;
}
bool WindowsProcessDownloadIo::open(const PinnedDownloadEndpoint& endpoint) {
    DownloadWorkerRequest request;
    request.mode = DownloadWorkerMode::Stream;
    request.runtime = impl_->config.runtime;
    request.endpoint = endpoint;
    return impl_->queue(std::move(request));
}
PinnedDownloadStreamUpdate WindowsProcessDownloadIo::poll() const {
    impl_->advance();
    auto update = impl_->stream;
    update.available_bytes = impl_->readable.size();
    return update;
}
QByteArray WindowsProcessDownloadIo::read(qint64 maximum) {
    impl_->advance();
    if (maximum <= 0 || impl_->cancelled || impl_->error != DownloadFailure::None)
        return {};
    const auto count = std::min<qint64>(maximum, impl_->readable.size());
    auto result = impl_->readable.left(count);
    impl_->readable.remove(0, count);
    return result;
}
void WindowsProcessDownloadIo::cancel() {
    auto& value = *impl_;
    value.cancelled = true;
    value.pending.reset();
    value.readable.clear();
    value.dns = {DownloadResolutionState::Failed, {}};
    if (value.error == DownloadFailure::None)
        value.stream = {PinnedDownloadStreamState::Cancelled};
    if (value.process && !value.native_stopped)
        value.process->cancel();
}
DownloadFailure WindowsProcessDownloadIo::failure() const { return impl_->error; }
DirectDownloadIoStopUpdate WindowsProcessDownloadIo::pollStop() {
    impl_->advance();
    return {impl_->drained() && !impl_->pending, impl_->error};
}
} // namespace music_player::m0
