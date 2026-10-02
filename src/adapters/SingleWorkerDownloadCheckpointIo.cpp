#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace music_player::m0 {
namespace {
struct Request final {
    std::uint64_t operation_id{0};
    DownloadCheckpointOperation operation{DownloadCheckpointOperation::Load};
    DownloadCheckpoint checkpoint{};
};

DownloadCheckpointIoResult failure(const Request& request) {
    DownloadCheckpointIoResult result{.operation_id = request.operation_id, .operation = request.operation};
    if (request.operation == DownloadCheckpointOperation::Load)
        result.load.status = DownloadCheckpointLoadStatus::Unreadable;
    return result;
}

DownloadCheckpointLoadResult checkedLoad(DownloadCheckpointLoadResult result) {
    switch (result.status) {
    case DownloadCheckpointLoadStatus::Loaded:
        if (result.checkpoint && validDownloadCheckpoint(*result.checkpoint))
            return result;
        break;
    case DownloadCheckpointLoadStatus::Missing:
    case DownloadCheckpointLoadStatus::Invalid:
    case DownloadCheckpointLoadStatus::Unreadable:
    case DownloadCheckpointLoadStatus::FutureVersion:
        if (!result.checkpoint)
            return result;
        break;
    }
    return {.status = DownloadCheckpointLoadStatus::Unreadable};
}

DownloadCheckpointSaveStatus checkedSave(const DownloadCheckpointSaveStatus status) {
    switch (status) {
    case DownloadCheckpointSaveStatus::Saved:
    case DownloadCheckpointSaveStatus::Failed:
    case DownloadCheckpointSaveStatus::BlockedByExistingData:
        return status;
    }
    return DownloadCheckpointSaveStatus::Failed;
}

DownloadCheckpointIoResult execute(DownloadCheckpointStore& store, const Request& request) noexcept {
    auto result = failure(request);
    try {
        if (request.operation == DownloadCheckpointOperation::Load)
            result.load = checkedLoad(store.load());
        else
            result.save_status = checkedSave(store.save(request.checkpoint));
    } catch (...) {
        // A failing save may already have committed. There is deliberately no
        // rollback, readback promotion, retry or replacement here.
        return failure(request);
    }
    return result;
}
} // namespace

class SingleWorkerDownloadCheckpointIo::Impl final {
  public:
    explicit Impl(Factory factory) : worker_([this, factory = std::move(factory)]() mutable { run(factory); }) {}

    ~Impl() {
        shutdown();
        if (worker_.joinable())
            worker_.join();
    }

    DownloadCheckpointIoCommand startLoad(const std::uint64_t operation_id) {
        {
            const std::scoped_lock lock(mutex_);
            const auto command = admission(operation_id);
            if (command != DownloadCheckpointIoCommand::Accepted)
                return command;
            pending_ = Request{.operation_id = operation_id};
            last_accepted_id_ = operation_id;
        }
        condition_.notify_one();
        return DownloadCheckpointIoCommand::Accepted;
    }

    DownloadCheckpointIoCommand startSave(const std::uint64_t operation_id, const DownloadCheckpoint& checkpoint) {
        {
            const std::scoped_lock lock(mutex_);
            const auto command = admission(operation_id);
            if (command != DownloadCheckpointIoCommand::Accepted)
                return command;
        }
        // Rejected Busy/Closed commands never inspect or copy the snapshot.
        // The pure validator caps collections and fields before copying them.
        DownloadCheckpoint owned;
        try {
            if (!validDownloadCheckpoint(checkpoint))
                return DownloadCheckpointIoCommand::InvalidRequest;
            owned = checkpoint;
        } catch (...) {
            return DownloadCheckpointIoCommand::InvalidRequest;
        }
        {
            const std::scoped_lock lock(mutex_);
            const auto command = admission(operation_id);
            if (command != DownloadCheckpointIoCommand::Accepted)
                return command;
            pending_ = Request{.operation_id = operation_id,
                               .operation = DownloadCheckpointOperation::Save,
                               .checkpoint = std::move(owned)};
            last_accepted_id_ = operation_id;
        }
        condition_.notify_one();
        return DownloadCheckpointIoCommand::Accepted;
    }

    std::optional<DownloadCheckpointIoResult> poll(const std::uint64_t operation_id) {
        const std::scoped_lock lock(mutex_);
        if (!completion_ || completion_->operation_id != operation_id)
            return std::nullopt;
        auto result = std::move(completion_);
        completion_.reset();
        return result;
    }

    void shutdown() {
        {
            const std::scoped_lock lock(mutex_);
            closed_ = true;
        }
        condition_.notify_one();
    }

    bool stopped() const {
        const std::scoped_lock lock(mutex_);
        return stopped_;
    }

  private:
    // Called only while mutex_ is held. A terminal result still occupies the
    // single slot until the caller observes it using its matching ID.
    DownloadCheckpointIoCommand admission(const std::uint64_t operation_id) const {
        if (closed_)
            return DownloadCheckpointIoCommand::Closed;
        if (pending_ || active_ || completion_)
            return DownloadCheckpointIoCommand::Busy;
        if (operation_id == 0 || operation_id <= last_accepted_id_)
            return DownloadCheckpointIoCommand::InvalidRequest;
        return DownloadCheckpointIoCommand::Accepted;
    }

    void run(Factory& factory) {
        std::unique_ptr<DownloadCheckpointStore> store;
        bool attempted_factory = false;
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [&] { return closed_ || pending_.has_value(); });
                if (closed_) {
                    if (pending_) {
                        completion_ = failure(*pending_);
                        pending_.reset();
                    }
                    break;
                }
                request = std::move(*pending_);
                pending_.reset();
                active_ = true;
            }

            // Lazy single construction also permits shutdown of an unused
            // worker without invoking a factory or opening a store.
            if (!attempted_factory) {
                attempted_factory = true;
                try {
                    if (factory)
                        store = factory();
                } catch (...) {
                    store.reset();
                }
            }

            bool cancel_before_io = false;
            {
                const std::scoped_lock lock(mutex_);
                cancel_before_io = closed_;
            }
            // Once execution passes this boundary, shutdown cannot preempt a
            // native store call. It drains and keeps the actual typed result.
            auto result = (!cancel_before_io && store) ? execute(*store, request) : failure(request);
            {
                const std::scoped_lock lock(mutex_);
                completion_ = std::move(result);
                active_ = false;
            }
        }

        // No port work or potentially blocking resource destruction is done
        // under the public mutex. Clear the actual thread closure's Factory,
        // not a moved-from copy, before reporting resource quiescence.
        store.reset();
        factory = {};
        {
            const std::scoped_lock lock(mutex_);
            stopped_ = true;
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Request> pending_{};
    std::optional<DownloadCheckpointIoResult> completion_{};
    std::uint64_t last_accepted_id_{0};
    bool active_{false};
    bool closed_{false};
    bool stopped_{false};
    std::thread worker_;
};

SingleWorkerDownloadCheckpointIo::SingleWorkerDownloadCheckpointIo(Factory factory)
    : impl_(std::make_unique<Impl>(std::move(factory))) {}
SingleWorkerDownloadCheckpointIo::~SingleWorkerDownloadCheckpointIo() = default;

DownloadCheckpointIoCommand SingleWorkerDownloadCheckpointIo::startLoad(const std::uint64_t operation_id) {
    return impl_->startLoad(operation_id);
}
DownloadCheckpointIoCommand SingleWorkerDownloadCheckpointIo::startSave(const std::uint64_t operation_id,
                                                                        const DownloadCheckpoint& checkpoint) {
    return impl_->startSave(operation_id, checkpoint);
}
std::optional<DownloadCheckpointIoResult> SingleWorkerDownloadCheckpointIo::poll(const std::uint64_t operation_id) {
    return impl_->poll(operation_id);
}
void SingleWorkerDownloadCheckpointIo::shutdown() { impl_->shutdown(); }
bool SingleWorkerDownloadCheckpointIo::stopped() const { return impl_->stopped(); }

} // namespace music_player::m0
