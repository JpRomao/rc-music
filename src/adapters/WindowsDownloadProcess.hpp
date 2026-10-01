#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace music_player::m0 {

inline constexpr qint64 download_process_poll_bytes = 64 * 1024;

// Trusted composition only: these are never accepted from an extension/page.
// The executable and working directory must be absolute local filesystem paths.
struct WindowsDownloadProcessConfig final {
    QString executable{};
    QStringList arguments{};
    QString working_directory{};
    quint64 max_stdout_bytes{512ULL * 1024 * 1024};
    quint64 max_stderr_bytes{4ULL * 1024 * 1024};
    quint64 max_memory_bytes{512ULL * 1024 * 1024};
    qint64 total_timeout_ms{30 * 60 * 1000};
};

enum class WindowsDownloadProcessState { Idle, Running, Stopping, Exited, Failed, Cancelled };
enum class WindowsDownloadProcessError { None, InvalidConfiguration, Start, Isolation, Io, LimitExceeded, Timeout };

struct WindowsDownloadProcessUpdate final {
    WindowsDownloadProcessState state{WindowsDownloadProcessState::Idle};
    WindowsDownloadProcessError error{WindowsDownloadProcessError::None};
    QByteArray stdout_chunk{};
    QByteArray stderr_chunk{};
    std::optional<int> exit_code{};
};

// Construct/use/destroy on the download worker with a Qt event loop. poll is
// nonblocking and returns at most 64 KiB across both channels. Native pipes have
// fixed read buffers and backpressure, without QProcess's unbounded buffers.
// A terminal state acknowledges both process termination and pipe completion.
// Cancellation never means that unacknowledged I/O has already stopped.
// Job Object restrictions bound resource use and lifecycle, not filesystem or
// network permissions. The caller still owns trusted arguments/media policy.
class WindowsDownloadProcess final {
  public:
    WindowsDownloadProcess();
    ~WindowsDownloadProcess();

    [[nodiscard]] bool start(const WindowsDownloadProcessConfig& config, qint64 now_ms);
    [[nodiscard]] WindowsDownloadProcessUpdate poll(qint64 now_ms);
    void cancel();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
