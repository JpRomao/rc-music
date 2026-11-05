#pragma once

#include "WindowsDownloadJobSession.hpp"
#include "WindowsDownloadNetworkLaunch.hpp"
#include "WindowsMediaFoundationLaunch.hpp"
#include "WindowsProcessDownloadIo.hpp"

#include <functional>
#include <memory>

namespace music_player::m0 {

// Slow, worker-only seam. A terminal poll acknowledges stopped native work.
// A false/throwing start is NOT an acknowledgement: cancel + poll must drain it.
// A factory creates only inert objects: no native I/O, process or media file may
// become active before task.start. A failure/null must retain no resources.
// No native work may escape the returned task's ownership.
class WindowsDownloadTask {
  public:
    virtual ~WindowsDownloadTask() = default;
    [[nodiscard]] virtual bool start(const std::string& job_id, const DownloadItem& item, qint64 now_ms) = 0;
    // Trusted resolver handoff, never selected by a field from a page. Legacy
    // tasks refuse this capability unless they implement the scoped session.
    [[nodiscard]] virtual bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability>, qint64,
                                                    qint64) {
        return false;
    }
    virtual void cancel() = 0;
    [[nodiscard]] virtual DownloadExecutionUpdate poll(qint64 now_ms) = 0;
    [[nodiscard]] virtual std::filesystem::path retainedPublicationPath() const { return {}; }
};

// Trusted worker-only seam. The optional preparer must return an authenticated
// capability or no escaped resources; it is never a page/plugin callback.
// Construction is inert. Preparation happens in start, session launch in the
// first uncancelled poll; the original clock includes preparation time.
using WindowsDownloadRuntimePreparer =
    std::function<std::shared_ptr<const WindowsDownloadNetworkLaunch>(const WindowsDownloadNetworkLaunchConfig&)>;
using WindowsMediaFoundationRuntimePreparer =
    std::function<std::shared_ptr<const WindowsMediaFoundationLaunch>(const WindowsMediaFoundationLaunchConfig&)>;
[[nodiscard]] std::unique_ptr<WindowsDownloadTask>
makeWindowsPreparedDownloadTask(WindowsDownloadNetworkLaunchConfig launch, WindowsDownloadJobSessionConfig job,
                                WindowsDownloadRuntimePreparer prepare = {});
[[nodiscard]] std::unique_ptr<WindowsDownloadTask>
makeWindowsPreparedDownloadTask(WindowsDownloadNetworkLaunchConfig network, WindowsMediaFoundationLaunchConfig media,
                                WindowsDownloadJobSessionConfig job,
                                WindowsDownloadRuntimePreparer network_prepare = {},
                                WindowsMediaFoundationRuntimePreparer media_prepare = {});
// Owns the synchronous journal on the task's worker, after inert construction.
// A borrowed job.ownership_store is rejected; the private existing directory is
// validated before preparing either runtime. The store outlives the session.
[[nodiscard]] std::unique_ptr<WindowsDownloadTask>
makeWindowsOwnedPreparedDownloadTask(WindowsDownloadNetworkLaunchConfig network,
                                     WindowsMediaFoundationLaunchConfig media, WindowsDownloadJobSessionConfig job,
                                     QString journal_directory, WindowsDownloadRuntimePreparer network_prepare = {},
                                     WindowsMediaFoundationRuntimePreparer media_prepare = {});

// Prompt UI port with one pending task and one coalesced snapshot. Factory,
// task methods, filesystem/native I/O and destruction run in a private Qt loop.
// Only a matching terminal poll releases admission for the next job.
// shutdown closes admission promptly; stopped additionally acknowledges thread
// teardown. The owner must observe stopped before destruction on the UI thread.
// The destructor's off-UI fallback joins: it is not a prompt shutdown substitute.
// Trusted configuration only. Reviewed distribution profile, hostile-media
// converter isolation, recovery journal and player composition remain gates.
class WindowsDownloadExecution final : public DownloadExecution {
  public:
    using TaskFactory = std::function<std::unique_ptr<WindowsDownloadTask>()>;
    explicit WindowsDownloadExecution(TaskFactory factory);
    WindowsDownloadExecution(WindowsProcessDownloadIoConfig io, WindowsDownloadJobSessionConfig job);
    WindowsDownloadExecution(WindowsDownloadNetworkLaunchConfig launch, WindowsDownloadJobSessionConfig job);
    WindowsDownloadExecution(WindowsDownloadNetworkLaunchConfig network, WindowsMediaFoundationLaunchConfig media,
                             WindowsDownloadJobSessionConfig job);
    ~WindowsDownloadExecution() override;
    WindowsDownloadExecution(const WindowsDownloadExecution&) = delete;
    WindowsDownloadExecution& operator=(const WindowsDownloadExecution&) = delete;

    [[nodiscard]] bool start(const std::string& job_id, const DownloadItem& item) override;
    void cancel(std::string_view job_id) override;
    [[nodiscard]] DownloadExecutionUpdate poll(std::string_view job_id) override;
    void shutdown();
    [[nodiscard]] bool stopped() const;

    // Current job's committed final, including Published racing cancellation or
    // failed post-rename cleanup. Bounded private recovery evidence, NOT import
    // authority or durable ownership. Cleared only by the next successful start.
    [[nodiscard]] std::filesystem::path retainedPublicationPath(std::string_view job_id) const;

  private:
    friend class YoutubeDownloadExecutionFactory;
    WindowsDownloadExecution(TaskFactory factory, std::function<bool(const DownloadItem&)> admission);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
