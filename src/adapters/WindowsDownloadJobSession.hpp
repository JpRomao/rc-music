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
    DownloadOwnershipStore* ownership_store{nullptr}; // Borrowed, trusted, worker-only; nullptr is volatile legacy.
};

// One-shot composition owned by a worker with a Qt event loop, NOT the prompt
// DownloadExecution UI port. File writes/flush/rename may block. The observed
// clock is checked between steps, not a hard deadline inside native calls.
// Borrowed IO/store and trusted parents/tools must outlive this object. Every
// completion/stop drains IO's explicit native acknowledgement before sealing,
// conversion, cleanup or terminal results. QtDownloadIo alone cannot prove this;
// use an IO adapter owning the separate diskless network process and its pipes.
// The owner MUST poll to terminal before destruction. RAII best-effort cleanup
// is not a stopping acknowledgement and cannot substitute for that lifecycle.
class WindowsDownloadJobSession final {
  public:
    WindowsDownloadJobSession(DirectDownloadIo& io, WindowsDownloadJobSessionConfig config);
    ~WindowsDownloadJobSession();
    WindowsDownloadJobSession(const WindowsDownloadJobSession&) = delete;
    WindowsDownloadJobSession& operator=(const WindowsDownloadJobSession&) = delete;

    [[nodiscard]] bool start(const std::string& job_id, const DownloadItem& item, qint64 now_ms);
    // Separate typed route: exact job/source identity belongs to the resolver
    // capability. Wall time is explicit, never read implicitly by this class.
    [[nodiscard]] bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> capability,
                                            qint64 now_ms, qint64 trusted_epoch_seconds);
    void cancel();
    [[nodiscard]] DownloadExecutionUpdate poll(qint64 now_ms);

    // Trusted recovery only: a proven final despite later journal/cleanup failure.
    // Not a Published acknowledgement and never an automatic import input.
    [[nodiscard]] QString retainedPublicationPath() const;

  private:
    bool startImpl(const std::string& job_id, const DownloadItem& item, qint64 now_ms,
                   std::shared_ptr<const ResolvedYoutubeAudioCapability> capability, qint64 trusted_epoch_seconds);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
