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
    const bool probe = stage == Stage::InputProbe;
    process_config.executable = probe ? config_.ffprobe_executable : config_.ffmpeg_executable;
    const auto path = workspace_->inputPath();
    process_config.working_directory = QFileInfo(path).absolutePath();
    process_config.max_memory_bytes = config_.max_process_memory_bytes;
    process_config.total_timeout_ms = config_.total_timeout_ms - (now_ms - started_ms_);
    process_config.max_stdout_bytes = probe ? 16384 : 512ULL * 1024 * 1024;
    process_config.max_stderr_bytes = probe ? 16384 : 4 * 1024 * 1024;
    if (probe)
        process_config.arguments = downloadProbeArguments(path);
    else
        process_config.arguments = downloadConversionArguments(path, input_container_);
    if (process_config.arguments.isEmpty() || !process_.start(process_config, now_ms)) {
        update_.state = LocalMediaConversionState::Failed;
        update_.error = failure(process_.poll(now_ms).error);
        return false;
    }
    update_.state = probe ? LocalMediaConversionState::Inspecting : LocalMediaConversionState::Converting;
    return true;
}

void WindowsLocalMediaConverter::stop(DownloadFailure error, bool cancelled) {
    if (terminal(update_.state) || update_.state == LocalMediaConversionState::Stopping)
        return;
    update_.error = error;
    cancel_requested_ = cancelled;
    update_.state = LocalMediaConversionState::Stopping;
    if (stage_ == Stage::Validation && validator_)
        validator_->cancel();
    else
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
    if (stage_ == Stage::Validation) {
        const auto result = validator_->poll(now_ms);
        const bool finished = result.state == LocalMp3ValidationState::Validated ||
                              result.state == LocalMp3ValidationState::Failed ||
                              result.state == LocalMp3ValidationState::Cancelled;
        if (update_.state == LocalMediaConversionState::Stopping) {
            if (finished)
                update_.state =
                    cancel_requested_ ? LocalMediaConversionState::Cancelled : LocalMediaConversionState::Failed;
        } else if (result.state == LocalMp3ValidationState::Validated)
            update_.state = LocalMediaConversionState::Validated;
        else if (result.state == LocalMp3ValidationState::Failed) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error = result.error;
        } else if (result.state == LocalMp3ValidationState::Stopping)
            stop(result.error);
        return update_;
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
    if (stage_ == Stage::InputProbe) {
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
    }
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
    if (stage_ == Stage::InputProbe) {
        const auto inspection = decodeDownloadAudioProbe(probe_bytes_, false);
        if (!inspection.audio) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error = inspection.error;
        } else {
            input_container_ = inspection.audio->container;
            update_.duration_us = inspection.audio->duration_us;
            static_cast<void>(launch(Stage::Conversion, now_ms));
        }
    } else if (stage_ == Stage::Conversion) {
        if (!progress_.finishEof() || !workspace_->sealOutput()) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error =
                workspace_->error() == DownloadFailure::None ? DownloadFailure::Conversion : workspace_->error();
        } else {
            validator_ = std::make_unique<WindowsLocalMp3Validator>(WindowsLocalMp3ValidatorConfig{
                config_.ffmpeg_executable, config_.ffprobe_executable, config_.max_process_memory_bytes,
                config_.total_timeout_ms - (now_ms - started_ms_)});
            stage_ = Stage::Validation;
            update_.state = LocalMediaConversionState::Validating;
            if (!validator_->start(workspace_->outputPath(), now_ms)) {
                update_.state = LocalMediaConversionState::Failed;
                const auto error = validator_->poll(now_ms).error;
                update_.error = error == DownloadFailure::None ? DownloadFailure::InvalidResult : error;
            }
        }
    }
    return update_;
}
} // namespace music_player::m0
