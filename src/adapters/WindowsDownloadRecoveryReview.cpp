#include "adapters/WindowsDownloadRecoveryReview.hpp"

#include "adapters/WindowsDownloadOwnershipRecovery.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace music_player::m0 {
namespace {
bool validJob(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
               ch == '-';
    });
}
DownloadRecoveryReviewOutcome checked(DownloadRecoveryReviewOutcome outcome) {
    switch (outcome) {
    case DownloadRecoveryReviewOutcome::Unverified:
    case DownloadRecoveryReviewOutcome::OwnedPartial:
    case DownloadRecoveryReviewOutcome::OwnedFinalCandidate:
    case DownloadRecoveryReviewOutcome::Missing:
    case DownloadRecoveryReviewOutcome::Conflict:
    case DownloadRecoveryReviewOutcome::Unreadable:
    case DownloadRecoveryReviewOutcome::HistoryMissing:
    case DownloadRecoveryReviewOutcome::HistoryInvalid:
    case DownloadRecoveryReviewOutcome::HistoryFuture:
        return outcome;
    }
    return DownloadRecoveryReviewOutcome::Unreadable;
}
DownloadRecoveryReviewOutcome assess(DownloadOwnershipStore& store, const WindowsDownloadWorkspaceConfig& config,
                                     std::string_view job_id) {
    const auto loaded = store.load();
    if (loaded.status != DownloadCheckpointLoadStatus::Loaded && loaded.journal)
        return DownloadRecoveryReviewOutcome::Unreadable;
    switch (loaded.status) {
    case DownloadCheckpointLoadStatus::Missing:
        return DownloadRecoveryReviewOutcome::HistoryMissing;
    case DownloadCheckpointLoadStatus::Invalid:
        return DownloadRecoveryReviewOutcome::HistoryInvalid;
    case DownloadCheckpointLoadStatus::FutureVersion:
        return DownloadRecoveryReviewOutcome::HistoryFuture;
    case DownloadCheckpointLoadStatus::Unreadable:
        return DownloadRecoveryReviewOutcome::Unreadable;
    case DownloadCheckpointLoadStatus::Loaded:
        if (!loaded.journal || !validDownloadOwnershipJournal(*loaded.journal))
            return DownloadRecoveryReviewOutcome::HistoryInvalid;
        break;
    default:
        return DownloadRecoveryReviewOutcome::Unreadable;
    }
    const auto& records = loaded.journal->records;
    const auto found =
        std::find_if(records.begin(), records.end(), [&](const auto& item) { return item.job_id == job_id; });
    if (found == records.end())
        return DownloadRecoveryReviewOutcome::Unverified;
    switch (WindowsDownloadOwnershipRecovery::assess(config, *found)) {
    case DownloadOwnershipAssessment::Unverified:
        return DownloadRecoveryReviewOutcome::Unverified;
    case DownloadOwnershipAssessment::OwnedPartial:
        return DownloadRecoveryReviewOutcome::OwnedPartial;
    case DownloadOwnershipAssessment::OwnedFinalCandidate:
        return DownloadRecoveryReviewOutcome::OwnedFinalCandidate;
    case DownloadOwnershipAssessment::Missing:
        return DownloadRecoveryReviewOutcome::Missing;
    case DownloadOwnershipAssessment::Conflict:
        return DownloadRecoveryReviewOutcome::Conflict;
    case DownloadOwnershipAssessment::Unreadable:
        return DownloadRecoveryReviewOutcome::Unreadable;
    }
    return DownloadRecoveryReviewOutcome::Unreadable;
}
} // namespace

struct WindowsDownloadRecoveryReview::Impl final {
    struct Request final {
        std::uint64_t operation_id{};
        std::string job_id{};
    };
    explicit Impl(Factory factory) : worker([this, factory = std::move(factory)]() mutable { run(factory); }) {}
    ~Impl() {
        shutdown();
        if (worker.joinable())
            worker.join();
    }
    DownloadRecoveryReviewCommand admission(std::uint64_t id) const {
        if (closed)
            return DownloadRecoveryReviewCommand::Closed;
        if (pending || active || completion)
            return DownloadRecoveryReviewCommand::Busy;
        if (!id || id <= last_id)
            return DownloadRecoveryReviewCommand::InvalidRequest;
        return DownloadRecoveryReviewCommand::Accepted;
    }
    DownloadRecoveryReviewCommand startReview(std::uint64_t id, std::string_view job_id) {
        {
            const std::scoped_lock lock(mutex);
            const auto command = admission(id);
            if (command != DownloadRecoveryReviewCommand::Accepted)
                return command;
            if (!validJob(job_id))
                return DownloadRecoveryReviewCommand::InvalidRequest;
            pending = Request{id, std::string{job_id}};
            last_id = id;
        }
        condition.notify_one();
        return DownloadRecoveryReviewCommand::Accepted;
    }
    std::optional<DownloadRecoveryReviewResult> poll(std::uint64_t id) {
        const std::scoped_lock lock(mutex);
        if (!completion || completion->operation_id != id)
            return std::nullopt;
        return std::exchange(completion, std::nullopt);
    }
    void shutdown() {
        {
            const std::scoped_lock lock(mutex);
            closed = true;
        }
        condition.notify_one();
    }
    bool stopped() const {
        const std::scoped_lock lock(mutex);
        return drained;
    }
    static DownloadRecoveryReviewResult failure(const Request& request) {
        return {request.operation_id, request.job_id, DownloadRecoveryReviewOutcome::Unreadable};
    }
    void run(Factory& factory) {
        Work work;
        bool attempted_factory = false;
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] { return closed || pending.has_value(); });
                if (closed) {
                    if (pending) {
                        completion = failure(*pending);
                        pending.reset();
                    }
                    break;
                }
                request = std::move(*pending);
                pending.reset();
                active = true;
            }
            if (!attempted_factory) {
                attempted_factory = true;
                try {
                    if (factory)
                        work = factory();
                } catch (...) {
                    work = {};
                }
            }
            bool cancel_before_io = false;
            {
                const std::scoped_lock lock(mutex);
                cancel_before_io = closed;
            }
            auto result = failure(request);
            try {
                if (!cancel_before_io && work)
                    result.outcome = checked(work(request.job_id));
            } catch (...) {
                // Preserve only a closed failure code, not native error/path.
            }
            {
                const std::scoped_lock lock(mutex);
                completion = std::move(result);
                active = false;
            }
        }
        // Native work may not be preemptible. Acknowledgement follows resource
        // destruction on the actual private thread, never its UI-side wrapper.
        work = {};
        factory = {};
        {
            const std::scoped_lock lock(mutex);
            drained = true;
        }
    }
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::optional<Request> pending{};
    std::optional<DownloadRecoveryReviewResult> completion{};
    std::uint64_t last_id{};
    bool active{false}, closed{false}, drained{false};
    std::thread worker;
};

WindowsDownloadRecoveryReview::WindowsDownloadRecoveryReview(QString journal_directory,
                                                             WindowsDownloadWorkspaceConfig config)
    : WindowsDownloadRecoveryReview([directory = std::move(journal_directory), config = std::move(config)] {
          auto store = std::make_shared<WindowsDownloadOwnershipStore>(directory);
          return [store = std::move(store), config](std::string_view id) { return assess(*store, config, id); };
      }) {}
WindowsDownloadRecoveryReview::WindowsDownloadRecoveryReview(Factory factory)
    : impl_(std::make_unique<Impl>(std::move(factory))) {}
WindowsDownloadRecoveryReview::~WindowsDownloadRecoveryReview() = default;
DownloadRecoveryReviewCommand WindowsDownloadRecoveryReview::startReview(std::uint64_t id, std::string_view job_id) {
    return impl_->startReview(id, job_id);
}
std::optional<DownloadRecoveryReviewResult> WindowsDownloadRecoveryReview::poll(std::uint64_t id) {
    return impl_->poll(id);
}
void WindowsDownloadRecoveryReview::shutdown() { impl_->shutdown(); }
bool WindowsDownloadRecoveryReview::stopped() const { return impl_->stopped(); }
} // namespace music_player::m0
