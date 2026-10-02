#pragma once

#include "m0/application/DownloadCheckpoint.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/DownloadRequestIdentity.hpp"

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace music_player::m0 {

struct DownloadServiceLimits final {
    std::size_t max_jobs{128};
    std::size_t max_requests{64};
    std::size_t max_retained_request_bytes{1024 * 1024};
    std::int64_t job_timeout_ms{30 * 60 * 1000};
};

enum class DownloadRecoveryStatus { NotRequested, Ready, InvalidHistory, NamespaceConflict, ReceiptConflict };
enum class DownloadRecoveryDisposition { Imported, Unconfirmed, Interrupted, Unchanged };

struct DownloadRecoveryItem final {
    std::string job_id{};
    DownloadRecoveryDisposition disposition{DownloadRecoveryDisposition::Unchanged};
    bool operator==(const DownloadRecoveryItem&) const = default;
};

struct DownloadRecoveryReport final {
    DownloadRecoveryStatus status{DownloadRecoveryStatus::NotRequested};
    std::vector<DownloadRecoveryItem> items{};
    std::vector<std::string> session_only_job_ids{};
    bool operator==(const DownloadRecoveryReport&) const = default;
};

// Single application-thread owner shared by all browser connections. Admission inputs must
// pass the common remote-source policy at the adapter boundary (NativeDownloadCodec today).
// This service also bounds typed inputs, owns them and never calls execution from submit.
// Receipts are retained without eviction for this service lifetime; persistence is separate.
// Dependencies are borrowed: composition must shutdown/drain before destroying them. The
// destructor does not invoke ports or promise worker termination; stopped() observes
// execution and pending handoff statuses, not separate session/autosave worker teardown.
class DownloadService final : public DownloadSubmission {
  public:
    DownloadService(DownloadExecution& execution, DownloadImport& importer, std::string job_id_prefix,
                    DownloadServiceLimits limits = {}, DownloadRequestIdentity* identity = nullptr);
    ~DownloadService();
    DownloadService(const DownloadService&) = delete;
    DownloadService& operator=(const DownloadService&) = delete;

    [[nodiscard]] DownloadSubmissionResult submit(const DownloadRequest& request) override;
    // Trusted durable-admission owner only. New jobs remain FIFO-staged until
    // confirmation of a matching Saved checkpoint; existing active work drains.
    [[nodiscard]] DownloadSubmissionResult submitStaged(const DownloadRequest& request);
    [[nodiscard]] bool confirmStaged(std::string_view request_id);
    [[nodiscard]] std::vector<DownloadJobSnapshot> snapshot() const;
    // These methods do not perform I/O. Restore is only for a pristine owner with
    // a fresh prefix and identity port. It never starts work or trusts stored paths.
    [[nodiscard]] std::optional<DownloadCheckpoint> checkpoint();
    [[nodiscard]] bool restore(const DownloadCheckpoint& checkpoint);
    // Trusted composition supplies only receipts proven by Loaded or matching
    // Saved session data. Reconciliation recognises history, never imports or
    // validates files; recovered candidate paths remain private evidence.
    [[nodiscard]] bool restore(const DownloadCheckpoint& checkpoint,
                               std::span<const DownloadImportReceipt> confirmed_imports);
    [[nodiscard]] DownloadRecoveryReport recoveryReport() const;
    // Trusted composition may suppress a new native start after a handoff
    // callback requests owner shutdown. Existing execution/import drains remain
    // observed. The optional predicate is bounded and cannot reenter mutations;
    // false/exception conservatively leaves the queued job unchanged.
    void tick(std::int64_t now_ms, const std::function<bool()>& allow_start = {});
    [[nodiscard]] bool cancel(std::string_view job_id);
    [[nodiscard]] bool retryImport(std::string_view job_id);
    void shutdown();
    [[nodiscard]] bool stopped() const;

  private:
    [[nodiscard]] DownloadSubmissionResult submitImpl(const DownloadRequest& request, bool staged);
    [[nodiscard]] bool restoreImpl(const DownloadCheckpoint& checkpoint,
                                   std::span<const DownloadImportReceipt> confirmed_imports, bool reconcile);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
