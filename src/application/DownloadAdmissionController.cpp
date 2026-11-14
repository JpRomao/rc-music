#include "m0/application/DownloadAdmissionController.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr auto maximum_confirmed_requests =
    download_checkpoint_max_receipts + download_checkpoint_max_archived_receipts;

std::string canonicalId(const std::string_view value) {
    // Only bounded UUID-shaped input is copied, including observer queries.
    if (value.size() != 36)
        return {};
    auto result = std::string{value};
    for (auto& c : result)
        if (c >= 'A' && c <= 'F')
            c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
DownloadAdmissionResult rejected(const DownloadSubmissionError error = DownloadSubmissionError::NotAvailable) {
    return {.result = {.error = error}};
}
bool validSaveStatus(const DownloadCheckpointSaveStatus status) {
    return status == DownloadCheckpointSaveStatus::Saved || status == DownloadCheckpointSaveStatus::Failed ||
           status == DownloadCheckpointSaveStatus::BlockedByExistingData;
}
} // namespace

struct DownloadAdmissionController::Impl {
    struct Receipt {
        std::string id;
        DownloadSubmissionResult result;
    };
    enum class Purpose { Load, Admission, Resume, Archive, Final };
    struct Operation {
        std::uint64_t id;
        Purpose purpose;
        // An uncertain port call may still own native work. Observe only to
        // release that slot; a later Saved cannot repair its failed protocol.
        bool drain_only{false};
    };
    DownloadService& service;
    DownloadCheckpointIo& io;
    const DownloadFinalSavePolicy final_save_policy;
    const DownloadQueueClosePolicy queue_close_policy;
    std::vector<Receipt> confirmed;
    std::optional<Receipt> pending;
    std::vector<std::string> retried_jobs;
    std::optional<DownloadCheckpoint> archive_checkpoint;
    std::optional<Operation> operation;
    // At most one explicit probe may follow an uncertain operation. If that
    // probe also throws, either ID could own the single native slot. Retain both
    // for drain-only polling and refuse further offers until consumption/exit.
    std::optional<Operation> uncertain_offer;
    std::optional<DownloadCheckpointLoadStatus> load_status;
    std::optional<DownloadCheckpointSaveStatus> save_status;
    DownloadAdmissionControllerState state{DownloadAdmissionControllerState::NotStarted};
    std::uint64_t next_id{0};
    bool operating{false};
    bool healthy_startup{false};
    bool closing{false};
    bool io_shutdown{false};
    bool final_attempted{false};
    bool reconcile_imports{false};
    bool import_history_healthy{false};
    std::vector<DownloadImportReceipt> import_receipts{};
    std::vector<std::string> retired_import_job_ids{};
    std::optional<DownloadCheckpoint> namespace_checkpoint{};
    DownloadRecoveryStatus blocked_recovery_status{DownloadRecoveryStatus::NotRequested};

    Impl(DownloadService& owner, DownloadCheckpointIo& storage, const DownloadFinalSavePolicy policy,
         const DownloadQueueClosePolicy queue_policy)
        : service(owner), io(storage), final_save_policy(policy), queue_close_policy(queue_policy) {
        confirmed.reserve(maximum_confirmed_requests);
    }
    static std::vector<Receipt> acknowledgements(const DownloadCheckpoint& checkpoint) {
        std::vector<Receipt> result;
        result.reserve(maximum_confirmed_requests);
        const auto append = [&](const auto& receipts) {
            for (const auto& receipt : receipts)
                result.push_back(
                    {receipt.request_id, {.error = DownloadSubmissionError::None, .job_ids = receipt.job_ids}});
        };
        append(checkpoint.receipts);
        append(checkpoint.archived_receipts);
        return result;
    }
    bool containsRetiredImports(const DownloadCheckpoint& checkpoint) const {
        if (retired_import_job_ids.empty())
            return true;
        std::unordered_set<std::string_view> missing(retired_import_job_ids.begin(), retired_import_job_ids.end());
        for (const auto& receipt : checkpoint.archived_receipts)
            for (const auto& id : receipt.job_ids)
                missing.erase(id);
        return missing.empty();
    }
    bool holdsFinalFailure() const {
        return final_save_policy == DownloadFinalSavePolicy::HoldForDecision && closing && healthy_startup &&
               !io_shutdown && service.stopped();
    }
    void closeIo() noexcept {
        if (io_shutdown)
            return;
        io_shutdown = true;
        try {
            io.shutdown();
        } catch (...) {
            save_status = DownloadCheckpointSaveStatus::Failed;
        }
    }
    void applyShutdown() noexcept {
        if (!closing || state == DownloadAdmissionControllerState::Stopped)
            return;
        if (state != DownloadAdmissionControllerState::FinalSaveFailed)
            state = DownloadAdmissionControllerState::Stopping;
        // Trusted job control can request owner shutdown from inside a service
        // port callback. The service refuses that reentry; retry its idempotent
        // shutdown on the next outer tick rather than remembering a false commit.
        service.shutdown(queue_close_policy == DownloadQueueClosePolicy::Preserve);
        // A startup load cannot authorise a final empty replacement after shutdown.
        // Closing IO is prompt; the worker still owns its active native drain.
        if (!healthy_startup && (!operation || operation->purpose == Purpose::Load))
            closeIo();
    }
    class Guard final {
      public:
        explicit Guard(Impl& owner) : owner_(owner) { owner_.operating = true; }
        ~Guard() {
            owner_.applyShutdown();
            owner_.operating = false;
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

      private:
        Impl& owner_;
    };
    std::optional<std::uint64_t> newOperationId() {
        if (next_id == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        return ++next_id;
    }
    bool load() {
        const auto id = newOperationId();
        try {
            if (id && io.startLoad(*id) == DownloadCheckpointIoCommand::Accepted) {
                operation = Operation{*id, Purpose::Load};
                state = DownloadAdmissionControllerState::Loading;
                return true;
            }
        } catch (...) {
        }
        load_status = DownloadCheckpointLoadStatus::Unreadable;
        state = DownloadAdmissionControllerState::Blocked;
        return false;
    }
    void failedSave(DownloadCheckpointSaveStatus status) {
        save_status = validSaveStatus(status) ? status : DownloadCheckpointSaveStatus::Failed;
        if (closing) {
            state = holdsFinalFailure() ? DownloadAdmissionControllerState::FinalSaveFailed
                                        : DownloadAdmissionControllerState::Stopping;
            return;
        }
        state = archive_checkpoint ? DownloadAdmissionControllerState::ArchiveFailed
                                   : DownloadAdmissionControllerState::AdmissionFailed;
    }
    bool save(const Purpose purpose) {
        std::optional<std::uint64_t> offered_id;
        try {
            const bool archiving = purpose == Purpose::Archive;
            const auto current = archiving ? std::nullopt : service.checkpoint();
            const auto& checkpoint = archiving ? archive_checkpoint : current;
            const auto id = newOperationId();
            if (checkpoint && id) {
                offered_id = id;
                if (io.startSave(*id, *checkpoint) == DownloadCheckpointIoCommand::Accepted) {
                    // Accepted is the port's slot acknowledgement, not Saved.
                    // It can supersede an uncertain prior call only because the
                    // single-slot contract refuses outstanding native work.
                    operation = Operation{*id, purpose};
                    uncertain_offer.reset();
                    state = closing     ? DownloadAdmissionControllerState::Stopping
                            : archiving ? DownloadAdmissionControllerState::ArchivePending
                                        : DownloadAdmissionControllerState::AdmissionPending;
                    return true;
                }
            }
        } catch (...) {
            if (offered_id &&
                (final_save_policy == DownloadFinalSavePolicy::HoldForDecision || purpose == Purpose::Archive)) {
                if (!operation)
                    operation = Operation{*offered_id, purpose, true};
                else if (operation->drain_only)
                    uncertain_offer = Operation{*offered_id, purpose, true};
            }
        }
        failedSave(DownloadCheckpointSaveStatus::Failed);
        return false;
    }
    void finishRestore() {
        healthy_startup = true;
        state = DownloadAdmissionControllerState::Ready;
        if (load_status == DownloadCheckpointLoadStatus::Loaded)
            service.retireArchivedImportReceipts();
        if (!closing && service.hasUnconfirmedRestoredQueue())
            (void)save(Purpose::Resume);
    }
    void observeLoad(DownloadCheckpointLoadResult loaded) {
        load_status = loaded.status;
        namespace_checkpoint.reset();
        blocked_recovery_status = DownloadRecoveryStatus::NotRequested;
        if (closing)
            return;
        try {
            DownloadCheckpoint empty;
            const auto* checkpoint = &empty;
            if (loaded.status == DownloadCheckpointLoadStatus::Loaded) {
                if (!loaded.checkpoint || !validDownloadCheckpoint(*loaded.checkpoint)) {
                    load_status = DownloadCheckpointLoadStatus::Invalid;
                    state = DownloadAdmissionControllerState::Blocked;
                    return;
                }
                checkpoint = &*loaded.checkpoint;
            } else if (loaded.checkpoint || loaded.status != DownloadCheckpointLoadStatus::Missing) {
                if (loaded.checkpoint || (loaded.status != DownloadCheckpointLoadStatus::Invalid &&
                                          loaded.status != DownloadCheckpointLoadStatus::Unreadable &&
                                          loaded.status != DownloadCheckpointLoadStatus::FutureVersion))
                    load_status = DownloadCheckpointLoadStatus::Unreadable;
                state = DownloadAdmissionControllerState::Blocked;
                return;
            }
            if (reconcile_imports && !import_history_healthy) {
                state = DownloadAdmissionControllerState::Blocked;
                return;
            }
            if (!containsRetiredImports(*checkpoint)) {
                blocked_recovery_status = DownloadRecoveryStatus::ReceiptConflict;
                state = DownloadAdmissionControllerState::Blocked;
                return;
            }
            // Allocate the complete acknowledgement index before the atomic restore.
            auto receipts = acknowledgements(*checkpoint);
            const bool restored =
                reconcile_imports ? service.restore(*checkpoint, import_receipts) : service.restore(*checkpoint);
            if (!restored) {
                blocked_recovery_status = service.recoveryReport().status;
                if (blocked_recovery_status == DownloadRecoveryStatus::NamespaceConflict)
                    namespace_checkpoint = *checkpoint;
                state = DownloadAdmissionControllerState::Blocked;
                return;
            }
            confirmed.swap(receipts);
            finishRestore();
        } catch (...) {
            namespace_checkpoint.reset();
            blocked_recovery_status = DownloadRecoveryStatus::NotRequested;
            load_status = DownloadCheckpointLoadStatus::Unreadable;
            state = DownloadAdmissionControllerState::Blocked;
        }
    }
    void observeOperation(const Operation expected) {
        try {
            auto result = io.poll(expected.id);
            if (!result)
                return;
            operation.reset();
            uncertain_offer.reset();
            if (expected.drain_only)
                return;
            const auto kind = expected.purpose == Purpose::Load ? DownloadCheckpointOperation::Load
                                                                : DownloadCheckpointOperation::Save;
            if (result->operation_id != expected.id || result->operation != kind) {
                if (expected.purpose == Purpose::Load)
                    observeLoad({.status = DownloadCheckpointLoadStatus::Unreadable});
                else
                    failedSave(DownloadCheckpointSaveStatus::Failed);
                return;
            }
            if (expected.purpose == Purpose::Load) {
                observeLoad(std::move(result->load));
                return;
            }
            if (result->save_status != DownloadCheckpointSaveStatus::Saved) {
                failedSave(result->save_status);
                return;
            }
            if (expected.purpose == Purpose::Admission) {
                if (!pending || confirmed.size() >= maximum_confirmed_requests || !service.confirmStaged(pending->id)) {
                    failedSave(DownloadCheckpointSaveStatus::Failed);
                    return;
                }
                // Reserved capacity and noexcept moves precede any observer's ACK.
                confirmed.push_back(std::move(*pending));
                pending.reset();
            } else if (expected.purpose == Purpose::Resume) {
                service.confirmRestoredQueue();
            } else if (expected.purpose == Purpose::Archive) {
                if (!archive_checkpoint || !service.confirmSettledArchive()) {
                    failedSave(DownloadCheckpointSaveStatus::Failed);
                    return;
                }
                archive_checkpoint.reset();
                if (closing)
                    final_attempted = true;
            }
            save_status = DownloadCheckpointSaveStatus::Saved;
            state = closing ? DownloadAdmissionControllerState::Stopping : DownloadAdmissionControllerState::Ready;
            if (expected.purpose == Purpose::Archive)
                service.retireArchivedImportReceipts();
        } catch (...) {
            // A drain-only query cannot restore protocol confidence, including
            // after an explicit lossy close. Keep both possible IDs bounded.
            if (expected.drain_only)
                return;
            if (expected.purpose != Purpose::Load &&
                (final_save_policy == DownloadFinalSavePolicy::HoldForDecision || expected.purpose == Purpose::Archive))
                operation = Operation{expected.id, expected.purpose, true};
            else
                operation.reset();
            if (expected.purpose == Purpose::Load)
                observeLoad({.status = DownloadCheckpointLoadStatus::Unreadable});
            else
                failedSave(DownloadCheckpointSaveStatus::Failed);
        }
    }
    void observeOperation() {
        if (operation)
            observeOperation(*operation);
        if (uncertain_offer)
            observeOperation(*uncertain_offer);
    }
    void drain() {
        if (!closing || !service.stopped())
            return;
        if (holdsFinalFailure() && save_status && *save_status != DownloadCheckpointSaveStatus::Saved &&
            (!operation || operation->drain_only)) {
            state = DownloadAdmissionControllerState::FinalSaveFailed;
            return;
        }
        if (!io_shutdown && !operation) {
            if (!final_attempted && healthy_startup && !pending &&
                (!save_status || *save_status == DownloadCheckpointSaveStatus::Saved)) {
                final_attempted = true;
                if (save(Purpose::Final))
                    return;
            }
            if (holdsFinalFailure() && save_status && *save_status != DownloadCheckpointSaveStatus::Saved) {
                state = DownloadAdmissionControllerState::FinalSaveFailed;
                return;
            }
            closeIo();
        }
        if (io_shutdown) {
            try {
                if (io.stopped())
                    state = DownloadAdmissionControllerState::Stopped;
            } catch (...) {
            }
        }
    }
};

DownloadAdmissionController::DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io,
                                                         DownloadFinalSavePolicy final_save_policy,
                                                         DownloadQueueClosePolicy queue_close_policy)
    : impl_(std::make_unique<Impl>(service, io, final_save_policy, queue_close_policy)) {}
DownloadAdmissionController::DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io,
                                                         const SessionLoadResult& initial_session,
                                                         DownloadFinalSavePolicy final_save_policy,
                                                         DownloadQueueClosePolicy queue_close_policy)
    : DownloadAdmissionController(service, io, final_save_policy, queue_close_policy) {
    auto& owner = *impl_;
    owner.reconcile_imports = true;
    try {
        const bool healthy =
            initial_session.status == SessionLoadStatus::Loaded
                ? isValidSessionSnapshot(initial_session.snapshot)
                : initial_session.status == SessionLoadStatus::Missing && initial_session.snapshot == SessionSnapshot{};
        if (healthy) {
            auto receipts = initial_session.snapshot.download_import_receipts;
            auto retired = initial_session.snapshot.retired_download_import_job_ids;
            owner.import_receipts = std::move(receipts);
            owner.retired_import_job_ids = std::move(retired);
            owner.import_history_healthy = true;
        }
    } catch (...) {
        // Allocation/validation failure leaves no authoritative partial ledger.
    }
}
DownloadAdmissionController::~DownloadAdmissionController() = default;
bool DownloadAdmissionController::start() {
    auto& owner = *impl_;
    if (owner.operating || owner.closing || owner.state != DownloadAdmissionControllerState::NotStarted)
        return false;
    const Impl::Guard guard(owner);
    return owner.load();
}
void DownloadAdmissionController::tick(const std::int64_t now_ms, const bool allow_start) {
    auto& owner = *impl_;
    if (owner.operating || owner.state == DownloadAdmissionControllerState::Stopped)
        return;
    const Impl::Guard guard(owner);
    owner.applyShutdown();
    owner.observeOperation();
    // Observing a port may request exit through a nested callback. Close the
    // service before any newly confirmed staged job is allowed to execute.
    owner.applyShutdown();
    // Active work keeps observing progress, cancellation and deadlines while
    // storage is pending; the staged FIFO barrier prevents new work starting.
    // Import acknowledgement polling may request shutdown from a trusted
    // callback inside the service port call. Recheck ingress immediately before
    // any successor is started, while execution/import draining stays enabled.
    owner.service.tick(now_ms, [&owner, allow_start] { return !owner.closing && allow_start; });
    owner.drain();
}
DownloadAdmissionResult DownloadAdmissionController::begin(const DownloadRequest& request) {
    auto& owner = *impl_;
    if (owner.operating)
        return rejected(DownloadSubmissionError::Busy);
    if (owner.closing || !owner.healthy_startup)
        return rejected();
    const Impl::Guard guard(owner);
    try {
        auto id = canonicalId(request.request_id);
        const auto known = std::find_if(owner.confirmed.begin(), owner.confirmed.end(),
                                        [&](const Impl::Receipt& receipt) { return receipt.id == id; });
        if (known != owner.confirmed.end())
            return {.result = owner.service.submitStaged(request)};
        if (owner.archive_checkpoint || owner.service.hasUnconfirmedRestoredQueue())
            return rejected(DownloadSubmissionError::Busy);
        if (owner.pending) {
            if (owner.pending->id != id)
                return rejected(DownloadSubmissionError::Busy);
            const auto validation = owner.service.submitStaged(request);
            if (validation.error != DownloadSubmissionError::None)
                return rejected(validation.error);
            return owner.state == DownloadAdmissionControllerState::AdmissionPending
                       ? DownloadAdmissionResult{.state = DownloadAdmissionState::Pending}
                       : rejected();
        }
        auto result = owner.service.submitStaged(request);
        if (result.error != DownloadSubmissionError::None)
            return {.result = std::move(result)};
        // Only moves follow service commit, before the potentially failing snapshot.
        owner.pending.emplace(Impl::Receipt{std::move(id), std::move(result)});
        if (owner.save(Impl::Purpose::Admission))
            return {.state = DownloadAdmissionState::Pending};
    } catch (...) {
        if (owner.pending)
            owner.failedSave(DownloadCheckpointSaveStatus::Failed);
    }
    return rejected();
}
std::optional<DownloadSubmissionResult> DownloadAdmissionController::poll(const std::string_view request_id) {
    const auto& owner = *impl_;
    if (owner.operating)
        return std::nullopt;
    const auto id = canonicalId(request_id);
    const auto known = std::find_if(owner.confirmed.begin(), owner.confirmed.end(),
                                    [&](const Impl::Receipt& receipt) { return receipt.id == id; });
    if (known != owner.confirmed.end())
        return known->result;
    if (owner.pending && owner.pending->id == id && !owner.operation)
        return DownloadSubmissionResult{};
    return std::nullopt;
}
bool DownloadAdmissionController::retryAdmission() {
    auto& owner = *impl_;
    const bool resuming = owner.service.hasUnconfirmedRestoredQueue();
    if (owner.operating || owner.closing || (!owner.pending && !resuming) || owner.uncertain_offer ||
        (owner.operation && !owner.operation->drain_only) ||
        owner.state != DownloadAdmissionControllerState::AdmissionFailed)
        return false;
    const Impl::Guard guard(owner);
    return owner.save(resuming ? Impl::Purpose::Resume : Impl::Purpose::Admission);
}
DownloadRetryStatus DownloadAdmissionController::downloadRetryStatus(const std::string_view job_id) const {
    const auto& owner = *impl_;
    if (owner.operating || owner.closing || owner.state != DownloadAdmissionControllerState::Ready || owner.operation ||
        owner.uncertain_offer || owner.pending || owner.archive_checkpoint)
        return DownloadRetryStatus::Unavailable;
    if (std::ranges::find(owner.retried_jobs, job_id) != owner.retried_jobs.end())
        return DownloadRetryStatus::AlreadyRetried;
    return owner.service.downloadRetryStatus(job_id);
}
DownloadAdmissionResult DownloadAdmissionController::retryDownload(const std::string_view job_id,
                                                                   const std::string_view new_request_id) {
    auto& owner = *impl_;
    if (downloadRetryStatus(job_id) != DownloadRetryStatus::Available)
        return rejected();
    try {
        const auto id = canonicalId(new_request_id);
        if (id.empty() ||
            std::ranges::any_of(owner.confirmed, [&](const Impl::Receipt& receipt) { return receipt.id == id; }))
            return rejected(DownloadSubmissionError::InvalidRequest);
        auto source = owner.service.retrySource(job_id);
        if (!source)
            return rejected();
        DownloadRequest request{.request_id = id, .items = {std::move(*source)}};
        // Only current rows retain a retry marker. Archive removal cannot grow
        // this session-only index beyond the service's bounded live ledger.
        const auto jobs = owner.service.snapshot();
        std::erase_if(owner.retried_jobs, [&](const std::string& previous) {
            return std::ranges::none_of(jobs, [&](const DownloadJobSnapshot& job) { return job.job_id == previous; });
        });
        owner.retried_jobs.emplace_back(job_id);
        auto result = begin(request);
        // Failed/uncertain saves retain the staged admission and its marker.
        // Only a rejection before staging permits the same row to try again.
        if (!owner.pending || owner.pending->id != id)
            owner.retried_jobs.pop_back();
        return result;
    } catch (...) {
        return rejected();
    }
}
bool DownloadAdmissionController::canArchiveSettled() const {
    const auto& owner = *impl_;
    return !owner.operating && !owner.closing && owner.state == DownloadAdmissionControllerState::Ready &&
           !owner.operation && !owner.uncertain_offer && !owner.pending && !owner.archive_checkpoint &&
           owner.service.canArchiveSettled();
}
bool DownloadAdmissionController::archiveSettled() {
    auto& owner = *impl_;
    if (!canArchiveSettled())
        return false;
    const Impl::Guard guard(owner);
    owner.archive_checkpoint = owner.service.stageSettledArchive();
    return owner.archive_checkpoint && owner.save(Impl::Purpose::Archive);
}
bool DownloadAdmissionController::canRetryArchive() const {
    const auto& owner = *impl_;
    return !owner.operating && !owner.closing && owner.state == DownloadAdmissionControllerState::ArchiveFailed &&
           owner.archive_checkpoint && !owner.uncertain_offer && (!owner.operation || owner.operation->drain_only);
}
bool DownloadAdmissionController::retryArchive() {
    if (!canRetryArchive())
        return false;
    auto& owner = *impl_;
    const Impl::Guard guard(owner);
    return owner.save(Impl::Purpose::Archive);
}
DownloadStartupRecoveryAction DownloadAdmissionController::startupRecoveryAction() const {
    const auto& owner = *impl_;
    if (owner.operating || owner.closing || owner.healthy_startup || owner.operation ||
        owner.state != DownloadAdmissionControllerState::Blocked)
        return DownloadStartupRecoveryAction::None;
    if (owner.reconcile_imports && !owner.import_history_healthy)
        return DownloadStartupRecoveryAction::ReloadSession;
    if (owner.blocked_recovery_status == DownloadRecoveryStatus::ReceiptConflict)
        return DownloadStartupRecoveryAction::ReviewHistory;
    if (owner.blocked_recovery_status == DownloadRecoveryStatus::NamespaceConflict)
        return owner.namespace_checkpoint ? DownloadStartupRecoveryAction::RenewNamespace
                                          : DownloadStartupRecoveryAction::None;
    return DownloadStartupRecoveryAction::RetryCheckpoint;
}
bool DownloadAdmissionController::retryStartup() {
    auto& owner = *impl_;
    if (startupRecoveryAction() != DownloadStartupRecoveryAction::RetryCheckpoint)
        return false;
    const Impl::Guard guard(owner);
    return owner.load();
}
bool DownloadAdmissionController::renewStartupNamespace() {
    auto& owner = *impl_;
    if (startupRecoveryAction() != DownloadStartupRecoveryAction::RenewNamespace)
        return false;
    const Impl::Guard guard(owner);
    try {
        auto receipts = Impl::acknowledgements(*owner.namespace_checkpoint);
        const bool restored =
            owner.reconcile_imports
                ? owner.service.restoreWithFreshNamespace(*owner.namespace_checkpoint, owner.import_receipts)
                : owner.service.restoreWithFreshNamespace(*owner.namespace_checkpoint);
        if (!restored) {
            owner.blocked_recovery_status = owner.service.recoveryReport().status;
            if (owner.blocked_recovery_status != DownloadRecoveryStatus::NamespaceConflict)
                owner.namespace_checkpoint.reset();
            return false;
        }
        owner.confirmed.swap(receipts);
        owner.namespace_checkpoint.reset();
        owner.blocked_recovery_status = DownloadRecoveryStatus::NotRequested;
        owner.finishRestore();
        return true;
    } catch (...) {
        return false;
    }
}
void DownloadAdmissionController::shutdown() {
    auto& owner = *impl_;
    owner.closing = true;
    if (!owner.operating) {
        const Impl::Guard guard(owner);
        owner.applyShutdown();
    }
}
bool DownloadAdmissionController::retryFinalSave() {
    auto& owner = *impl_;
    if (owner.operating || !owner.holdsFinalFailure() || owner.uncertain_offer ||
        owner.state != DownloadAdmissionControllerState::FinalSaveFailed ||
        (owner.operation && !owner.operation->drain_only))
        return false;
    const Impl::Guard guard(owner);
    owner.final_attempted = true;
    return owner.save(owner.archive_checkpoint ? Impl::Purpose::Archive : Impl::Purpose::Final);
}
bool DownloadAdmissionController::closeAfterFinalSaveFailure() {
    auto& owner = *impl_;
    if (owner.operating || !owner.holdsFinalFailure() ||
        owner.state != DownloadAdmissionControllerState::FinalSaveFailed)
        return false;
    const Impl::Guard guard(owner);
    const auto failure = owner.save_status;
    owner.state = DownloadAdmissionControllerState::Stopping;
    owner.closeIo();
    // An explicit lossy exit acknowledges neither the final save nor a failed
    // admission. Shutdown still has to prove physical native drain separately.
    owner.save_status = failure;
    return true;
}
bool DownloadAdmissionController::stopped() const { return impl_->state == DownloadAdmissionControllerState::Stopped; }
DownloadAdmissionControllerState DownloadAdmissionController::state() const { return impl_->state; }
std::optional<DownloadCheckpointLoadStatus> DownloadAdmissionController::loadStatus() const {
    return impl_->load_status;
}
std::optional<DownloadCheckpointSaveStatus> DownloadAdmissionController::saveStatus() const {
    return impl_->save_status;
}
DownloadRecoveryReport DownloadAdmissionController::recoveryReport() const {
    if (impl_->reconcile_imports && !impl_->import_history_healthy)
        return {.status = DownloadRecoveryStatus::InvalidHistory};
    if (impl_->blocked_recovery_status != DownloadRecoveryStatus::NotRequested)
        return {.status = impl_->blocked_recovery_status};
    return impl_->service.recoveryReport();
}
} // namespace music_player::m0
