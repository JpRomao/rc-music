#include "WindowsLocalMediaConverter.hpp"

#include "MediaFoundationWorkerProtocol.hpp"
#include "WindowsDownloadReadInput.hpp"
#include "WindowsMediaFoundationLaunch.hpp"

#include <QFileInfo>
#include <qt_windows.h>

#include <algorithm>
#include <array>

namespace music_player::m0 {
namespace {
void appendSynchsafeSize(QByteArray& bytes, const qsizetype size) {
    for (const int shift : {21, 14, 7, 0})
        bytes.append(static_cast<char>((size >> shift) & 0x7f));
}
std::optional<QByteArray> publicationTag(const DownloadMp3Metadata& metadata) {
    constexpr qsizetype max_field_bytes = 512;
    const auto field = [](const QString& input) -> std::optional<QByteArray> {
        if (input.size() > max_field_bytes || !input.isValidUtf16() || std::ranges::any_of(input, [](const QChar c) {
                const auto unit = c.unicode();
                return unit <= 0x1f || (unit >= 0x7f && unit <= 0x9f) || (unit >= 0x2028 && unit <= 0x202e) ||
                       (unit >= 0x2066 && unit <= 0x2069);
            }))
            return {};
        auto utf8 = input.trimmed().toUtf8();
        if (utf8.size() > max_field_bytes)
            return {};
        return utf8;
    };
    const auto title = field(metadata.title), artist = field(metadata.artist);
    if (!title || !artist)
        return {};
    if (title->isEmpty() && artist->isEmpty())
        return QByteArray{};
    QByteArray frames;
    const auto append_frame = [&](const QByteArrayView id, const QByteArray& text) {
        if (!text.isEmpty()) {
            frames.append(id);
            appendSynchsafeSize(frames, 1 + text.size());
            frames.append("\0\0\x03", 3); // no frame flags, UTF-8 text
            frames.append(text);
        }
    };
    append_frame("TIT2", *title);
    append_frame("TPE1", *artist);
    QByteArray tag("ID3\x04\0\0", 6);
    appendSynchsafeSize(tag, frames.size());
    tag.append(frames);
    return tag;
}
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
DownloadFailure mediaFoundationFailure(const int code) {
    switch (static_cast<MediaFoundationWorkerExit>(code)) {
    case MediaFoundationWorkerExit::InvalidRequest:
        return DownloadFailure::InvalidResult;
    case MediaFoundationWorkerExit::Input:
    case MediaFoundationWorkerExit::Unavailable:
        return DownloadFailure::Unavailable;
    case MediaFoundationWorkerExit::NoAudio:
        return DownloadFailure::NoAudio;
    case MediaFoundationWorkerExit::Unsupported:
        return DownloadFailure::UnsupportedMedia;
    case MediaFoundationWorkerExit::LimitExceeded:
        return DownloadFailure::LimitExceeded;
    case MediaFoundationWorkerExit::Conversion:
    case MediaFoundationWorkerExit::Validation:
    case MediaFoundationWorkerExit::Output:
        return DownloadFailure::Conversion;
    case MediaFoundationWorkerExit::Success:
        break;
    }
    return DownloadFailure::InvalidResult;
}
std::optional<QProcessEnvironment> mediaFoundationEnvironment() {
    std::array<wchar_t, 32'768> directory{};
    const auto size = GetWindowsDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (size == 0 || size >= directory.size())
        return std::nullopt;
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("SystemRoot"), QString::fromWCharArray(directory.data(), size));
    environment.insert(QStringLiteral("PATH"), QString{});
    return environment;
}
} // namespace

WindowsLocalMediaConverter::WindowsLocalMediaConverter(WindowsLocalMediaConverterConfig config)
    : config_(std::move(config)) {}
bool WindowsLocalMediaConverter::start(WindowsDownloadWorkspace& workspace, qint64 now_ms) {
    if (update_.state != LocalMediaConversionState::Idle || now_ms < 0 || config_.total_timeout_ms <= 0 ||
        config_.max_process_memory_bytes == 0 || workspace.error() != DownloadFailure::None ||
        (config_.backend != WindowsLocalMediaBackend::FfmpegLegacy &&
         config_.backend != WindowsLocalMediaBackend::MediaFoundation) ||
        workspace.inputPath().isEmpty() || !workspace.outputPath().isEmpty() || !workspace.outputSink() ||
        !workspace.outputSink()->isWritable() || workspace.outputSink()->size() != 0)
        return false;
    auto tag = publicationTag(config_.publication_metadata);
    if (!tag) {
        update_.state = LocalMediaConversionState::Failed;
        update_.error = DownloadFailure::InvalidResult;
        return false;
    }
    publication_tag_ = std::move(*tag);
    workspace_ = &workspace;
    started_ms_ = previous_ms_ = now_ms;
    return launch(config_.backend == WindowsLocalMediaBackend::MediaFoundation ? Stage::Conversion : Stage::InputProbe,
                  now_ms);
}

bool WindowsLocalMediaConverter::launch(Stage stage, qint64 now_ms) {
    stage_ = stage;
    probe_bytes_.clear();
    WindowsDownloadProcessConfig process_config;
    const bool foundation = config_.backend == WindowsLocalMediaBackend::MediaFoundation;
    const bool probe = stage == Stage::InputProbe;
    const auto path = workspace_->inputPath();
    if (foundation && config_.prepared_launch) {
        if (!config_.prepared_launch->unchanged()) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error = DownloadFailure::InvalidResult;
            return false;
        }
        process_config = config_.prepared_launch->processConfig();
    } else {
        process_config.executable = foundation ? config_.media_foundation_executable
                                               : (probe ? config_.ffprobe_executable : config_.ffmpeg_executable);
        process_config.working_directory = QFileInfo(foundation ? process_config.executable : path).absolutePath();
    }
    process_config.max_memory_bytes = config_.max_process_memory_bytes;
    process_config.total_timeout_ms = config_.total_timeout_ms - (now_ms - started_ms_);
    process_config.max_stdout_bytes = probe ? 16384 : 512ULL * 1024 * 1024;
    process_config.max_stderr_bytes = probe ? 16384 : 4 * 1024 * 1024;
    if (foundation) {
        process_config.read_input = workspace_->readInput();
        const auto bytes =
            process_config.read_input
                ? encodeMediaFoundationWorkerRequest({.mode = MediaFoundationWorkerMode::Convert,
                                                      .input_handle = process_config.read_input->handleValue()})
                : std::vector<std::uint8_t>{};
        if (!config_.prepared_launch)
            process_config.environment = mediaFoundationEnvironment();
        if (bytes.empty() || !process_config.environment || !QFileInfo(process_config.executable).isFile()) {
            update_.state = LocalMediaConversionState::Failed;
            update_.error = bytes.empty() ? DownloadFailure::InvalidResult : DownloadFailure::Unavailable;
            return false;
        }
        process_config.standard_input =
            QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    } else if (probe)
        process_config.arguments = downloadProbeArguments(path);
    else
        process_config.arguments = downloadConversionArguments(path, input_container_);
    if ((!foundation && process_config.arguments.isEmpty()) || !process_.start(process_config, now_ms)) {
        update_.state = LocalMediaConversionState::Failed;
        const auto error = process_.poll(now_ms).error;
        update_.error = foundation && (error == WindowsDownloadProcessError::Start ||
                                       error == WindowsDownloadProcessError::Isolation)
                            ? DownloadFailure::Unavailable
                            : failure(error);
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
        // Keep the encoder blind to application metadata. Write known framing
        // once to the same bounded sink before sealing and decoding the result.
        const auto write_tag = [&] {
            if (publication_tag_.isEmpty())
                return true;
            if (sink->write(publication_tag_) != publication_tag_.size())
                return false;
            publication_tag_.clear();
            return true;
        };
        if (!sink || (!result.stdout_chunk.isEmpty() &&
                      (!write_tag() || sink->write(result.stdout_chunk) != result.stdout_chunk.size()))) {
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
        update_.error =
            config_.backend == WindowsLocalMediaBackend::MediaFoundation
                ? (result.exit_code ? mediaFoundationFailure(*result.exit_code) : DownloadFailure::InvalidResult)
                : (stage_ == Stage::InputProbe ? DownloadFailure::UnsupportedMedia : DownloadFailure::Conversion);
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
                config_.total_timeout_ms - (now_ms - started_ms_), config_.backend, config_.media_foundation_executable,
                config_.prepared_launch});
            stage_ = Stage::Validation;
            update_.state = LocalMediaConversionState::Validating;
            const bool validation_started = config_.backend == WindowsLocalMediaBackend::MediaFoundation
                                                ? validator_->startReadInput(workspace_->readOutput(), now_ms)
                                                : validator_->start(workspace_->outputPath(), now_ms);
            if (!validation_started) {
                update_.state = LocalMediaConversionState::Failed;
                const auto error = validator_->poll(now_ms).error;
                update_.error = error == DownloadFailure::None ? DownloadFailure::InvalidResult : error;
            }
        }
    }
    return update_;
}
} // namespace music_player::m0
