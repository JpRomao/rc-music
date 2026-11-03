#include "adapters/WindowsDownloadRecoveryValidation.hpp"

#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsRecoveredMediaValidator.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <mutex>
#include <utility>

namespace music_player::m0 {
namespace {
using Outcome = DownloadRecoveryValidationOutcome;
bool validJob(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '-' || value == '_';
    });
}
bool known(Outcome outcome) {
    switch (outcome) {
    case Outcome::Validated:
    case Outcome::ImportReady:
    case Outcome::PublicationUnconfirmed:
    case Outcome::Released:
    case Outcome::Unverified:
    case Outcome::OwnedPartial:
    case Outcome::Missing:
    case Outcome::Conflict:
    case Outcome::Unreadable:
    case Outcome::HistoryMissing:
    case Outcome::HistoryInvalid:
    case Outcome::HistoryFuture:
    case Outcome::InvalidMedia:
    case Outcome::Timeout:
        return true;
    }
    return false;
}
bool validPreparedPath(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute() || path != path.lexically_normal())
        return false;
    const auto text = QString::fromStdWString(path.native());
    QString normalized;
    return download_artifacts::validPath(text, normalized) &&
           QString::fromStdWString(path.extension().native()).compare(QStringLiteral(".mp3"), Qt::CaseInsensitive) == 0;
}
Outcome ownership(DownloadOwnershipAssessment assessment) {
    switch (assessment) {
    case DownloadOwnershipAssessment::Unverified:
        return Outcome::Unverified;
    case DownloadOwnershipAssessment::OwnedPartial:
        return Outcome::OwnedPartial;
    case DownloadOwnershipAssessment::Missing:
        return Outcome::Missing;
    case DownloadOwnershipAssessment::Conflict:
        return Outcome::Conflict;
    case DownloadOwnershipAssessment::OwnedFinalCandidate:
    case DownloadOwnershipAssessment::Unreadable:
        return Outcome::Unreadable;
    }
    return Outcome::Unreadable;
}
class RecoveryTask final : public WindowsDownloadRecoveryValidationTask {
  public:
    RecoveryTask(QString directory, WindowsDownloadWorkspaceConfig workspace, WindowsLocalMp3ValidatorConfig tools,
                 WindowsDownloadOwnershipStoreOptions options)
        : directory_(std::move(directory)), workspace_(std::move(workspace)), tools_(std::move(tools)),
          ownership_options_(std::move(options)) {}
    bool start(const std::string& id, qint64 now_ms) override {
        if (started_)
            return false;
        started_ = true;
        WindowsDownloadOwnershipStore store(directory_);
        const auto loaded = store.load();
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded && loaded.journal) {
            result_ = Outcome::Unreadable;
            return true;
        }
        switch (loaded.status) {
        case DownloadCheckpointLoadStatus::Missing:
            result_ = Outcome::HistoryMissing;
            return true;
        case DownloadCheckpointLoadStatus::Invalid:
            result_ = Outcome::HistoryInvalid;
            return true;
        case DownloadCheckpointLoadStatus::FutureVersion:
            result_ = Outcome::HistoryFuture;
            return true;
        case DownloadCheckpointLoadStatus::Unreadable:
            result_ = Outcome::Unreadable;
            return true;
        case DownloadCheckpointLoadStatus::Loaded:
            if (!loaded.journal || !validDownloadOwnershipJournal(*loaded.journal)) {
                result_ = Outcome::HistoryInvalid;
                return true;
            }
            break;
        default:
            result_ = Outcome::Unreadable;
            return true;
        }
        const auto found = std::ranges::find(loaded.journal->records, id, &DownloadOwnershipRecord::job_id);
        if (found == loaded.journal->records.end()) {
            result_ = Outcome::Unverified;
            return true;
        }
        validator_ = std::make_unique<WindowsRecoveredMediaValidator>(workspace_, tools_);
        static_cast<void>(validator_->start(*found, now_ms));
        // The fast owner observes cancellation after journal/ownership proof,
        // before the first poll can launch the decoder.
        return true;
    }
    void cancel() override {
        if (validator_)
            validator_->cancel();
        else
            result_ = Outcome::Released;
    }
    std::optional<Outcome> poll(qint64 now_ms) override {
        if (result_)
            return result_;
        if (!validator_)
            return Outcome::Unreadable;
        const auto update = validator_->poll(now_ms);
        switch (update.state) {
        case RecoveredMediaValidationState::Validating:
        case RecoveredMediaValidationState::Stopping:
            return {};
        case RecoveredMediaValidationState::Validated:
            return Outcome::Validated;
        case RecoveredMediaValidationState::Cancelled:
            return Outcome::Released;
        case RecoveredMediaValidationState::Failed:
            if (update.ownership != DownloadOwnershipAssessment::OwnedFinalCandidate)
                return ownership(update.ownership);
            if (update.error == DownloadFailure::Timeout)
                return Outcome::Timeout;
            if (update.error == DownloadFailure::UnsupportedMedia || update.error == DownloadFailure::NoAudio ||
                update.error == DownloadFailure::Conversion || update.error == DownloadFailure::LimitExceeded)
                return Outcome::InvalidMedia;
            return Outcome::Unreadable;
        case RecoveredMediaValidationState::Idle:
            return Outcome::Unreadable;
        }
        return {};
    }
    DownloadRecoveryImportPreparation prepareImport() override {
        const auto* candidate = validator_ ? validator_->validatedCandidate() : nullptr;
        if (!candidate)
            return {};
        auto assessment = candidate->revalidate();
        if (assessment != DownloadOwnershipAssessment::OwnedFinalCandidate)
            return {.outcome = ownership(assessment)};
        WindowsDownloadOwnershipStore store(directory_, ownership_options_);
        const auto loaded = store.load();
        switch (loaded.status) {
        case DownloadCheckpointLoadStatus::Missing:
            return {.outcome = Outcome::HistoryMissing};
        case DownloadCheckpointLoadStatus::Invalid:
            return {.outcome = Outcome::HistoryInvalid};
        case DownloadCheckpointLoadStatus::FutureVersion:
            return {.outcome = Outcome::HistoryFuture};
        case DownloadCheckpointLoadStatus::Unreadable:
            return {.outcome = Outcome::Unreadable};
        case DownloadCheckpointLoadStatus::Loaded:
            if (!loaded.journal || !validDownloadOwnershipJournal(*loaded.journal))
                return {.outcome = Outcome::HistoryInvalid};
            break;
        default:
            return {};
        }
        const auto& expected = candidate->record();
        const auto found =
            std::ranges::find(loaded.journal->records, expected.job_id, &DownloadOwnershipRecord::job_id);
        if (found == loaded.journal->records.end())
            return {.outcome = Outcome::Conflict};
        auto normalized = *found;
        normalized.stage = expected.stage;
        if (normalized != expected ||
            (found->stage != expected.stage && !(expected.stage == DownloadOwnershipStage::PublicationPrepared &&
                                                 found->stage == DownloadOwnershipStage::Published)))
            return {.outcome = Outcome::Conflict};
        auto published = *found;
        published.stage = DownloadOwnershipStage::Published;
        // Even an identical Published replay needs this NEW Saved. Loading
        // bytes left by a previous AfterCommit failure never authorizes import.
        if (store.record(published) != DownloadCheckpointSaveStatus::Saved)
            return {.outcome = Outcome::PublicationUnconfirmed};
        assessment = candidate->revalidate();
        if (assessment != DownloadOwnershipAssessment::OwnedFinalCandidate)
            return {.outcome = ownership(assessment)};
        return {.outcome = Outcome::ImportReady,
                .trusted_mp3 = std::filesystem::path{candidate->pathForValidation().toStdWString()}};
    }

  private:
    QString directory_;
    WindowsDownloadWorkspaceConfig workspace_;
    WindowsLocalMp3ValidatorConfig tools_;
    WindowsDownloadOwnershipStoreOptions ownership_options_;
    bool started_{false};
    std::optional<Outcome> result_{};
    std::unique_ptr<WindowsRecoveredMediaValidator> validator_{};
};
} // namespace

struct WindowsDownloadRecoveryValidation::Impl {
    struct Request {
        std::uint64_t operation_id{};
        std::string job_id{};
    };
    struct Worker final : QThread {
        Impl& owner;
        explicit Worker(Impl& value) : owner(value) {}
        void run() override { owner.run(); }
    };
    TaskFactory factory;
    mutable std::mutex mutex;
    std::optional<Request> slot;
    std::optional<DownloadRecoveryValidationResult> completion;
    std::uint64_t last_id{0};
    bool pending{false}, release_requested{false}, closed{false}, wake_enqueued{false};
    bool retained{false}, prepare_pending{false}, prepare_running{false};
    QObject* dispatcher{nullptr};
    Worker worker;

    explicit Impl(TaskFactory value) : factory(std::move(value)), worker(*this) { worker.start(); }
    void wake() {
        if (!dispatcher || wake_enqueued)
            return;
        wake_enqueued = true;
        QMetaObject::invokeMethod(dispatcher, "start", Qt::QueuedConnection);
    }
    void run() {
        // swap leaves the UI-owned member empty. Closure/capture teardown below
        // therefore precedes QThread::finished on this private worker.
        TaskFactory private_factory;
        private_factory.swap(factory);
        QObject context;
        QTimer wake_timer(&context), timer(&context);
        wake_timer.setSingleShot(true);
        timer.setInterval(5);
        QEventLoop loop;
        QElapsedTimer clock;
        clock.start();
        std::optional<Request> current;
        std::unique_ptr<WindowsDownloadRecoveryValidationTask> task;
        bool uncertain{false}, cancel_sent{false}, held{false}, published{false};
        bool pumping{false}, quitting{false};
        auto cancellation = [&] {
            std::lock_guard lock(mutex);
            return closed || release_requested;
        };
        auto publish = [&](Outcome outcome) {
            task.reset(); // no mutex during potentially blocking teardown
            held = false;
            timer.stop();
            std::lock_guard lock(mutex);
            retained = prepare_pending = prepare_running = false;
            if (current && slot && current->operation_id == slot->operation_id) {
                const auto final = uncertain                     ? Outcome::Unreadable
                                   : closed || release_requested ? Outcome::Released
                                                                 : outcome;
                completion = DownloadRecoveryValidationResult{current->operation_id, current->job_id, final};
                published = true;
            }
        };
        auto pump = [&] {
            if (pumping)
                return;
            QScopedValueRollback guard(pumping, true);
            {
                std::lock_guard lock(mutex);
                if (current && (!slot || slot->operation_id != current->operation_id))
                    current.reset();
                if (!current && slot && pending) {
                    current = *slot;
                    pending = false;
                    uncertain = cancel_sent = held = published = false;
                }
            }
            if (current && !task && !published) {
                if (cancellation())
                    publish(Outcome::Released);
                else {
                    try {
                        if (private_factory)
                            task = private_factory();
                    } catch (...) {
                        uncertain = true;
                    }
                    if (!task)
                        publish(Outcome::Unreadable);
                    else if (cancellation())
                        publish(Outcome::Released); // factory's task is still inert
                    else {
                        try {
                            if (!task->start(current->job_id, clock.elapsed()))
                                uncertain = true;
                        } catch (...) {
                            uncertain = true;
                        }
                        timer.start();
                    }
                }
            }
            if (task) {
                if (held) {
                    if (cancellation())
                        publish(Outcome::Released);
                    else {
                        bool prepare = false;
                        {
                            std::lock_guard lock(mutex);
                            if (prepare_pending && !closed && !release_requested) {
                                prepare_pending = false;
                                prepare_running = prepare = true;
                            }
                        }
                        if (prepare) {
                            DownloadRecoveryImportPreparation prepared;
                            try {
                                prepared = task->prepareImport();
                            } catch (...) {
                                // The synchronous preparation may have saved
                                // before throwing. Never fabricate a ready path.
                                prepared.outcome = Outcome::PublicationUnconfirmed;
                            }
                            {
                                std::lock_guard lock(mutex);
                                prepare_running = false;
                            }
                            if (cancellation())
                                publish(Outcome::Released);
                            else if (prepared.outcome == Outcome::ImportReady &&
                                     validPreparedPath(prepared.trusted_mp3)) {
                                bool retain = false;
                                {
                                    std::lock_guard lock(mutex);
                                    if (!closed && !release_requested && slot &&
                                        slot->operation_id == current->operation_id) {
                                        completion = DownloadRecoveryValidationResult{
                                            current->operation_id, current->job_id, Outcome::ImportReady,
                                            std::move(prepared.trusted_mp3)};
                                        retain = true;
                                    }
                                }
                                if (!retain)
                                    publish(Outcome::Released);
                            } else {
                                const bool valid_failure = known(prepared.outcome) && prepared.trusted_mp3.empty() &&
                                                           prepared.outcome != Outcome::Validated &&
                                                           prepared.outcome != Outcome::ImportReady &&
                                                           prepared.outcome != Outcome::Released;
                                if (!valid_failure)
                                    uncertain = true;
                                publish(valid_failure ? prepared.outcome : Outcome::Unreadable);
                            }
                        }
                    }
                } else {
                    if ((uncertain || cancellation()) && !cancel_sent) {
                        try {
                            task->cancel();
                            cancel_sent = true;
                        } catch (...) {
                            uncertain = true;
                        }
                    }
                    try {
                        const auto result = task->poll(clock.elapsed());
                        if (result && (!known(*result) || *result == Outcome::ImportReady ||
                                       *result == Outcome::PublicationUnconfirmed)) {
                            uncertain = true;
                            cancel_sent = false;
                        } else if (result) {
                            if (*result == Outcome::Validated && !uncertain) {
                                bool retain = false;
                                {
                                    std::lock_guard lock(mutex);
                                    if (!closed && !release_requested && slot &&
                                        slot->operation_id == current->operation_id) {
                                        completion = DownloadRecoveryValidationResult{
                                            current->operation_id, current->job_id, Outcome::Validated};
                                        held = published = retain = true;
                                        retained = true;
                                    }
                                }
                                if (retain)
                                    timer.stop();
                                else
                                    publish(Outcome::Released);
                            } else {
                                if (*result == Outcome::Released && !cancellation())
                                    uncertain = true;
                                publish(*result);
                            }
                        }
                    } catch (...) {
                        uncertain = true;
                        cancel_sent = false;
                    }
                }
            } else if (current && published && cancellation()) {
                std::lock_guard lock(mutex);
                if (!completion)
                    completion = DownloadRecoveryValidationResult{current->operation_id, current->job_id,
                                                                  uncertain ? Outcome::Unreadable : Outcome::Released};
            }
            {
                std::lock_guard lock(mutex);
                if (closed && !task && !pending) {
                    dispatcher = nullptr;
                    quitting = true;
                }
            }
            if (quitting)
                loop.quit();
        };
        QObject::connect(&timer, &QTimer::timeout, &context, pump);
        QObject::connect(&wake_timer, &QTimer::timeout, &context, [&] {
            {
                std::lock_guard lock(mutex);
                wake_enqueued = false;
            }
            pump();
        });
        {
            std::lock_guard lock(mutex);
            dispatcher = &wake_timer;
        }
        pump();
        if (!quitting)
            loop.exec();
        {
            std::lock_guard lock(mutex);
            dispatcher = nullptr;
            wake_enqueued = false;
        }
    }
};
WindowsDownloadRecoveryValidation::WindowsDownloadRecoveryValidation(TaskFactory factory)
    : impl_(std::make_unique<Impl>(std::move(factory))) {}
WindowsDownloadRecoveryValidation::WindowsDownloadRecoveryValidation(QString directory,
                                                                     WindowsDownloadWorkspaceConfig workspace,
                                                                     WindowsLocalMp3ValidatorConfig tools,
                                                                     WindowsDownloadOwnershipStoreOptions options)
    : WindowsDownloadRecoveryValidation([directory = std::move(directory), workspace = std::move(workspace),
                                         tools = std::move(tools), options = std::move(options)] {
          return std::make_unique<RecoveryTask>(directory, workspace, tools, options);
      }) {}
WindowsDownloadRecoveryValidation::~WindowsDownloadRecoveryValidation() {
    shutdown();
    impl_->worker.wait();
}
DownloadRecoveryValidationCommand WindowsDownloadRecoveryValidation::startValidation(std::uint64_t id,
                                                                                     std::string_view job) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed)
        return DownloadRecoveryValidationCommand::Closed;
    if (impl_->slot)
        return DownloadRecoveryValidationCommand::Busy;
    if (!id || id <= impl_->last_id || !validJob(job))
        return DownloadRecoveryValidationCommand::InvalidRequest;
    impl_->slot = Impl::Request{id, std::string{job}};
    impl_->last_id = id;
    impl_->pending = true;
    impl_->completion.reset();
    impl_->release_requested = false;
    impl_->retained = impl_->prepare_pending = impl_->prepare_running = false;
    impl_->wake();
    return DownloadRecoveryValidationCommand::Accepted;
}
bool WindowsDownloadRecoveryValidation::release(std::uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->slot || impl_->slot->operation_id != id)
        return false;
    impl_->release_requested = true;
    if (impl_->completion &&
        (impl_->completion->outcome == Outcome::Validated || impl_->completion->outcome == Outcome::ImportReady))
        impl_->completion.reset();
    impl_->wake();
    return true;
}
DownloadRecoveryValidationCommand WindowsDownloadRecoveryValidation::prepareImport(std::uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed)
        return DownloadRecoveryValidationCommand::Closed;
    if (!impl_->slot || impl_->slot->operation_id != id)
        return DownloadRecoveryValidationCommand::InvalidRequest;
    if (!impl_->retained || impl_->completion || impl_->pending || impl_->prepare_pending || impl_->prepare_running ||
        impl_->release_requested)
        return DownloadRecoveryValidationCommand::Busy;
    impl_->prepare_pending = true;
    impl_->wake();
    return DownloadRecoveryValidationCommand::Accepted;
}
std::optional<DownloadRecoveryValidationResult> WindowsDownloadRecoveryValidation::poll(std::uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->slot || impl_->slot->operation_id != id || !impl_->completion)
        return {};
    auto result = std::exchange(impl_->completion, {});
    if (result->outcome != Outcome::Validated && result->outcome != Outcome::ImportReady) {
        impl_->slot.reset();
        impl_->release_requested = false;
        impl_->retained = impl_->prepare_pending = impl_->prepare_running = false;
        impl_->wake();
    }
    return result;
}
void WindowsDownloadRecoveryValidation::shutdown() {
    std::lock_guard lock(impl_->mutex);
    impl_->closed = true;
    impl_->release_requested = true;
    if (impl_->completion &&
        (impl_->completion->outcome == Outcome::Validated || impl_->completion->outcome == Outcome::ImportReady))
        impl_->completion.reset();
    impl_->wake();
}
bool WindowsDownloadRecoveryValidation::stopped() const { return impl_->worker.isFinished(); }
} // namespace music_player::m0
