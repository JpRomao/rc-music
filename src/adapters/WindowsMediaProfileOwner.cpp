#include "adapters/WindowsMediaProfileOwner.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#include <QUuid>

#include <algorithm>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
using Stage = DownloadMediaProfileStage;
using State = WindowsMediaProfileOwnerState;
using Fence = WindowsDownloadNativeUseFence;
struct BusyGuard final {
    bool& busy;
    explicit BusyGuard(bool& value) : busy(value) { busy = true; }
    ~BusyGuard() { busy = false; }
};
bool validJob(const std::string& job) {
    return !job.empty() && job.size() <= 64 && std::ranges::all_of(job, [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool terminal(const Stage stage) { return stage == Stage::Removed || stage == Stage::Abandoned; }
} // namespace

struct WindowsMediaProfileOwner::Impl final {
    enum class AfterSave { Create, Activate, Ready, Remove, Removed, Abandoned };
    DownloadMediaProfileStore& store;
    WindowsMediaProfilePlatform& platform;
    WindowsMediaProfileOwnerConfig config;
    State state{State::Idle};
    bool busy{false};
    bool closing{false};
    bool created_here{false};
    std::unique_ptr<DownloadMediaProfileLease> lease{};
    std::optional<DownloadMediaProfileRecord> saved{};
    std::optional<DownloadMediaProfileRecord> pending{};
    AfterSave after_save{AfterSave::Create};
    QString name{};
    std::shared_ptr<Fence> fence{};
    std::shared_ptr<const WindowsDownloadAppContainer> capability{};

    Impl(DownloadMediaProfileStore& journal, WindowsMediaProfilePlatform& native,
         WindowsMediaProfileOwnerConfig settings)
        : store(journal), platform(native), config(std::move(settings)) {}
    ~Impl() {
        // Revocation is in-memory only. Destruction never invents a stop ACK,
        // writes a terminal record or asks the platform to remove a profile.
        if (fence)
            static_cast<void>(fence->closeAdmission());
    }
    bool finished() const { return state == State::Ready || state == State::Removed || state == State::Abandoned; }
    void unexpectedFailure() {
        closing = true;
        if (fence)
            static_cast<void>(fence->closeAdmission());
        pending.reset();
        state = State::BlockedHistory;
    }
    void persist(DownloadMediaProfileRecord record, const AfterSave next) {
        pending = std::move(record);
        after_save = next;
        DownloadCheckpointSaveStatus result;
        try {
            result = store.record(*pending);
        } catch (...) {
            result = DownloadCheckpointSaveStatus::Failed;
        }
        if (result != DownloadCheckpointSaveStatus::Saved) {
            state = result == DownloadCheckpointSaveStatus::Failed ? State::PersistenceFailed : State::BlockedHistory;
            return;
        }
        saved = std::move(pending);
        pending.reset();
        advance(next);
    }
    void persistStage(const Stage stage, const AfterSave next) {
        auto record = *saved;
        record.stage = stage;
        persist(std::move(record), next);
    }
    void retire() {
        if (!fence || !created_here || !saved || (saved->stage != Stage::Created && saved->stage != Stage::InUse)) {
            state = State::NativeUseUnconfirmed;
            return;
        }
        switch (fence->closeAdmission()) {
        case Fence::State::ClosingBusy:
            state = State::Draining;
            return;
        case Fence::State::ClosedQuiescent:
            persistStage(Stage::CleanupPending, AfterSave::Remove);
            return;
        default:
            state = State::NativeUseUnconfirmed;
            return;
        }
    }
    void advance(const AfterSave next) {
        switch (next) {
        case AfterSave::Create: {
            if (closing) {
                persistStage(Stage::Abandoned, AfterSave::Abandoned);
                return;
            }
            WindowsMediaProfileCreateResult result;
            try {
                result = platform.create(name);
            } catch (...) {
                state = State::CreationUnconfirmed;
                return;
            }
            if (result.status != WindowsMediaProfileCreateStatus::Created) {
                state = State::CreationUnconfirmed;
                return;
            }
            capability = WindowsDownloadAppContainer::fromTrustedSid(std::move(result.sid), fence);
            if (!capability) {
                state = State::CreationUnconfirmed;
                return;
            }
            created_here = true;
            auto record = *saved;
            record.stage = Stage::Created;
            DownloadMediaProfileSid sid{};
            const auto& bytes = capability->sidBytes();
            std::copy(bytes.begin(), bytes.end(), sid.begin());
            record.sid = sid;
            persist(std::move(record), AfterSave::Activate);
            return;
        }
        case AfterSave::Activate:
            if (closing)
                retire();
            else
                persistStage(Stage::InUse, AfterSave::Ready);
            return;
        case AfterSave::Ready:
            if (closing)
                retire();
            else
                state = State::Ready;
            return;
        case AfterSave::Remove: {
            WindowsMediaProfileRemovalResult result;
            try {
                result = platform.remove(name, capability->sidBytes());
            } catch (...) {
                state = State::RemovalUnconfirmed;
                return;
            }
            if (!result.api_succeeded || result.absence != WindowsMediaProfileAbsence::Absent) {
                state = State::RemovalUnconfirmed;
                return;
            }
            persistStage(Stage::Removed, AfterSave::Removed);
            return;
        }
        case AfterSave::Removed:
            state = State::Removed;
            created_here = false;
            lease.reset();
            return;
        case AfterSave::Abandoned:
            state = State::Abandoned;
            lease.reset();
            return;
        }
    }
    void prepare() {
        if (!validJob(config.job_id)) {
            state = State::InvalidConfiguration;
            return;
        }
        try {
            lease = store.acquireLease();
            if (!lease) {
                state = State::BlockedHistory;
                return;
            }
            const auto loaded = store.load();
            const bool missing = loaded.status == DownloadCheckpointLoadStatus::Missing && !loaded.record;
            const bool ended = loaded.status == DownloadCheckpointLoadStatus::Loaded && loaded.record &&
                               validDownloadMediaProfileRecord(*loaded.record) && terminal(loaded.record->stage);
            if (!missing && !ended) {
                state = State::BlockedHistory;
                lease.reset();
                return;
            }
            if (closing) {
                state = State::Abandoned;
                lease.reset();
                return;
            }
            auto operation = config.operation_identity
                                 ? config.operation_identity()
                                 : QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            DownloadMediaProfileRecord planned{config.job_id, std::move(operation), Stage::Planned, {}};
            if (!validDownloadMediaProfileRecord(planned) ||
                (loaded.record && planned.operation_id == loaded.record->operation_id)) {
                state = State::InvalidConfiguration;
                lease.reset();
                return;
            }
            name = QString::fromStdString(downloadMediaProfileName(planned.operation_id));
            fence = std::make_shared<Fence>();
            persist(std::move(planned), AfterSave::Create);
        } catch (...) {
            unexpectedFailure();
        }
    }
};
WindowsMediaProfileOwner::WindowsMediaProfileOwner(DownloadMediaProfileStore& store,
                                                   WindowsMediaProfilePlatform& platform,
                                                   WindowsMediaProfileOwnerConfig config)
    : impl_(std::make_unique<Impl>(store, platform, std::move(config))) {}
WindowsMediaProfileOwner::~WindowsMediaProfileOwner() = default;
bool WindowsMediaProfileOwner::prepare() {
    auto& value = *impl_;
    if (value.busy)
        return false;
    if (value.state == State::Ready && !value.closing)
        return true;
    if (value.state != State::Idle)
        return false;
    const BusyGuard guard(value.busy);
    value.prepare();
    return value.state == State::Ready;
}
bool WindowsMediaProfileOwner::close() {
    auto& value = *impl_;
    value.closing = true;
    if (value.fence)
        static_cast<void>(value.fence->closeAdmission());
    if (value.busy)
        return false;
    const BusyGuard guard(value.busy);
    try {
        if (value.state == State::Idle)
            value.state = State::Abandoned;
        else if (value.state == State::Ready || value.state == State::Draining)
            value.retire();
    } catch (...) {
        value.unexpectedFailure();
    }
    return value.state == State::Removed || value.state == State::Abandoned;
}
bool WindowsMediaProfileOwner::retryPersistence() {
    auto& value = *impl_;
    if (!canRetryPersistence())
        return false;
    const BusyGuard guard(value.busy);
    try {
        value.persist(*value.pending, value.after_save);
    } catch (...) {
        value.unexpectedFailure();
    }
    return value.finished();
}
bool WindowsMediaProfileOwner::canRetryPersistence() const {
    return !impl_->busy && impl_->state == State::PersistenceFailed && impl_->pending.has_value();
}
WindowsMediaProfileOwnerState WindowsMediaProfileOwner::state() const { return impl_->state; }
std::shared_ptr<const WindowsDownloadAppContainer> WindowsMediaProfileOwner::capability() const {
    return !impl_->busy && !impl_->closing && impl_->state == State::Ready ? impl_->capability : nullptr;
}
} // namespace music_player::m0
