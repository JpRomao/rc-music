#include "WindowsLocalMp3Validator.hpp"

#include <QFileInfo>

#include <utility>

namespace music_player::m0 {
namespace {
bool terminal(LocalMp3ValidationState state) {
    return state == LocalMp3ValidationState::Validated || state == LocalMp3ValidationState::Failed ||
           state == LocalMp3ValidationState::Cancelled;
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
WindowsLocalMp3Validator::WindowsLocalMp3Validator(WindowsLocalMp3ValidatorConfig config)
    : config_(std::move(config)) {}
bool WindowsLocalMp3Validator::start(const QString& local_mp3, qint64 now_ms) {
    if (update_.state != LocalMp3ValidationState::Idle || now_ms < 0 || config_.total_timeout_ms <= 0 ||
        !config_.max_process_memory_bytes || !local_mp3.isValidUtf16() || downloadProbeArguments(local_mp3).isEmpty() ||
        downloadValidationArguments(local_mp3).isEmpty())
        return false;
    path_ = local_mp3;
    started_ms_ = previous_ms_ = now_ms;
    return launch(Stage::Probe, now_ms);
}
bool WindowsLocalMp3Validator::launch(Stage stage, qint64 now_ms) {
    stage_ = stage;
    probe_bytes_.clear();
    WindowsDownloadProcessConfig process;
    const bool probe = stage == Stage::Probe;
    process.executable = probe ? config_.ffprobe_executable : config_.ffmpeg_executable;
    process.arguments = probe ? downloadProbeArguments(path_) : downloadValidationArguments(path_);
    process.working_directory = QFileInfo(path_).absolutePath();
    process.max_memory_bytes = config_.max_process_memory_bytes;
    process.total_timeout_ms = config_.total_timeout_ms - (now_ms - started_ms_);
    process.max_stdout_bytes = probe ? 16384 : 1;
    process.max_stderr_bytes = 16384;
    if (process.arguments.isEmpty() || !process_.start(process, now_ms)) {
        update_.state = LocalMp3ValidationState::Failed;
        update_.error = failure(process_.poll(now_ms).error);
        return false;
    }
    update_.state = LocalMp3ValidationState::Validating;
    return true;
}
void WindowsLocalMp3Validator::stop(DownloadFailure error, bool cancelled) {
    if (terminal(update_.state) || update_.state == LocalMp3ValidationState::Stopping)
        return;
    update_.state = LocalMp3ValidationState::Stopping;
    update_.error = error;
    cancel_requested_ = cancelled;
    process_.cancel();
}
void WindowsLocalMp3Validator::cancel() {
    if (update_.state == LocalMp3ValidationState::Idle)
        update_.state = LocalMp3ValidationState::Cancelled;
    else
        stop(DownloadFailure::None, true);
}
LocalMp3ValidationUpdate WindowsLocalMp3Validator::poll(qint64 now_ms) {
    if (update_.state == LocalMp3ValidationState::Idle || terminal(update_.state))
        return update_;
    if (update_.state != LocalMp3ValidationState::Stopping) {
        if (now_ms < previous_ms_)
            stop(DownloadFailure::InvalidResult);
        else if (now_ms - started_ms_ >= config_.total_timeout_ms)
            stop(DownloadFailure::Timeout);
        previous_ms_ = now_ms;
    }
    const auto result = process_.poll(now_ms);
    if (update_.state == LocalMp3ValidationState::Stopping) {
        if (processTerminal(result.state))
            update_.state = cancel_requested_ ? LocalMp3ValidationState::Cancelled : LocalMp3ValidationState::Failed;
        return update_;
    }
    if (result.state == WindowsDownloadProcessState::Failed || result.state == WindowsDownloadProcessState::Stopping) {
        stop(failure(result.error));
        if (processTerminal(result.state))
            update_.state = LocalMp3ValidationState::Failed;
        return update_;
    }
    if (stage_ == Stage::Probe) {
        if (result.stdout_chunk.size() > 16384 - probe_bytes_.size())
            stop(DownloadFailure::LimitExceeded);
        else
            probe_bytes_ += result.stdout_chunk;
    } else if (!result.stdout_chunk.isEmpty())
        stop(DownloadFailure::InvalidResult);
    if (update_.state == LocalMp3ValidationState::Stopping) {
        if (processTerminal(result.state))
            update_.state = LocalMp3ValidationState::Failed;
        return update_;
    }
    if (result.state != WindowsDownloadProcessState::Exited)
        return update_;
    if (!result.exit_code || *result.exit_code != 0) {
        update_.state = LocalMp3ValidationState::Failed;
        update_.error = DownloadFailure::Conversion;
        return update_;
    }
    if (stage_ == Stage::Probe) {
        const auto inspection = decodeDownloadAudioProbe(probe_bytes_, true);
        if (!inspection.audio) {
            update_.state = LocalMp3ValidationState::Failed;
            update_.error = inspection.error;
        } else
            static_cast<void>(launch(Stage::Decode, now_ms));
    } else
        update_.state = LocalMp3ValidationState::Validated;
    return update_;
}
} // namespace music_player::m0
