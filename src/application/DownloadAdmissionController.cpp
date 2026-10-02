#include "m0/application/DownloadAdmissionController.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0 {
namespace {
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
    enum class Purpose { Load, Admission, Final };
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
    std::vector<Receipt> confirmed;
    std::optional<Receipt> pending;
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

    Impl(DownloadService& owner, DownloadCheckpointIo& storage, const DownloadFinalSavePolicy policy)
        : service(owner), io(storage), final_save_policy(policy) {
        confirmed.reserve(download_checkpoint_max_receipts);
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
        service.shutdown();
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
        state = holdsFinalFailure() ? DownloadAdmissionControllerState::FinalSaveFailed
                : closing           ? DownloadAdmissionControllerState::Stopping
                                    : DownloadAdmissionControllerState::AdmissionFailed;
    }
    bool save(const Purpose purpose) {
        std::optional<std::uint64_t> offered_id;
        try {
            const auto checkpoint = service.checkpoint();
            const auto id = newOperationId();
            if (checkpoint && id) {
                offered_id = id;
                if (io.startSave(*id, *checkpoint) == DownloadCheckpointIoCommand::Accepted) {
                    // Accepted is the port's slot acknowledgement, not Saved.
                    // It can supersede an uncertain prior call only because the
                    // single-slot contract refuses outstanding native work.
                    operation = Operation{*id, purpose};
                    uncertain_offer.reset();
                    state = closing ? DownloadAdmissionControllerState::Stopping
                                    : DownloadAdmissionControllerState::AdmissionPending;
                    return true;
                }
            }
        } catch (...) {
            if (final_save_policy == DownloadFinalSavePolicy::HoldForDecision && offered_id) {
                if (!operation)
                    operation = Operation{*offered_id, purpose, true};
                else if (operation->drain_only)
                    uncertain_offer = Operation{*offered_id, purpose, true};
            }
        }
        failedSave(DownloadCheckpointSaveStatus::Failed);
        return false;
    }
    void observeLoad(DownloadCheckpointLoadResult loaded) {
        load_status = loaded.status;
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
            // Allocate the complete acknowledgement index before the atomic restore.
            std::vector<Receipt> receipts;
            receipts.reserve(download_checkpoint_max_receipts);
            for (const auto& receipt : checkpoint->receipts)
                receipts.push_back(
                    {receipt.request_id, {.error = DownloadSubmissionError::None, .job_ids = receipt.job_ids}});
            const bool restored =
                reconcile_imports ? service.restore(*checkpoint, import_receipts) : service.restore(*checkpoint);
            if (!restored) {
                state = DownloadAdmissionControllerState::Blocked;
                return;
            }
            confirmed.swap(receipts);
            healthy_startup = true;
            state = DownloadAdmissionControllerState::Ready;
        } catch (...) {
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
                if (!pending || confirmed.size() >= download_checkpoint_max_receipts ||
                    !service.confirmStaged(pending->id)) {
                    failedSave(DownloadCheckpointSaveStatus::Failed);
                    return;
                }
                // Reserved capacity and noexcept moves precede any observer's ACK.
                confirmed.push_back(std::move(*pending));
                pending.reset();
            }
            save_status = DownloadCheckpointSaveStatus::Saved;
            state = closing ? DownloadAdmissionControllerState::Stopping : DownloadAdmissionControllerState::Ready;
        } catch (...) {
            // A drain-only query cannot restore protocol confidence, including
            // after an explicit lossy close. Keep both possible IDs bounded.
            if (expected.drain_only)
                return;
            if (expected.purpose != Purpose::Load && final_save_policy == DownloadFinalSavePolicy::HoldForDecision)
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
                                                         DownloadFinalSavePolicy final_save_policy)
    : impl_(std::make_unique<Impl>(service, io, final_save_policy)) {}
DownloadAdmissionController::DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io,
                                                         const SessionLoadResult& initial_session,
                                                         DownloadFinalSavePolicy final_save_policy)
    : DownloadAdmissionController(service, io, final_save_policy) {
    auto& owner = *impl_;
    owner.reconcile_imports = true;
    try {
        const bool healthy =
            initial_session.status == SessionLoadStatus::Loaded
                ? isValidSessionSnapshot(initial_session.snapshot)
                : initial_session.status == SessionLoadStatus::Missing && initial_session.snapshot == SessionSnapshot{};
        if (healthy) {
            auto receipts = initial_session.snapshot.download_import_receipts;
            owner.import_receipts = std::move(receipts);
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
void DownloadAdmissionController::tick(const std::int64_t now_ms) {
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
    owner.service.tick(now_ms, [&owner] { return !owner.closing; });
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
    if (owner.operating || owner.closing || !owner.pending || owner.uncertain_offer ||
        (owner.operation && !owner.operation->drain_only) ||
        owner.state != DownloadAdmissionControllerState::AdmissionFailed)
        return false;
    const Impl::Guard guard(owner);
    return owner.save(Impl::Purpose::Admission);
}
bool DownloadAdmissionController::retryStartup() {
    auto& owner = *impl_;
    if (owner.operating || owner.closing || owner.healthy_startup || owner.operation ||
        owner.state != DownloadAdmissionControllerState::Blocked)
        return false;
    const Impl::Guard guard(owner);
    return owner.load();
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
    return owner.save(Impl::Purpose::Final);
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
    return impl_->service.recoveryReport();
}
} // namespace music_player::m0
