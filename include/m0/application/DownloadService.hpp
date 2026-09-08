#pragma once

#include "m0/ports/DownloadExecution.hpp"

#include <memory>
#include <vector>

namespace music_player::m0 {

enum class DownloadJobState {
    Queued,
    Downloading,
    Converting,
    Publishing,
    Cancelling,
    Completed,
    Cancelled,
    Failed,
    ImportFailed
};

struct DownloadJobSnapshot final {
    std::string job_id{};
    std::string title{};
    DownloadJobState state{DownloadJobState::Queued};
    std::uint64_t completed_units{0};
    std::optional<std::uint64_t> total_units{};
    std::filesystem::path completed_mp3{};
    DownloadFailure error{DownloadFailure::None};
};

struct DownloadServiceLimits final {
    std::size_t max_jobs{128};
    std::size_t max_requests{64};
    std::size_t max_retained_request_bytes{1024 * 1024};
    std::int64_t job_timeout_ms{30 * 60 * 1000};
};

// Single application-thread owner shared by all browser connections. Admission inputs must
// pass the common remote-source policy at the adapter boundary (NativeDownloadCodec today).
// This service also bounds typed inputs, owns them and never calls execution from submit.
// Receipts are retained without eviction for this service lifetime; persistence is separate.
// Dependencies are borrowed: composition must shutdown/drain before destroying them. The
// destructor does not invoke ports or promise worker termination; stopped() observes the drain.
class DownloadService final : public DownloadSubmission {
  public:
    DownloadService(DownloadExecution& execution, DownloadImport& importer, std::string job_id_prefix,
                    DownloadServiceLimits limits = {});
    ~DownloadService();
    DownloadService(const DownloadService&) = delete;
    DownloadService& operator=(const DownloadService&) = delete;

    [[nodiscard]] DownloadSubmissionResult submit(const DownloadRequest& request) override;
    [[nodiscard]] std::vector<DownloadJobSnapshot> snapshot() const;
    void tick(std::int64_t now_ms);
    [[nodiscard]] bool cancel(std::string_view job_id);
    [[nodiscard]] bool retryImport(std::string_view job_id);
    void shutdown();
    [[nodiscard]] bool stopped() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
