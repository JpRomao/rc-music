#include "WindowsYoutubeBrokerProcess.hpp"

#include "WindowsDownloadProcess.hpp"
#include "YoutubeBrokerProtocol.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr qint64 broker_timeout_ms = 60'000;
constexpr quint64 broker_stderr_bytes = 128 * 1024;
constexpr quint64 broker_memory_bytes = 512ULL * 1024 * 1024;

QString owned(const QString& value) { return QString(value.constData(), value.size()); }
bool validVideoId(const QString& value) {
    if (value.size() != 11)
        return false;
    for (const auto unit : value)
        if (!((unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') ||
              unit == u'_' || unit == u'-'))
            return false;
    return true;
}
std::optional<QProcessEnvironment> copiedEnvironment(const QProcessEnvironment& source) {
    const auto keys = source.keys();
    if (keys.size() > 128 || !source.contains(QStringLiteral("PATH")) ||
        source.value(QStringLiteral("SystemRoot")).isEmpty())
        return {};
    QProcessEnvironment result;
    qsizetype total = 1;
    for (const auto& key : keys) {
        const auto value = source.value(key);
        if (key.isEmpty() || key.contains(QChar::Null) || key.contains(u'=') || value.contains(QChar::Null) ||
            key.size() > download_process_max_environment_units - total)
            return {};
        total += key.size();
        if (value.size() > download_process_max_environment_units - total - 2)
            return {};
        total += value.size() + 2;
        result.insert(owned(key), owned(value));
    }
    for (const auto* key : {"NODE_OPTIONS", "NODE_PATH", "PYTHONPATH", "PYTHONHOME", "PYTHONUSERBASE", "PYTHONSTARTUP",
                            "PYTHONINSPECT", "PYTHONBREAKPOINT"})
        result.remove(QString::fromLatin1(key));
    return result;
}
DownloadFailure failure(const WindowsDownloadProcessError error) {
    switch (error) {
    case WindowsDownloadProcessError::InvalidConfiguration:
    case WindowsDownloadProcessError::Io:
        return DownloadFailure::InvalidResult;
    case WindowsDownloadProcessError::Isolation:
        return DownloadFailure::Restricted;
    case WindowsDownloadProcessError::LimitExceeded:
        return DownloadFailure::LimitExceeded;
    case WindowsDownloadProcessError::Timeout:
        return DownloadFailure::Timeout;
    case WindowsDownloadProcessError::Start:
    case WindowsDownloadProcessError::None:
        return DownloadFailure::Unavailable;
    }
    return DownloadFailure::InvalidResult;
}
bool terminal(const WindowsDownloadProcessState state) {
    return state == WindowsDownloadProcessState::Exited || state == WindowsDownloadProcessState::Failed ||
           state == WindowsDownloadProcessState::Cancelled;
}
DownloadFailure readScript(const QString& path, QByteArray& output) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return DownloadFailure::Restricted;
    const auto size = file.size();
    if (size > youtube_broker_max_script_bytes)
        return DownloadFailure::LimitExceeded;
    if (size <= 0)
        return DownloadFailure::Restricted;
    auto bytes = file.read(size);
    if (file.error() != QFileDevice::NoError || bytes.size() != size || !file.atEnd())
        return DownloadFailure::Restricted;
    const auto decoded = QString::fromUtf8(bytes);
    if (decoded.trimmed().isEmpty() || decoded.toUtf8() != bytes)
        return DownloadFailure::Restricted;
    output = std::move(bytes);
    return DownloadFailure::None;
}
} // namespace

struct WindowsYoutubeBrokerProcess::Impl final {
    enum class Phase { AwaitFrame, Node, SendingResponse, Final };
    struct Child final {
        std::unique_ptr<WindowsDownloadProcess> process{};
        bool refused_start{};
    };
    Prepare prepare;
    std::optional<WindowsYoutubeBrokerPreparedRuntime> retained{};
    QString video_id{};
    QByteArray core{}, library{}, node_program{}, node_output{}, response{}, final_json{};
    std::optional<YoutubeBrokerChallenge> challenge{};
    YoutubeBrokerFrameReader reader{};
    std::array<char, 4> prefix{};
    qsizetype prefix_size{}, program_offset{}, response_offset{};
    quint32 expected_sequence{1};
    quint64 traffic_bytes{}, stderr_bytes{};
    qint64 started_ms{}, last_now{};
    Child extractor{}, node{};
    Phase phase{Phase::AwaitFrame};
    YoutubeResolverProcessUpdate update{.state = YoutubeResolverProcessState::Failed,
                                        .error = DownloadFailure::Unavailable};
    std::optional<DownloadFailure> first_failure{};
    bool attempted{}, preparing{}, acknowledged{true}, stopping{}, user_cancelled{}, node_input_closed{},
        extractor_launch_pending{};

    explicit Impl(Prepare source) : prepare(std::move(source)) {}
    void releaseRuntime() {
        // Raw process objects must already have acknowledged native exit and
        // every owned pipe before the authenticated tree may be released.
        retained.reset();
        extractor_launch_pending = false;
        video_id.clear();
        core.clear();
        library.clear();
        node_program.clear();
        node_output.clear();
        response.clear();
        final_json.clear();
        challenge.reset();
    }
    void noNativeFailure(const DownloadFailure error) {
        update = {.state =
                      user_cancelled ? YoutubeResolverProcessState::Cancelled : YoutubeResolverProcessState::Failed,
                  .error = user_cancelled ? DownloadFailure::None : error};
        acknowledged = true;
        preparing = false;
        releaseRuntime();
    }
    void requestStop() noexcept {
        for (auto* child : {&extractor, &node}) {
            if (!child->process)
                continue;
            try {
                child->process->cancel();
            } catch (...) {
                if (!first_failure)
                    first_failure = DownloadFailure::InvalidResult;
            }
        }
    }
    void stop(const DownloadFailure error = DownloadFailure::None) noexcept {
        if (error != DownloadFailure::None && !first_failure)
            first_failure = error;
        stopping = true;
        requestStop();
    }
    bool accountTraffic(const quint64 count) {
        if (count > youtube_broker_max_traffic_bytes - traffic_bytes) {
            stop(DownloadFailure::LimitExceeded);
            return false;
        }
        traffic_bytes += count;
        return true;
    }
    void accountStderr(const QByteArray& bytes) {
        if (static_cast<quint64>(bytes.size()) > broker_stderr_bytes - stderr_bytes)
            stop(DownloadFailure::LimitExceeded);
        else
            stderr_bytes += static_cast<quint64>(bytes.size());
        // Diagnostics can contain URLs, signatures or arbitrary child text.
        // Count them across all siblings, but never expose their contents.
    }
    WindowsDownloadProcessConfig configuration(const WindowsYoutubeRuntimeRole role, const qint64 now_ms) const {
        WindowsDownloadProcessConfig config;
        config.executable = owned(retained->files->artifactPath(role));
        config.arguments = role == WindowsYoutubeRuntimeRole::Extractor
                               ? QStringList{owned(video_id)}
                               : QStringList{QStringLiteral("--permission"), QStringLiteral("-")};
        config.working_directory = retained->files->directory() + (role == WindowsYoutubeRuntimeRole::Extractor
                                                                       ? QStringLiteral("/extractor")
                                                                       : QStringLiteral("/javascript"));
        config.environment = retained->environment;
        config.max_stdout_bytes = role == WindowsYoutubeRuntimeRole::Extractor
                                      ? youtube_broker_max_traffic_bytes + youtube_broker_max_frame_bytes + 4
                                      : static_cast<quint64>(youtube_broker_max_response_bytes);
        config.max_stderr_bytes = broker_stderr_bytes;
        config.max_memory_bytes = broker_memory_bytes;
        config.total_timeout_ms = std::max<qint64>(1, broker_timeout_ms - (now_ms - started_ms));
        config.interactive_input = WindowsDownloadInteractiveInputLimits{
            .max_buffered_bytes = download_process_max_input_bytes,
            .max_total_bytes =
                role == WindowsYoutubeRuntimeRole::Extractor ? youtube_broker_max_traffic_bytes : 16ULL * 1024 * 1024};
        return config;
    }
    bool launch(Child& child, const WindowsYoutubeRuntimeRole role, const qint64 now_ms) {
        auto config = configuration(role, now_ms);
        child.process = std::make_unique<WindowsDownloadProcess>();
        // Check the entire closed inventory immediately before every launch,
        // including the second and later Node siblings.
        if (!retained->files->unchanged()) {
            child.process.reset(); // Still passive: native start was not called.
            stop(DownloadFailure::Restricted);
            return false;
        }
        try {
            const bool accepted = child.process->start(config, now_ms);
            child.refused_start = !accepted;
            if (!accepted)
                stop(); // False is not an exit/pipe ACK; poll the child below.
            return accepted;
        } catch (...) {
            child.refused_start = true;
            stop(DownloadFailure::Unavailable);
            return false;
        }
    }
    bool beginNode(YoutubeBrokerChallenge value, const qint64 now_ms) {
        auto program = youtubeBrokerNodeInput(value, core, library);
        if (!program) {
            // The decoded challenge and authenticated scripts are already
            // valid; their combined serialized Node program can exceed 16 MiB.
            stop(DownloadFailure::LimitExceeded);
            return false;
        }
        challenge = std::move(value);
        node_program = std::move(*program);
        program_offset = 0;
        node_output.clear();
        node_input_closed = false;
        phase = Phase::Node;
        return launch(node, WindowsYoutubeRuntimeRole::Node, now_ms);
    }
    void consumeExtractor(const QByteArray& bytes, const bool native_terminal, const qint64 now_ms) {
        if (bytes.isEmpty())
            return;
        if (phase != Phase::AwaitFrame) {
            stop(DownloadFailure::InvalidResult);
            return;
        }
        if (prefix_size < 4) {
            const auto count = std::min<qsizetype>(4 - prefix_size, bytes.size());
            std::copy_n(bytes.constData(), count, prefix.data() + prefix_size);
            prefix_size += count;
            if (prefix_size == 4) {
                const auto length = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(prefix.data()));
                if (length > static_cast<quint32>(youtube_broker_max_frame_bytes)) {
                    stop(DownloadFailure::LimitExceeded);
                    return;
                }
                if (length == 0) {
                    stop(DownloadFailure::InvalidResult);
                    return;
                }
            }
        }
        if (!reader.append(QByteArrayView(bytes))) {
            stop(DownloadFailure::InvalidResult);
            return;
        }
        auto payload = reader.take();
        if (!payload)
            return;
        prefix_size = 0;
        const auto wire_bytes = static_cast<quint64>(payload->size()) + 4;
        auto decoded = decodeYoutubeBrokerMessage(std::move(*payload), expected_sequence);
        if (!decoded.message || decoded.error != DownloadFailure::None) {
            stop(decoded.error == DownloadFailure::None ? DownloadFailure::InvalidResult : decoded.error);
            return;
        }
        auto& message = *decoded.message;
        switch (message.kind) {
        case YoutubeBrokerMessageKind::Challenge:
            if (!message.challenge || native_terminal) {
                stop(DownloadFailure::InvalidResult);
                return;
            }
            if (accountTraffic(wire_bytes))
                static_cast<void>(beginNode(std::move(*message.challenge), now_ms));
            break;
        case YoutubeBrokerMessageKind::Result:
            final_json = std::move(message.result_json);
            phase = Phase::Final;
            if (!native_terminal && !extractor.process->closeStandardInput())
                stop(DownloadFailure::InvalidResult);
            break;
        case YoutubeBrokerMessageKind::Failure:
            stop(message.failure == DownloadFailure::None ? DownloadFailure::InvalidResult : message.failure);
            break;
        default:
            stop(DownloadFailure::InvalidResult);
            break;
        }
    }
    std::optional<WindowsDownloadProcessUpdate> sampleChild(Child& child, const qint64 now_ms) {
        try {
            auto native = child.process->poll(now_ms);
            accountStderr(native.stderr_chunk);
            if (!stopping || (!first_failure && !user_cancelled)) {
                if (native.state == WindowsDownloadProcessState::Failed)
                    stop(failure(native.error));
                else if (native.state == WindowsDownloadProcessState::Exited &&
                         (!native.exit_code || *native.exit_code != 0))
                    stop(native.exit_code ? DownloadFailure::Unavailable : DownloadFailure::InvalidResult);
                else if (native.state == WindowsDownloadProcessState::Cancelled)
                    stop(DownloadFailure::Unavailable);
                else if (native.state != WindowsDownloadProcessState::Running &&
                         native.state != WindowsDownloadProcessState::Stopping &&
                         native.state != WindowsDownloadProcessState::Exited)
                    stop(DownloadFailure::InvalidResult);
            }
            return native;
        } catch (...) {
            stop(DownloadFailure::InvalidResult);
            return {};
        }
    }
    void sampleExtractor(const qint64 now_ms) {
        auto native = sampleChild(extractor, now_ms);
        if (!native)
            return;
        const bool ended = terminal(native->state);
        if (!stopping)
            consumeExtractor(native->stdout_chunk, ended, now_ms);
        if (!ended)
            return;
        if (!stopping) {
            if (extractor.refused_start)
                stop(DownloadFailure::Unavailable);
            else if (!reader.finish() || phase != Phase::Final)
                stop(DownloadFailure::InvalidResult);
        }
        extractor.process.reset(); // Terminal update is native + all-pipe ACK.
    }
    void sampleNode(const qint64 now_ms) {
        auto native = sampleChild(node, now_ms);
        if (!native)
            return;
        if (!stopping) {
            if (native->stdout_chunk.size() > youtube_broker_max_response_bytes - node_output.size())
                stop(DownloadFailure::LimitExceeded);
            else
                node_output.append(native->stdout_chunk);
        }
        if (!terminal(native->state))
            return;
        const bool refused = node.refused_start;
        node.process.reset(); // No response is forwarded before this ACK.
        if (stopping)
            return;
        if (refused) {
            stop(DownloadFailure::Unavailable);
            return;
        }
        if (!challenge || phase != Phase::Node || !node_input_closed || program_offset != node_program.size()) {
            stop(DownloadFailure::InvalidResult);
            return;
        }
        auto encoded = encodeYoutubeBrokerResponse(expected_sequence, *challenge, std::move(node_output));
        if (!encoded) {
            stop(DownloadFailure::InvalidResult);
            return;
        }
        if (!accountTraffic(static_cast<quint64>(encoded->size())))
            return;
        response = std::move(*encoded);
        response_offset = 0;
        node_program.clear();
        node_output.clear();
        challenge.reset();
        phase = Phase::SendingResponse;
    }
    bool enqueue(Child& child, const QByteArray& bytes, qsizetype& offset) {
        if (!child.process) {
            stop(DownloadFailure::InvalidResult);
            return false;
        }
        const auto count = std::min<qsizetype>(download_process_max_input_bytes, bytes.size() - offset);
        const auto status = child.process->enqueueStandardInput(QByteArrayView(bytes).sliced(offset, count));
        switch (status) {
        case WindowsDownloadInputResult::Accepted:
            offset += count;
            return true;
        case WindowsDownloadInputResult::Backpressure:
            return false;
        case WindowsDownloadInputResult::Closed:
            stop(DownloadFailure::InvalidResult);
            return false;
        case WindowsDownloadInputResult::LimitExceeded:
            stop(DownloadFailure::LimitExceeded);
            return false;
        }
        stop(DownloadFailure::InvalidResult);
        return false;
    }
    void pumpInput() {
        if (phase == Phase::Node && !node_input_closed) {
            if (program_offset < node_program.size() && !enqueue(node, node_program, program_offset))
                return;
            if (program_offset == node_program.size()) {
                if (!node.process || !node.process->closeStandardInput())
                    stop(DownloadFailure::InvalidResult);
                else
                    node_input_closed = true;
            }
        } else if (phase == Phase::SendingResponse) {
            if (response_offset < response.size() && !enqueue(extractor, response, response_offset))
                return;
            if (response_offset == response.size()) {
                // Acceptance transfers an owned copy to the bounded native
                // queue. It is not proof of native write or peer consumption.
                // The child supervisor will reject success if accepted input
                // remains unwritten at its eventual terminal observation.
                response.clear();
                ++expected_sequence;
                phase = Phase::AwaitFrame;
            }
        }
    }
    void finishIfAcknowledged() {
        if (preparing || extractor.process || node.process)
            return;
        if (stopping) {
            update = {
                .state = user_cancelled && !first_failure ? YoutubeResolverProcessState::Cancelled
                                                          : YoutubeResolverProcessState::Failed,
                .error = first_failure.value_or(user_cancelled ? DownloadFailure::None : DownloadFailure::Unavailable)};
        } else if (phase == Phase::Final) {
            update = {
                .state = YoutubeResolverProcessState::Exited, .stdout_chunk = std::move(final_json), .exit_code = 0};
        } else {
            update = {.state = YoutubeResolverProcessState::Failed, .error = DownloadFailure::InvalidResult};
        }
        acknowledged = true;
        releaseRuntime();
    }
    YoutubeResolverProcessUpdate sample(const qint64 now_ms) {
        update.stdout_chunk.clear();
        update.stderr_chunk.clear();
        if (acknowledged || preparing)
            return update;
        if (now_ms < last_now)
            stop(DownloadFailure::InvalidResult);
        else {
            last_now = now_ms;
            if (static_cast<quint64>(now_ms) - static_cast<quint64>(started_ms) >=
                static_cast<quint64>(broker_timeout_ms))
                stop(DownloadFailure::Timeout);
        }
        if (stopping)
            requestStop();
        try {
            if (!stopping && extractor_launch_pending) {
                extractor_launch_pending = false;
                static_cast<void>(launch(extractor, WindowsYoutubeRuntimeRole::Extractor, last_now));
                if (!extractor.process)
                    finishIfAcknowledged();
                return update;
            }
            const bool had_node = static_cast<bool>(node.process);
            if (extractor.process)
                sampleExtractor(last_now);
            // Return after a synchronous launch/preparation before any Node
            // I/O, giving the worker owner its normal cancellation boundary.
            if (!stopping && !had_node && node.process)
                return update;
            if (node.process)
                sampleNode(last_now);
            if (!stopping)
                pumpInput();
        } catch (...) {
            stop(DownloadFailure::InvalidResult);
        }
        if (stopping)
            requestStop();
        finishIfAcknowledged();
        return update;
    }
    bool drainForDestruction() noexcept {
        if (acknowledged)
            return true;
        user_cancelled = true;
        stop();
        try {
            QElapsedTimer elapsed;
            elapsed.start();
            do {
                static_cast<void>(sample(last_now));
                if (acknowledged)
                    return true;
                QThread::msleep(1);
            } while (elapsed.elapsed() < 1000);
        } catch (...) {
        }
        requestStop();
        return acknowledged;
    }
};

WindowsYoutubeBrokerProcess::WindowsYoutubeBrokerProcess(Prepare prepare)
    : impl_(std::make_unique<Impl>(std::move(prepare))) {}
WindowsYoutubeBrokerProcess::~WindowsYoutubeBrokerProcess() {
    if (impl_ && !impl_->drainForDestruction()) {
        // Best effort cancellation is not an ACK. Preserve both raw process
        // state and the pinned runtime together if the owner failed to drain.
        static_cast<void>(impl_.release());
    }
}
bool WindowsYoutubeBrokerProcess::start(const QString& video_id, const qint64 now_ms) {
    auto& value = *impl_;
    if (value.attempted)
        return false;
    value.attempted = true;
    if (now_ms < 0 || !validVideoId(video_id)) {
        value.noNativeFailure(DownloadFailure::InvalidResult);
        return false;
    }
    value.started_ms = value.last_now = now_ms;
    value.update = {.state = YoutubeResolverProcessState::Running};
    value.acknowledged = false;
    value.preparing = true;
    try {
        // Snapshot before invoking trusted user code: even fromRawData input
        // must not be changed through a reentrant preparation callback.
        value.video_id = owned(video_id);
        auto prepare = std::move(value.prepare);
        auto prepared = prepare ? prepare() : std::nullopt;
        value.preparing = false;
        if (value.user_cancelled) {
            prepared.reset();
            value.noNativeFailure(DownloadFailure::None);
            return false;
        }
        if (!prepared || !prepared->files) {
            value.noNativeFailure(DownloadFailure::Unavailable);
            return false;
        }
        const auto environment = copiedEnvironment(prepared->environment);
        if (!environment) {
            value.noNativeFailure(DownloadFailure::InvalidResult);
            return false;
        }
        prepared->environment = *environment;
        value.retained = std::move(prepared);
        if (!value.retained->files->unchanged()) {
            value.noNativeFailure(DownloadFailure::Restricted);
            return false;
        }
        auto error = readScript(value.retained->files->artifactPath(WindowsYoutubeRuntimeRole::EjsCore), value.core);
        if (error == DownloadFailure::None)
            error =
                readScript(value.retained->files->artifactPath(WindowsYoutubeRuntimeRole::EjsLibrary), value.library);
        if (!value.retained->files->unchanged())
            error = DownloadFailure::Restricted;
        if (error != DownloadFailure::None) {
            value.noNativeFailure(error);
            return false;
        }
        // Return to the worker's cancellation boundary after potentially slow
        // preparation. The first valid, uncancelled poll owns native launch.
        value.extractor_launch_pending = true;
        return true;
    } catch (...) {
        value.preparing = false;
        if (value.extractor.process || value.node.process)
            value.stop(DownloadFailure::Unavailable);
        else
            value.noNativeFailure(DownloadFailure::Unavailable);
        return false;
    }
}
YoutubeResolverProcessUpdate WindowsYoutubeBrokerProcess::poll(const qint64 now_ms) { return impl_->sample(now_ms); }
void WindowsYoutubeBrokerProcess::cancel() {
    auto& value = *impl_;
    if (!value.attempted) {
        value.attempted = true;
        value.prepare = {};
        value.update = {.state = YoutubeResolverProcessState::Cancelled};
        return;
    }
    if (value.acknowledged)
        return;
    value.user_cancelled = true;
    value.stop();
}
} // namespace music_player::m0
