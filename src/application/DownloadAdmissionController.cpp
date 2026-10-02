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
    };
    DownloadService& service;
    DownloadCheckpointIo& io;
    std::vector<Receipt> confirmed;
    std::optional<Receipt> pending;
    std::optional<Operation> operation;
    std::optional<DownloadCheckpointLoadStatus> load_status;
    std::optional<DownloadCheckpointSaveStatus> save_status;
    DownloadAdmissionControllerState state{DownloadAdmissionControllerState::NotStarted};
    std::uint64_t next_id{0};
    bool operating{false};
    bool healthy_startup{false};
    bool closing{false};
    bool io_shutdown{false};
    bool final_attempted{false};

    Impl(DownloadService& owner, DownloadCheckpointIo& storage) : service(owner), io(storage) {
        confirmed.reserve(download_checkpoint_max_receipts);
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
        state =
            closing ? DownloadAdmissionControllerState::Stopping : DownloadAdmissionControllerState::AdmissionFailed;
    }
    bool save(const Purpose purpose) {
        try {
            const auto checkpoint = service.checkpoint();
            const auto id = newOperationId();
            if (checkpoint && id && io.startSave(*id, *checkpoint) == DownloadCheckpointIoCommand::Accepted) {
                operation = Operation{*id, purpose};
                if (!closing)
                    state = DownloadAdmissionControllerState::AdmissionPending;
                return true;
            }
        } catch (...) {
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
            // Allocate the complete acknowledgement index before the atomic restore.
            std::vector<Receipt> receipts;
            receipts.reserve(download_checkpoint_max_receipts);
            for (const auto& receipt : checkpoint->receipts)
                receipts.push_back(
                    {receipt.request_id, {.error = DownloadSubmissionError::None, .job_ids = receipt.job_ids}});
            if (!service.restore(*checkpoint)) {
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
    void observeOperation() {
        if (!operation)
            return;
        const auto expected = *operation;
        try {
            auto result = io.poll(expected.id);
            if (!result)
                return;
            operation.reset();
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
            operation.reset();
            if (expected.purpose == Purpose::Load)
                observeLoad({.status = DownloadCheckpointLoadStatus::Unreadable});
            else
                failedSave(DownloadCheckpointSaveStatus::Failed);
        }
    }
    void drain() {
        if (!closing || !service.stopped())
            return;
        if (!io_shutdown && !operation) {
            if (!final_attempted && healthy_startup && !pending &&
                (!save_status || *save_status == DownloadCheckpointSaveStatus::Saved)) {
                final_attempted = true;
                if (save(Purpose::Final))
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

DownloadAdmissionController::DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io)
    : impl_(std::make_unique<Impl>(service, io)) {}
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
    owner.service.tick(now_ms);
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
    if (owner.operating || owner.closing || !owner.pending || owner.operation ||
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
bool DownloadAdmissionController::stopped() const { return impl_->state == DownloadAdmissionControllerState::Stopped; }
DownloadAdmissionControllerState DownloadAdmissionController::state() const { return impl_->state; }
std::optional<DownloadCheckpointLoadStatus> DownloadAdmissionController::loadStatus() const {
    return impl_->load_status;
}
std::optional<DownloadCheckpointSaveStatus> DownloadAdmissionController::saveStatus() const {
    return impl_->save_status;
}
} // namespace music_player::m0
