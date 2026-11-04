#pragma once

#include "FfmpegDownloadCodec.hpp"
#include "WindowsDownloadProcess.hpp"
#include "WindowsLocalMediaBackend.hpp"

namespace music_player::m0 {
struct WindowsLocalMp3ValidatorConfig final {
    QString ffmpeg_executable{};
    QString ffprobe_executable{};
    quint64 max_process_memory_bytes{512 * 1024 * 1024};
    qint64 total_timeout_ms{30 * 60 * 1000};
    WindowsLocalMediaBackend backend{WindowsLocalMediaBackend::FfmpegLegacy};
    QString media_foundation_executable{};
    // Retains the same authenticated runtime through decode and pipe teardown.
    std::shared_ptr<const WindowsMediaFoundationLaunch> prepared_launch{};
};
enum class LocalMp3ValidationState { Idle, Validating, Stopping, Validated, Failed, Cancelled };
struct LocalMp3ValidationUpdate final {
    LocalMp3ValidationState state{LocalMp3ValidationState::Idle};
    DownloadFailure error{DownloadFailure::None};
};

// Construct/use/destroy on one private worker with a Qt event loop. The caller
// owns trusted local-path/tool selection and retains any required identity pins.
// Probe plus full decode validates content only: no conversion, write, publish,
// import, ownership proof or network authorization. Terminal means owned native
// process and pipes have drained; cancel alone is not that acknowledgement.
class WindowsLocalMp3Validator final {
  public:
    explicit WindowsLocalMp3Validator(WindowsLocalMp3ValidatorConfig config);
    [[nodiscard]] bool start(const QString& local_mp3, qint64 now_ms);
    void cancel();
    [[nodiscard]] LocalMp3ValidationUpdate poll(qint64 now_ms);

  private:
    enum class Stage { Probe, Decode };
    bool launch(Stage stage, qint64 now_ms);
    void stop(DownloadFailure error, bool cancelled = false);
    WindowsLocalMp3ValidatorConfig config_;
    WindowsDownloadProcess process_{};
    QString path_{};
    QByteArray probe_bytes_{};
    DownloadProgressCodec progress_{};
    LocalMp3ValidationUpdate update_{};
    Stage stage_{Stage::Probe};
    qint64 started_ms_{0};
    qint64 previous_ms_{0};
    bool cancel_requested_{false};
};
} // namespace music_player::m0
