#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace music_player::m0 {
class WindowsDownloadReadInput;
class WindowsDownloadAppContainer;

inline constexpr qint64 download_process_poll_bytes = 64 * 1024;
inline constexpr qsizetype download_process_max_input_bytes = 256 * 1024;
inline constexpr qsizetype download_process_max_environment_units = 32760;
inline constexpr qsizetype download_process_max_path_units = 8192;
inline constexpr quint64 download_process_max_interactive_input_bytes = 64ULL * 1024 * 1024;

struct WindowsDownloadInteractiveInputLimits final {
    qsizetype max_buffered_bytes{download_process_max_input_bytes};
    quint64 max_total_bytes{16ULL * 1024 * 1024};
};
enum class WindowsDownloadInputResult { Accepted, Backpressure, Closed, LimitExceeded };

// Trusted composition only: these are never accepted from an extension/page.
// The executable and working directory must be absolute local filesystem paths.
struct WindowsDownloadProcessConfig final {
    QString executable{};
    QStringList arguments{};
    QString working_directory{};
    // One bounded private request, never a command-line/environment argument.
    // Empty keeps stdin connected to NUL; nonempty is followed by EOF.
    QByteArray standard_input{};
    // nullopt retains the legacy filtered environment. A supplied environment
    // must explicitly contain PATH (may be empty) and nonempty SystemRoot to
    // prevent Qt's fallback to parent values. It is bounded and replaces other
    // inheritance. Trusted composition owns values; FFREPORT/color are removed.
    std::optional<QProcessEnvironment> environment{};
    quint64 max_stdout_bytes{512ULL * 1024 * 1024};
    quint64 max_stderr_bytes{4ULL * 1024 * 1024};
    quint64 max_memory_bytes{512ULL * 1024 * 1024};
    qint64 total_timeout_ms{30 * 60 * 1000};
    // Optional parent-owned file capability. Only this reduced read handle is
    // added to the explicit inheritance list; it is never acquired by path.
    // The process releases its reference after native process/pipe ACK so a
    // delete-pending data file cannot block the workspace's directory cleanup.
    std::shared_ptr<const WindowsDownloadReadInput> read_input{};
    // Trusted identity of a previously provisioned profile, with no granted
    // capabilities. A non-null identity always requires AppContainer launch.
    std::shared_ptr<const WindowsDownloadAppContainer> app_container{};
    // Explicit opt-in survives a failed identity factory: required + null is
    // Isolation before allocating native resources, never a legacy fallback.
    bool require_app_container{false};
    // Trusted opt-in for a parent-controlled protocol. Empty stdin remains
    // open between writes; EOF requires closeStandardInput or native exit.
    // Initial bytes count against both limits. The default one-shot mode and
    // its 256 KiB initial-input ceiling are unchanged.
    std::optional<WindowsDownloadInteractiveInputLimits> interactive_input{};
};

enum class WindowsDownloadProcessState { Idle, Running, Stopping, Exited, Failed, Cancelled };
enum class WindowsDownloadProcessError { None, InvalidConfiguration, Start, Isolation, Io, LimitExceeded, Timeout };

// Notifications witnessed for this process's own Job. False means unobserved,
// not proof that a limit was never exceeded. This is never a native/pipe ACK.
struct WindowsDownloadJobEvidence final {
    bool active_process_limit{false};
    bool process_memory_limit{false};
    bool job_memory_limit{false};
};

struct WindowsDownloadProcessUpdate final {
    WindowsDownloadProcessState state{WindowsDownloadProcessState::Idle};
    WindowsDownloadProcessError error{WindowsDownloadProcessError::None};
    QByteArray stdout_chunk{};
    QByteArray stderr_chunk{};
    std::optional<int> exit_code{};
    WindowsDownloadJobEvidence job_evidence{};
};

// Construct/use/destroy on the download worker with a Qt event loop. poll is
// nonblocking and returns at most 64 KiB across both channels. Native pipes have
// fixed read buffers and backpressure, without QProcess's unbounded buffers.
// A terminal state acknowledges initial process termination and all input/output
// pipe completion. The Job permits only this process, never running descendants.
// Cancellation never means that unacknowledged I/O has already stopped.
// Job Object restrictions bound resource use and lifecycle, not filesystem or
// network permissions. The caller still owns trusted arguments/media policy.
// Optional AppContainer creation is required when configured: verify the token
// and Job before resuming its initial thread or sending a private request.
class WindowsDownloadProcess final {
  public:
    WindowsDownloadProcess();
    ~WindowsDownloadProcess();

    [[nodiscard]] bool start(const WindowsDownloadProcessConfig& config, qint64 now_ms);
    [[nodiscard]] WindowsDownloadProcessUpdate poll(qint64 now_ms);
    // Worker-only, nonblocking, all-or-nothing deep copy; native writing only
    // occurs in poll(). Backpressure changes neither queue nor total quota.
    // A chunk larger than the queue ceiling or cumulative-quota overflow
    // initiates Failed/LimitExceeded; it is not a native/pipe stop ACK.
    [[nodiscard]] WindowsDownloadInputResult enqueueStandardInput(QByteArrayView bytes);
    // Seals admission immediately; already accepted bytes drain before EOF.
    // Idempotent while Running in interactive mode. Other modes/states refuse.
    [[nodiscard]] bool closeStandardInput();
    void cancel();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
