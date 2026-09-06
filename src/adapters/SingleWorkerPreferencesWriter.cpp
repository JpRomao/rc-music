#include "m0/adapters/SingleWorkerPreferencesWriter.hpp"

#include <utility>

namespace music_player::m0 {

SingleWorkerPreferencesWriter::SingleWorkerPreferencesWriter(PreferencesStore& store)
    : store_(store), worker_([this] { runWorker(); }) {}

SingleWorkerPreferencesWriter::~SingleWorkerPreferencesWriter() { static_cast<void>(shutdown()); }

PreferencesWriterCommand SingleWorkerPreferencesWriter::start(PreferencesWriteRequest request) {
    std::lock_guard lock{mutex_};
    if (shutdown_requested_ || stopped_)
        return PreferencesWriterCommand::Shutdown;
    if (pending_.has_value() || active_revision_.has_value())
        return PreferencesWriterCommand::Busy;
    if (completion_.has_value() && completion_->revision <= request.revision)
        completion_.reset();
    if (latest_completed_.has_value() && latest_completed_->revision <= request.revision)
        latest_completed_.reset();
    pending_ = std::move(request);
    condition_.notify_all();
    return PreferencesWriterCommand::Accepted;
}

std::optional<PreferencesWriteCompletion> SingleWorkerPreferencesWriter::poll() {
    std::lock_guard lock{mutex_};
    auto result = completion_;
    completion_.reset();
    return result;
}

std::optional<PreferencesWriteCompletion> SingleWorkerPreferencesWriter::flush(PreferencesWriteRequest request) {
    std::unique_lock lock{mutex_};
    if (shutdown_requested_ || stopped_)
        return std::nullopt;

    const PreferencesRevision requested_revision = request.revision;
    if (latest_completed_.has_value() && latest_completed_->revision > requested_revision)
        return latest_completed_;
    if (latest_completed_.has_value() && latest_completed_->revision == requested_revision &&
        latest_completed_->status == PreferencesSaveStatus::Saved && !active_revision_.has_value() &&
        !pending_.has_value()) {
        return latest_completed_;
    }

    if (completion_.has_value() && completion_->revision <= requested_revision)
        completion_.reset();
    if (latest_completed_.has_value() && latest_completed_->revision <= requested_revision)
        latest_completed_.reset();

    const bool requested_revision_is_active = active_revision_.has_value() && *active_revision_ == requested_revision;
    const bool newer_revision_is_active = active_revision_.has_value() && *active_revision_ > requested_revision;
    const bool newer_revision_is_pending = pending_.has_value() && pending_->revision > requested_revision;
    if (!requested_revision_is_active && !newer_revision_is_active && !newer_revision_is_pending) {
        if (!pending_.has_value() || pending_->revision <= requested_revision) {
            pending_ = std::move(request);
        }
        condition_.notify_all();
    }

    condition_.wait(lock, [this, requested_revision] {
        return stopped_ || (latest_completed_.has_value() && latest_completed_->revision >= requested_revision);
    });
    if (!latest_completed_.has_value() || latest_completed_->revision < requested_revision)
        return std::nullopt;
    return latest_completed_;
}

bool SingleWorkerPreferencesWriter::shutdown() {
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

void SingleWorkerPreferencesWriter::runWorker() {
    std::unique_lock lock{mutex_};
    for (;;) {
        condition_.wait(lock, [this] { return shutdown_requested_ || pending_.has_value(); });
        if (!pending_.has_value()) {
            if (shutdown_requested_)
                break;
            continue;
        }

        PreferencesWriteRequest request = std::move(*pending_);
        pending_.reset();
        active_revision_ = request.revision;
        lock.unlock();
        const PreferencesSaveStatus status = save(request);
        lock.lock();
        active_revision_.reset();
        const PreferencesWriteCompletion completion{.revision = request.revision, .status = status};
        completion_ = completion;
        latest_completed_ = completion;
        condition_.notify_all();
    }
    stopped_ = true;
    condition_.notify_all();
}

PreferencesSaveStatus SingleWorkerPreferencesWriter::save(const PreferencesWriteRequest& request) noexcept {
    try {
        return store_.save(
            {.preferences = request.preferences, .replace_future_version = request.replace_future_version});
    } catch (...) {
        return PreferencesSaveStatus::Failed;
    }
}

} // namespace music_player::m0
