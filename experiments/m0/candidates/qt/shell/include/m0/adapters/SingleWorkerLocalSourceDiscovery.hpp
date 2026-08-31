#pragma once

#include "m0/ports/LocalSourceDiscovery.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace music_player::m0 {

struct SingleWorkerLocalSourceDiscoveryOptions final {
    std::size_t batch_size{32};
    std::size_t progress_interval{128};
    std::size_t maximum_depth{64};
    // Test hook invoked by the worker before inspecting a source; it must be thread-safe and non-throwing.
    std::function<void()> before_inspect{};
    // Test seam overriding the platform I/O interruption. Production uses CancelSynchronousIo on Windows.
    std::function<void(std::thread::native_handle_type)> cancel_synchronous_io{};
    // Test seam invoked after a Windows directory handle is validated and before its binding is rechecked.
    std::function<void(const std::filesystem::path&)> after_directory_handle_validated{};
};

class SingleWorkerLocalSourceDiscovery final : public LocalSourceDiscovery {
  public:
    explicit SingleWorkerLocalSourceDiscovery(SingleWorkerLocalSourceDiscoveryOptions options = {});
    ~SingleWorkerLocalSourceDiscovery() override;

    SingleWorkerLocalSourceDiscovery(const SingleWorkerLocalSourceDiscovery&) = delete;
    SingleWorkerLocalSourceDiscovery& operator=(const SingleWorkerLocalSourceDiscovery&) = delete;
    SingleWorkerLocalSourceDiscovery(SingleWorkerLocalSourceDiscovery&&) = delete;
    SingleWorkerLocalSourceDiscovery& operator=(SingleWorkerLocalSourceDiscovery&&) = delete;

    DiscoveryCommandResult start(LocalSourceDiscoveryRequest request) override;
    // Active cancellation is asynchronous; callers must keep polling until Cancelled is observed.
    DiscoveryCommandResult cancel(DiscoveryGeneration generation) override;
    [[nodiscard]] DiscoveryPollResult poll(DiscoveryGeneration generation) override;
    DiscoveryCommandResult shutdown() override;

  private:
    enum class CurrentState {
        Idle,
        Pending,
        Active,
        Cancelling,
        Completed,
        Cancelled,
    };

    void runWorker();
    [[nodiscard]] bool runDiscovery(const LocalSourceDiscoveryRequest& request, LocalSourceDiscoverySummary& summary);
    [[nodiscard]] bool publishBatch(LocalSourceDiscoveryBatch batch);
    [[nodiscard]] bool isCurrent(DiscoveryGeneration generation) const;
    void interruptWorkerIo() noexcept;

    SingleWorkerLocalSourceDiscoveryOptions options_;
    mutable std::mutex mutex_{};
    std::condition_variable changed_{};
    std::condition_variable worker_exit_changed_{};
    std::mutex shutdown_mutex_{};
    DiscoveryGeneration highest_generation_{};
    CurrentState current_state_{CurrentState::Idle};
    std::optional<LocalSourceDiscoveryRequest> pending_{};
    std::optional<LocalSourceDiscoveryBatch> mailbox_{};
    std::optional<LocalSourceDiscoverySummary> completion_{};
    bool stopping_{false};
    bool worker_exited_{false};
    std::jthread worker_{};
};

} // namespace music_player::m0
