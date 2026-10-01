#pragma once

#include "DirectDownloadSession.hpp"
#include "WindowsLocalMediaConverter.hpp"

#include <memory>

namespace music_player::m0 {

struct WindowsDownloadJobSessionConfig final {
    WindowsDownloadWorkspaceConfig workspace{};
    WindowsLocalMediaConverterConfig converter{};
    DirectDownloadLimits network{};
    qint64 total_timeout_ms{30 * 60 * 1000};
};

// One-shot composition owned by a worker with a Qt event loop, NOT the prompt
// DownloadExecution UI port. File writes/flush/rename may block. The observed
// clock is checked between steps, not a hard deadline inside native calls.
// The borrowed IO and trusted parents/tools must outlive this object. Qt DNS
// cancellation alone does not acknowledge termination of native resolver work;
// a future subprocess owner must establish that before exposing a terminal DTO.
class WindowsDownloadJobSession final {
  public:
    WindowsDownloadJobSession(DirectDownloadIo& io, WindowsDownloadJobSessionConfig config);
    ~WindowsDownloadJobSession();
    WindowsDownloadJobSession(const WindowsDownloadJobSession&) = delete;
    WindowsDownloadJobSession& operator=(const WindowsDownloadJobSession&) = delete;

    [[nodiscard]] bool start(const std::string& job_id, const DownloadItem& item, qint64 now_ms);
    void cancel();
    [[nodiscard]] DownloadExecutionUpdate poll(qint64 now_ms);

    // Trusted recovery only: a committed final whose subsequent cleanup failed.
    // Not a Published acknowledgement and never an automatic import input.
    [[nodiscard]] QString retainedPublicationPath() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
