#include "WindowsDownloadJobSession.hpp"
#include "YoutubeAudioCodec.hpp"
#include "YoutubeAudioResolution.hpp"

#include <QScopedValueRollback>

#include <algorithm>

namespace music_player::m0 {
namespace {
bool validJobId(const std::string& value) {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
std::optional<QString> validatedUrl(const DownloadItem& item) {
    if (item.url.empty() || item.url.size() > 8192 ||
        (item.kind != DownloadMediaKind::Audio && item.kind != DownloadMediaKind::Video))
        return std::nullopt;
    const auto bytes = QByteArray::fromStdString(item.url);
    const auto url = QString::fromUtf8(bytes);
    if (url.toUtf8() != bytes || !parseDownloadNetworkTarget(url))
        return std::nullopt;
    return url;
}
bool conversionTerminal(LocalMediaConversionState state) {
    return state == LocalMediaConversionState::Idle || state == LocalMediaConversionState::Validated ||
           state == LocalMediaConversionState::Failed || state == LocalMediaConversionState::Cancelled;
}
DownloadFailure normalizedFailure(DownloadFailure error) {
    switch (error) {
    case DownloadFailure::None:
    case DownloadFailure::Unavailable:
    case DownloadFailure::Network:
    case DownloadFailure::Expired:
    case DownloadFailure::Restricted:
    case DownloadFailure::UnsupportedMedia:
    case DownloadFailure::NoAudio:
    case DownloadFailure::LimitExceeded:
    case DownloadFailure::Conversion:
    case DownloadFailure::Publication:
    case DownloadFailure::Import:
    case DownloadFailure::Timeout:
    case DownloadFailure::InvalidResult:
        return error;
    default:
        return DownloadFailure::InvalidResult;
    }
}
} // namespace

struct WindowsDownloadJobSession::Impl {
    enum class Stage { Idle, Downloading, NetworkDraining, Converting, Publishing, Stopping, Terminal };
    DirectDownloadIo& io;
    WindowsDownloadJobSessionConfig config;
    std::unique_ptr<WindowsDownloadWorkspace> workspace{};
    std::unique_ptr<WindowsLocalMediaConverter> converter{};
    std::unique_ptr<DirectDownloadSession> download{};
    DownloadExecutionUpdate update{};
    QString validated_output{};
    QString committed_output{};
    Stage stage{Stage::Idle};
    qint64 started_ms{0};
    qint64 previous_ms{0};
    DownloadFailure stop_error{DownloadFailure::None};
    bool stop_cancelled{false};
    bool cancel_requested{false};
    bool network_stop_requested{false};
    bool network_stopped{false};
    bool operating{false};

    Impl(DirectDownloadIo& source_io, WindowsDownloadJobSessionConfig configuration)
        : io(source_io), config(std::move(configuration)) {}
    ~Impl() {
        // Best effort only. The owner must poll to terminal before destruction;
        // process teardown precedes any attempted deletion of its local inputs.
        stage = Stage::Terminal;
        download.reset();
        converter.reset();
        if (workspace)
            static_cast<void>(workspace->cleanup());
    }

    void rememberFailure(DownloadFailure error) {
        error = normalizedFailure(error);
        if (error != DownloadFailure::None && stop_error == DownloadFailure::None) {
            stop_error = error;
            stop_cancelled = false;
        }
    }
    void requestNetworkStop() {
        if (!download || network_stop_requested)
            return;
        network_stop_requested = true;
        try {
            // DirectDownloadSession may already have cancelled on framing EOF,
            // failure or redirect. Explicit idempotent cancel establishes the
            // stop-seam precondition even when start failed before resolve.
            io.cancel();
        } catch (...) {
            rememberFailure(DownloadFailure::Network);
        }
    }
    void stop(DownloadFailure error, bool cancelled = false) {
        if (stage == Stage::Terminal)
            return;
        rememberFailure(error);
        if (cancelled && stop_error == DownloadFailure::None)
            stop_cancelled = true;
        if (stage == Stage::Stopping)
            return;
        stage = Stage::Stopping;
        if (download)
            download->cancel();
        requestNetworkStop();
        if (converter)
            converter->cancel();
    }
    bool pollNetworkStop() {
        if (!download || network_stopped)
            return true;
        requestNetworkStop();
        try {
            const auto result = io.pollStop();
            rememberFailure(result.error);
            // Error alone is never a release of ownership, even when fatal.
            network_stopped = result.stopped;
        } catch (...) {
            rememberFailure(DownloadFailure::Network);
        }
        if (stop_error != DownloadFailure::None || cancel_requested)
            stop(stop_error, cancel_requested && stop_error == DownloadFailure::None);
        return network_stopped;
    }
    void finishStop(qint64 now_ms) {
        if (!pollNetworkStop())
            return;
        if (converter) {
            try {
                const auto result = converter->poll(now_ms);
                if (!conversionTerminal(result.state))
                    return;
                if (result.state == LocalMediaConversionState::Failed)
                    rememberFailure(result.error);
            } catch (...) {
                // An exception is not proof that the decoder has stopped.
                rememberFailure(DownloadFailure::Conversion);
                return;
            }
        }
        const bool cleaned = !workspace || workspace->cleanup();
        update.published_mp3.clear();
        if (!cleaned) {
            update.state = DownloadExecutionState::Failed;
            update.error = DownloadFailure::Publication;
        } else if (stop_cancelled) {
            update.state = DownloadExecutionState::Cancelled;
            update.error = DownloadFailure::None;
        } else {
            update.state = DownloadExecutionState::Failed;
            update.error = stop_error == DownloadFailure::None ? DownloadFailure::InvalidResult : stop_error;
        }
        stage = Stage::Terminal;
    }
    bool checkObservedClock(qint64 now_ms) {
        if (now_ms < previous_ms || now_ms < 0) {
            stop(DownloadFailure::InvalidResult);
            return false;
        }
        previous_ms = now_ms;
        if (now_ms - started_ms >= config.total_timeout_ms) {
            stop(DownloadFailure::Timeout);
            return false;
        }
        return true;
    }
    void transferProgress(const DirectDownloadSnapshot& snapshot) {
        update.completed_units = snapshot.received_bytes;
        update.total_units = snapshot.total_bytes ? std::optional<std::uint64_t>{*snapshot.total_bytes} : std::nullopt;
    }
    void beginConversion(qint64 now_ms) {
        if (cancel_requested) {
            stop(DownloadFailure::None, true);
            return;
        }
        if (!workspace->sealInput()) {
            stop(workspace->error() == DownloadFailure::None ? DownloadFailure::Publication : workspace->error());
            return;
        }
        if (cancel_requested) {
            stop(DownloadFailure::None, true);
            return;
        }
        auto conversion_config = config.converter;
        conversion_config.total_timeout_ms =
            std::min(conversion_config.total_timeout_ms, config.total_timeout_ms - (now_ms - started_ms));
        converter = std::make_unique<WindowsLocalMediaConverter>(std::move(conversion_config));
        stage = Stage::Converting;
        update.phase = DownloadPhase::Converting;
        update.completed_units = 0;
        update.total_units.reset();
        const bool accepted = converter->start(*workspace, now_ms);
        if (cancel_requested)
            stop(DownloadFailure::None, true);
        else if (!accepted) {
            const auto result = converter->poll(now_ms);
            stop(result.error == DownloadFailure::None ? DownloadFailure::Unavailable : result.error);
        }
    }
    void beginNetworkDrain() {
        stage = Stage::NetworkDraining;
        requestNetworkStop();
    }
    void publish(qint64 now_ms) {
        // Publishing was visible during the preceding poll. The observed
        // clock/cancellation has been checked again before reaching here.
        const auto planned = workspace->plannedPublicationPath();
        // Allocate/normalize the DTO path before the commit point. Moving it
        // into the result later cannot fail and lose an already-published file.
        auto native_final = std::filesystem::path(planned.toStdWString()).lexically_normal();
        const auto final = workspace->validateAndPublish(
            [&](const QString& path) {
                return !cancel_requested && !validated_output.isEmpty() && path == validated_output && converter &&
                       converter->poll(now_ms).state == LocalMediaConversionState::Validated;
            },
            [&] { return !cancel_requested; });
        if (final.isEmpty()) {
            // A physically committed final remains private recovery evidence
            // even when the durability acknowledgement failed.
            committed_output = workspace->retainedPublicationPath();
            if (workspace->error() != DownloadFailure::None)
                stop(workspace->error());
            else if (cancel_requested)
                stop(DownloadFailure::None, true);
            else
                stop(DownloadFailure::Publication);
            return;
        }
        // Rename is the commit point. A late clock/cancellation observation
        // cannot hide an already-created final. Keep its identity/path even
        // when subsequent cleanup fails; recovery remains caller-owned.
        committed_output = final;
        const bool cleaned = workspace->cleanup();
        if (!cleaned || final != planned) {
            update.state = DownloadExecutionState::Failed;
            update.error = DownloadFailure::Publication;
            update.published_mp3.clear();
        } else {
            update.state = DownloadExecutionState::Published;
            update.phase = DownloadPhase::Publishing;
            update.completed_units = 1;
            update.total_units = 1;
            update.error = DownloadFailure::None;
            update.published_mp3 = std::move(native_final);
        }
        stage = Stage::Terminal;
    }
};

WindowsDownloadJobSession::WindowsDownloadJobSession(DirectDownloadIo& io, WindowsDownloadJobSessionConfig config)
    : impl_(std::make_unique<Impl>(io, std::move(config))) {}
WindowsDownloadJobSession::~WindowsDownloadJobSession() = default;
bool WindowsDownloadJobSession::startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> capability,
                                                     const qint64 now_ms, const qint64 trusted_epoch_seconds,
                                                     const YoutubePublication& publication) {
    if (!capability)
        return startImpl({}, {}, now_ms, {}, trusted_epoch_seconds);
    const auto job_id = capability->jobId();
    const DownloadItem item{canonicalYoutubeWatchUrl(capability->videoId().toStdString()).toStdString(),
                            capability->data().title.toStdString(), DownloadMediaKind::Audio};
    return startImpl(job_id, item, now_ms, std::move(capability), trusted_epoch_seconds, publication);
}
bool WindowsDownloadJobSession::start(const std::string& job_id, const DownloadItem& item, qint64 now_ms) {
    return startImpl(job_id, item, now_ms, {}, 0);
}
bool WindowsDownloadJobSession::startImpl(const std::string& job_id, const DownloadItem& item, const qint64 now_ms,
                                          std::shared_ptr<const ResolvedYoutubeAudioCapability> capability,
                                          const qint64 trusted_epoch_seconds, const YoutubePublication& publication) {
    auto& value = *impl_;
    if (value.operating || value.stage != Impl::Stage::Idle)
        return false;
    const QScopedValueRollback operation(value.operating, true);
    std::optional<QString> url;
    const bool matching_capability =
        !capability || (capability->jobId() == job_id && item.kind == DownloadMediaKind::Audio &&
                        item.url == canonicalYoutubeWatchUrl(capability->videoId().toStdString()).toStdString());
    if (capability) {
        if (const auto target = parseResolvedYoutubeNetworkTarget(*capability))
            url = target->url;
    } else {
        url = validatedUrl(item);
    }
    if (!validJobId(job_id) || !url || !matching_capability || now_ms < 0 || value.config.total_timeout_ms <= 0) {
        value.update.state = DownloadExecutionState::Failed;
        value.update.error = DownloadFailure::InvalidResult;
        value.stage = Impl::Stage::Terminal;
        return false;
    }
    value.update.job_id = job_id;
    if (capability && (trusted_epoch_seconds < 0 || trusted_epoch_seconds >= capability->expiresAtSeconds())) {
        value.update.state = DownloadExecutionState::Failed;
        value.update.error = trusted_epoch_seconds < 0 ? DownloadFailure::InvalidResult : DownloadFailure::Expired;
        value.stage = Impl::Stage::Terminal;
        return false;
    }
    value.started_ms = value.previous_ms = now_ms;
    value.stage = Impl::Stage::Downloading;
    try {
        auto workspace_config = value.config.workspace;
        if (capability) {
            workspace_config.publication_name = publication.file_name;
            value.config.converter.publication_metadata = publication.metadata;
        }
        value.workspace = value.config.ownership_store
                              ? std::make_unique<WindowsDownloadWorkspace>(std::move(workspace_config), job_id,
                                                                           *value.config.ownership_store)
                              : std::make_unique<WindowsDownloadWorkspace>(std::move(workspace_config));
        if (!value.workspace->create([&] { return !value.cancel_requested; })) {
            if (value.workspace->error() != DownloadFailure::None)
                value.stop(value.workspace->error());
            else if (value.cancel_requested)
                value.stop(DownloadFailure::None, true);
            else
                value.stop(DownloadFailure::Publication);
            value.finishStop(now_ms);
            return false;
        }
        if (value.cancel_requested) {
            value.stop(DownloadFailure::None, true);
            value.finishStop(now_ms);
            return false;
        }
        auto limits = value.config.network;
        limits.total_timeout_ms = std::min(limits.total_timeout_ms, value.config.total_timeout_ms);
        value.download = std::make_unique<DirectDownloadSession>(value.io, *value.workspace->inputSink(), limits);
        const bool accepted =
            capability ? value.download->startResolvedYoutube(std::move(capability), now_ms, trusted_epoch_seconds)
                       : value.download->start(*url, now_ms);
        if (value.cancel_requested)
            value.stop(DownloadFailure::None, true);
        else if (!accepted)
            value.stop(value.download->snapshot().error);
        if (value.stage == Impl::Stage::Stopping)
            value.finishStop(now_ms);
        return accepted && !value.cancel_requested;
    } catch (...) {
        value.stop(DownloadFailure::Unavailable);
        value.finishStop(now_ms);
        return false;
    }
}
void WindowsDownloadJobSession::cancel() {
    auto& value = *impl_;
    if (value.stage == Impl::Stage::Terminal || value.cancel_requested)
        return;
    value.cancel_requested = true;
    // DirectDownloadSession explicitly supports this reentrancy: cancel changes
    // its state before a borrowed read/resolve callback can continue writing.
    if (value.download)
        value.download->cancel();
    if (!value.operating)
        value.stop(DownloadFailure::None, true);
}
DownloadExecutionUpdate WindowsDownloadJobSession::poll(qint64 now_ms) {
    auto& value = *impl_;
    if (value.operating || value.stage == Impl::Stage::Idle || value.stage == Impl::Stage::Terminal)
        return value.update;
    const QScopedValueRollback operation(value.operating, true);
    if (value.cancel_requested)
        value.stop(DownloadFailure::None, true);
    // Draining is still active work. A prior cancellation has no error to
    // preserve; observed deadline/regression during its drain becomes Failed.
    static_cast<void>(value.checkObservedClock(now_ms));
    try {
        if (value.stage == Impl::Stage::Downloading) {
            value.download->tick(now_ms);
            const auto result = value.download->snapshot();
            value.transferProgress(result);
            if (value.cancel_requested || result.state == DirectDownloadState::Cancelled)
                value.stop(DownloadFailure::None, true);
            else if (result.state == DirectDownloadState::Failed)
                value.stop(value.workspace->error() == DownloadFailure::None ? result.error : value.workspace->error());
            else if (result.state == DirectDownloadState::Completed)
                value.beginNetworkDrain();
        } else if (value.stage == Impl::Stage::Converting) {
            const auto result = value.converter->poll(now_ms);
            value.update.completed_units = std::max<std::uint64_t>(value.update.completed_units, result.processed_us);
            value.update.total_units.reset(); // duration_us is an estimate, never a strict total.
            if (value.cancel_requested || result.state == LocalMediaConversionState::Cancelled)
                value.stop(DownloadFailure::None, true);
            else if (result.state == LocalMediaConversionState::Failed)
                value.stop(result.error);
            else if (result.state == LocalMediaConversionState::Validated) {
                value.validated_output = value.workspace->outputPath();
                value.stage = Impl::Stage::Publishing;
                value.update.phase = DownloadPhase::Publishing;
                value.update.completed_units = 0;
                value.update.total_units = 1;
            }
        } else if (value.stage == Impl::Stage::Publishing)
            value.publish(now_ms);
        if (value.stage == Impl::Stage::NetworkDraining) {
            // Hold Downloading telemetry and private sinks until native work
            // and every pipe have stopped. Never read/tick the transfer again.
            if (!value.pollNetworkStop())
                return value.update;
            if (value.stage == Impl::Stage::NetworkDraining)
                value.beginConversion(now_ms);
        }
    } catch (...) {
        value.stop(value.committed_output.isEmpty() ? DownloadFailure::Conversion : DownloadFailure::Publication);
    }
    if (value.stage == Impl::Stage::Stopping)
        value.finishStop(now_ms);
    return value.update;
}
QString WindowsDownloadJobSession::retainedPublicationPath() const {
    return impl_->update.state == DownloadExecutionState::Failed && impl_->update.error == DownloadFailure::Publication
               ? impl_->committed_output
               : QString{};
}
} // namespace music_player::m0
