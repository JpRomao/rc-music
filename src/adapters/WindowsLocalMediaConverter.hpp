#pragma once

#include "FfmpegDownloadCodec.hpp"
#include "WindowsDownloadProcess.hpp"
#include "WindowsDownloadWorkspace.hpp"
#include "WindowsLocalMediaBackend.hpp"
#include "WindowsLocalMp3Validator.hpp"

namespace music_player::m0 {

struct WindowsLocalMediaConverterConfig final {
    QString ffmpeg_executable{};
    QString ffprobe_executable{};
    quint64 max_process_memory_bytes{512 * 1024 * 1024};
    qint64 total_timeout_ms{30 * 60 * 1000};
    WindowsLocalMediaBackend backend{WindowsLocalMediaBackend::FfmpegLegacy};
    QString media_foundation_executable{};
    // With a prepared lease, executable/CWD/environment come only from it.
    // Raw paths remain solely for the trusted local-fixture composition.
    std::shared_ptr<const WindowsMediaFoundationLaunch> prepared_launch{};
};
enum class LocalMediaConversionState {
    Idle,
    Inspecting,
    Converting,
    Validating,
    Stopping,
    Validated,
    Failed,
    Cancelled
};
struct LocalMediaConversionUpdate final {
    LocalMediaConversionState state{LocalMediaConversionState::Idle};
    DownloadFailure error{DownloadFailure::None};
    quint64 processed_us{0};
    // Source estimate, not a guaranteed upper bound: MP3 padding/resampling may
    // advance processed_us further. Do not forward it as a strict job total.
    std::optional<quint64> duration_us{};
};

// Construct/use/destroy in one worker with event loop. Borrowed workspace must
// outlive the converter. This converts sealed local bytes, never opens URLs or
// publishes/imports a file. Caller supplies reviewed executable paths.
class WindowsLocalMediaConverter final {
  public:
    explicit WindowsLocalMediaConverter(WindowsLocalMediaConverterConfig config);
    bool start(WindowsDownloadWorkspace& workspace, qint64 now_ms);
    void cancel();
    LocalMediaConversionUpdate poll(qint64 now_ms);

  private:
    enum class Stage { InputProbe, Conversion, Validation };
    bool launch(Stage stage, qint64 now_ms);
    void stop(DownloadFailure error, bool cancelled = false);
    WindowsLocalMediaConverterConfig config_;
    WindowsDownloadProcess process_{};
    std::unique_ptr<WindowsLocalMp3Validator> validator_{};
    WindowsDownloadWorkspace* workspace_{nullptr};
    LocalMediaConversionUpdate update_{};
    Stage stage_{Stage::InputProbe};
    DownloadContainer input_container_{DownloadContainer::Mp3};
    QByteArray probe_bytes_{};
    DownloadProgressCodec progress_{};
    qint64 started_ms_{0};
    qint64 previous_ms_{0};
    bool cancel_requested_{false};
};

} // namespace music_player::m0
