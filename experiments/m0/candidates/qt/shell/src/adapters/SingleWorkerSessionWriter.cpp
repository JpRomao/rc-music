#include "m0/adapters/SingleWorkerSessionWriter.hpp"

#include <utility>

namespace music_player::m0 {

SingleWorkerSessionWriter::SingleWorkerSessionWriter(SessionStore& store) : store_(store) {
    worker_ = std::thread{[this] { runWorker(); }};
}

SingleWorkerSessionWriter::~SingleWorkerSessionWriter() { static_cast<void>(shutdown()); }

SessionWriterCommand SingleWorkerSessionWriter::start(SessionWriteRequest request) {
    std::lock_guard lock{mutex_};
    if (shutdown_requested_ || stopped_)
        return SessionWriterCommand::Shutdown;
    if (pending_.has_value() || active_revision_.has_value())
        return SessionWriterCommand::Busy;
    if (completion_.has_value() && completion_->revision <= request.revision)
        completion_.reset();
    if (latest_completed_.has_value() && latest_completed_->revision <= request.revision)
        latest_completed_.reset();
    pending_ = std::move(request);
    condition_.notify_all();
    return SessionWriterCommand::Accepted;
}

std::optional<SessionWriteCompletion> SingleWorkerSessionWriter::poll() {
    std::lock_guard lock{mutex_};
    auto result = completion_;
    completion_.reset();
    return result;
}

std::optional<SessionWriteCompletion> SingleWorkerSessionWriter::flush(SessionWriteRequest request) {
    std::unique_lock lock{mutex_};
    if (shutdown_requested_ || stopped_)
        return std::nullopt;

    const auto consume_latest_completion = [this] {
        auto result = latest_completed_;
        if (result.has_value() && completion_.has_value() && completion_->revision == result->revision &&
            completion_->status == result->status &&
            completion_->requested_snapshot_recoverable_after_failure ==
                result->requested_snapshot_recoverable_after_failure) {
            completion_.reset();
        }
        return result;
    };

    const SessionRevision requested_revision = request.revision;
    if (latest_completed_.has_value() && latest_completed_->revision > requested_revision)
        return consume_latest_completion();
    if (latest_completed_.has_value() && latest_completed_->revision == requested_revision &&
        latest_completed_->status == SessionSaveStatus::Saved && !active_revision_.has_value() &&
        !pending_.has_value()) {
        return consume_latest_completion();
    }

    if (completion_.has_value() && completion_->revision <= requested_revision)
        completion_.reset();
    if (latest_completed_.has_value() && latest_completed_->revision <= requested_revision)
        latest_completed_.reset();

    const bool requested_revision_is_active = active_revision_.has_value() && *active_revision_ == requested_revision;
    const bool newer_revision_is_active = active_revision_.has_value() && *active_revision_ > requested_revision;
    const bool newer_revision_is_pending = pending_.has_value() && pending_->revision > requested_revision;
    if (!requested_revision_is_active && !newer_revision_is_active && !newer_revision_is_pending) {
        if (!pending_.has_value() || pending_->revision <= requested_revision)
            pending_ = std::move(request);
        condition_.notify_all();
    }

    condition_.wait(lock, [this, requested_revision] {
        return stopped_ || (latest_completed_.has_value() && latest_completed_->revision >= requested_revision);
    });
    if (!latest_completed_.has_value() || latest_completed_->revision < requested_revision)
        return std::nullopt;
    return consume_latest_completion();
}

bool SingleWorkerSessionWriter::shutdown() {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    {
        std::lock_guard lock{mutex_};
        if (!shutdown_requested_) {
            shutdown_requested_ = true;
            condition_.notify_all();
        }
    }
    if (worker_.joinable())
        worker_.join();
    return true;
}

void SingleWorkerSessionWriter::runWorker() {
    std::unique_lock lock{mutex_};
    for (;;) {
        condition_.wait(lock, [this] { return shutdown_requested_ || pending_.has_value(); });
        if (!pending_.has_value()) {
            if (shutdown_requested_)
                break;
            continue;
        }

        SessionWriteRequest request = std::move(*pending_);
        pending_.reset();
        active_revision_ = request.revision;
        lock.unlock();
        const SessionWriteCompletion completion = saveAndReconcile(request);
        lock.lock();
        active_revision_.reset();
        completion_ = completion;
        latest_completed_ = completion;
        condition_.notify_all();
    }
    stopped_ = true;
    condition_.notify_all();
}

SessionWriteCompletion SingleWorkerSessionWriter::saveAndReconcile(const SessionWriteRequest& request) noexcept {
    SessionWriteCompletion completion{.revision = request.revision};
    try {
        completion.status =
            store_.save({.snapshot = request.snapshot, .replace_future_version = request.replace_future_version});
    } catch (...) {
        completion.status = SessionSaveStatus::Failed;
    }

    if (completion.status != SessionSaveStatus::Failed)
        return completion;

    try {
        const SessionLoadResult readback = store_.load();
        switch (readback.status) {
        case SessionLoadStatus::Loaded:
        case SessionLoadStatus::RecoveredFromBackup:
            completion.requested_snapshot_recoverable_after_failure = readback.snapshot == request.snapshot;
            break;
        case SessionLoadStatus::Missing:
        case SessionLoadStatus::InvalidUsingDefaults:
        case SessionLoadStatus::FutureVersion:
            completion.requested_snapshot_recoverable_after_failure = false;
            break;
        }
    } catch (...) {
        completion.requested_snapshot_recoverable_after_failure.reset();
    }
    return completion;
}

} // namespace music_player::m0
