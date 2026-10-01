#include "WindowsLocalMediaConverter.hpp"

#include <QFileInfo>

namespace music_player::m0 {
namespace {
bool terminal(LocalMediaConversionState state) {
    return state == LocalMediaConversionState::Validated || state == LocalMediaConversionState::Failed ||
           state == LocalMediaConversionState::Cancelled;
}
bool processTerminal(WindowsDownloadProcessState state) {
    return state == WindowsDownloadProcessState::Exited || state == WindowsDownloadProcessState::Failed ||
           state == WindowsDownloadProcessState::Cancelled;
}
DownloadFailure failure(WindowsDownloadProcessError error) {
    if (error == WindowsDownloadProcessError::LimitExceeded)
        return DownloadFailure::LimitExceeded;
    if (error == WindowsDownloadProcessError::Timeout)
        return DownloadFailure::Timeout;
    if (error == WindowsDownloadProcessError::InvalidConfiguration)
        return DownloadFailure::InvalidResult;
    return DownloadFailure::Conversion;
}
} // namespace

WindowsLocalMediaConverter::WindowsLocalMediaConverter(WindowsLocalMediaConverterConfig config)
    : config_(std::move(config)) {}
bool WindowsLocalMediaConverter::start(WindowsDownloadWorkspace& workspace, qint64 now_ms) {
    if (update_.state != LocalMediaConversionState::Idle || now_ms < 0 || config_.total_timeout_ms <= 0 ||
        config_.max_process_memory_bytes == 0 || workspace.error() != DownloadFailure::None ||
        workspace.inputPath().isEmpty() || !workspace.outputPath().isEmpty() || !workspace.outputSink() ||
        !workspace.outputSink()->isWritable() || workspace.outputSink()->size() != 0)
        return false;
    workspace_ = &workspace;
    started_ms_ = previous_ms_ = now_ms;
    return launch(Stage::InputProbe, now_ms);
}

bool WindowsLocalMediaConverter::launch(Stage stage, qint64 now_ms) {
    stage_ = stage;
    probe_bytes_.clear();
    WindowsDownloadProcessConfig process_config;
    const bool probe = stage == Stage::InputProbe || stage == Stage::OutputProbe;
    process_config.executable = probe ? config_.ffprobe_executable : config_.ffmpeg_executable;
    const auto path =
        stage == Stage::InputProbe || stage == Stage::Conversion ? workspace_->inputPath() : workspace_->outputPath();
    process_config.working_directory = QFileInfo(path).absolutePath();
    process_config.max_memory_bytes = config_.max_process_memory_bytes;
    process_config.total_timeout_ms = config_.total_timeout_ms - (now_ms - started_ms_);
    process_config.max_stdout_bytes = probe ? 16384 : stage == Stage::Conversion ? 512ULL * 1024 * 1024 : 1;
    process_config.max_stderr_bytes = stage == Stage::Conversion ? 4 * 1024 * 1024 : 16384;
    if (probe)
        process_config.arguments = downloadProbeArguments(path);
    else if (stage == Stage::Conversion)
        process_config.arguments = downloadConversionArguments(path, input_container_);
    else
        process_config.arguments = downloadValidationArguments(path);
    if (process_config.arguments.isEmpty() || !process_.start(process_config, now_ms)) {
        update_.state = LocalMediaConversionState::Failed;
        update_.error = failure(process_.poll(now_ms).error);
        return false;
    }
    update_.state = stage == Stage::InputProbe   ? LocalMediaConversionState::Inspecting
                    : stage == Stage::Conversion ? LocalMediaConversionState::Converting
                                                 : LocalMediaConversionState::Validating;
    return true;
}

void WindowsLocalMediaConverter::stop(DownloadFailure error, bool cancelled) {
    if (terminal(update_.state) || update_.state == LocalMediaConversionState::Stopping)
        return;
    update_.error = error;
    cancel_requested_ = cancelled;
    update_.state = LocalMediaConversionState::Stopping;
    process_.cancel();
}

void WindowsLocalMediaConverter::cancel() {
    if (update_.state == LocalMediaConversionState::Idle)
        update_.state = LocalMediaConversionState::Cancelled;
    else
        stop(DownloadFailure::None, true);
}

LocalMediaConversionUpdate WindowsLocalMediaConverter::poll(qint64 now_ms) {
    if (update_.state == LocalMediaConversionState::Idle || terminal(update_.state))
        return update_;
    if (update_.state != LocalMediaConversionState::Stopping) {
        if (now_ms < previous_ms_)
            stop(DownloadFailure::InvalidResult);
        else if (now_ms - started_ms_ >= config_.total_timeout_ms)
            stop(DownloadFailure::Timeout);
        previous_ms_ = now_ms;
    }
    const auto result = process_.poll(now_ms);
    if (update_.state == LocalMediaConversionState::Stopping) {
        if (processTerminal(result.state))
            update_.state =
                cancel_requested_ ? LocalMediaConversionState::Cancelled : LocalMediaConversionState::Failed;
        return update_;
    }
    if (result.state == WindowsDownloadProcessState::Failed || result.state == WindowsDownloadProcessState::Stopping) {
        stop(failure(result.error));
        if (processTerminal(result.state))
            update_.state = LocalMediaConversionState::Failed;
        return update_;
    }
    if (stage_ == Stage::InputProbe || stage_ == Stage::OutputProbe) {
        if (result.stdout_chunk.size() > 16384 - probe_bytes_.size())
            stop(DownloadFailure::LimitExceeded);
        else
            probe_bytes_ += result.stdout_chunk;
    } else if (stage_ == Stage::Conversion) {
        auto* sink = workspace_->outputSink();
        if (!sink ||
            (!result.stdout_chunk.isEmpty() && sink->write(result.stdout_chunk) != result.stdout_chunk.size())) {
            stop(workspace_->error() == DownloadFailure::None ? DownloadFailure::Conversion : workspace_->error());
        } else if (!progress_.feed(result.stderr_chunk))
            stop(DownloadFailure::Conversion);
        else if (progress_.snapshot().processed_us)
            update_.processed_us = *progress_.snapshot().processed_us;
    } else if (!result.stdout_chunk.isEmpty())
        stop(DownloadFailure::InvalidResult);
    if (update_.state == LocalMediaConversionState::Stopping) {
        if (processTerminal(result.state))
            update_.state = LocalMediaConversionState::Failed;
        return update_;
    }
    if (result.state != WindowsDownloadProcessState::Exited)
        return update_;
    if (!result.exit_code || *result.exit_code != 0) {
        update_.state = LocalMediaConversionState::Failed;
        update_.error = stage_ == Stage::InputProbe ? DownloadFailure::UnsupportedMedia : DownloadFailure::Conversion;
        return update_;
    }
    if (stage_ == Stage::InputProbe || stage_ == Stage::OutputProbe) {
        const auto inspection = decodeDownloadAudioProbe(probe_bytes_, stage_ == Stage::OutputProbe);
        if (!inspection.audio) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error = inspection.error;
        } else if (stage_ == Stage::InputProbe) {
            input_container_ = inspection.audio->container;
            update_.duration_us = inspection.audio->duration_us;
            static_cast<void>(launch(Stage::Conversion, now_ms));
        } else
            static_cast<void>(launch(Stage::Decode, now_ms));
    } else if (stage_ == Stage::Conversion) {
        if (!progress_.finishEof() || !workspace_->sealOutput()) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error =
                workspace_->error() == DownloadFailure::None ? DownloadFailure::Conversion : workspace_->error();
        } else
            static_cast<void>(launch(Stage::OutputProbe, now_ms));
    } else
        update_.state = LocalMediaConversionState::Validated;
    return update_;
}
} // namespace music_player::m0
