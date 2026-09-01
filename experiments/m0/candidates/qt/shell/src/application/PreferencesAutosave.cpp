#include "m0/application/PreferencesAutosave.hpp"

#include <stdexcept>
#include <utility>

namespace music_player::m0 {

PreferencesAutosave::PreferencesAutosave(PreferencesWriter& writer, Preferences initial,
                                         PreferencesAutosaveOptions options)
    : writer_(writer), latest_(std::move(initial)), options_(std::move(options)) {
    if (!options_.now)
        throw std::invalid_argument{"preferences autosave clock is required"};
    if (options_.debounce < std::chrono::milliseconds::zero())
        throw std::invalid_argument{"preferences autosave debounce cannot be negative"};
}

bool PreferencesAutosave::schedule(const Preferences& preferences) {
    if (preferences == latest_)
        return false;

    latest_ = preferences;
    ++latest_revision_;
    dirty_ = true;
    due_ = options_.now() + options_.debounce;
    return true;
}

void PreferencesAutosave::scheduleRepair() {
    ++latest_revision_;
    dirty_ = true;
    due_ = options_.now();
}

void PreferencesAutosave::requestSaveNow() {
    poll();
    if (!dirty_)
        return;
    due_ = options_.now();
    if (active_revision_.has_value())
        return;
    tryStartDueWrite();
}

void PreferencesAutosave::poll() {
    if (const auto completion = writer_.poll(); completion.has_value()) {
        bool replaced_future_version = false;
        bool retry_after_completion = false;
        if (active_revision_.has_value() && *active_revision_ == completion->revision) {
            replaced_future_version = active_replaces_future_version_.value_or(false);
            retry_after_completion = retry_after_active_;
            active_revision_.reset();
            active_replaces_future_version_.reset();
            retry_after_active_ = false;
        }
        applyCompletion(*completion, replaced_future_version);
        if (retry_after_completion && dirty_ && completion->status == PreferencesSaveStatus::Failed)
            due_ = options_.now();
    }
    tryStartDueWrite();
}

PreferencesSaveStatus PreferencesAutosave::flush() {
    poll();
    if (!dirty_)
        return PreferencesSaveStatus::Saved;

    const PreferencesWriteRequest request = latestRequest();
    const auto completion = writer_.flush(request);
    if (!completion.has_value()) {
        notice_ = PreferencesAutosaveNotice::SaveFailed;
        return PreferencesSaveStatus::Failed;
    }

    active_revision_.reset();
    active_replaces_future_version_.reset();
    retry_after_active_ = false;
    applyCompletion(*completion, completion->revision == latest_revision_ && request.replace_future_version);
    return completion->status;
}

bool PreferencesAutosave::retry() {
    if (!dirty_)
        return false;
    due_ = options_.now();
    retry_after_active_ = active_revision_.has_value();
    return true;
}

bool PreferencesAutosave::confirmFutureVersionReplacement() {
    if (!dirty_)
        return false;
    replace_future_version_ = true;
    due_ = options_.now();
    return true;
}

bool PreferencesAutosave::dirty() const noexcept { return dirty_; }

bool PreferencesAutosave::writeActive() const noexcept { return active_revision_.has_value(); }

bool PreferencesAutosave::needsPolling() const noexcept {
    return active_revision_.has_value() || (dirty_ && due_.has_value());
}

PreferencesRevision PreferencesAutosave::latestRevision() const noexcept { return latest_revision_; }

PreferencesAutosaveNotice PreferencesAutosave::notice() const noexcept { return notice_; }

void PreferencesAutosave::tryStartDueWrite() {
    if (!dirty_ || active_revision_.has_value() || !due_.has_value() || options_.now() < *due_)
        return;

    const PreferencesWriteRequest request = latestRequest();
    switch (writer_.start(request)) {
    case PreferencesWriterCommand::Accepted:
        active_revision_ = latest_revision_;
        active_replaces_future_version_ = request.replace_future_version;
        due_.reset();
        break;
    case PreferencesWriterCommand::Busy:
        break;
    case PreferencesWriterCommand::Shutdown:
        due_.reset();
        notice_ = PreferencesAutosaveNotice::SaveFailed;
        break;
    }
}

void PreferencesAutosave::applyCompletion(const PreferencesWriteCompletion& completion,
                                          const bool replaced_future_version) {
    if (completion.revision != latest_revision_)
        return;

    switch (completion.status) {
    case PreferencesSaveStatus::Saved:
        dirty_ = false;
        due_.reset();
        replace_future_version_ = false;
        notice_ = PreferencesAutosaveNotice::None;
        break;
    case PreferencesSaveStatus::Failed:
        dirty_ = true;
        due_.reset();
        notice_ = PreferencesAutosaveNotice::SaveFailed;
        break;
    case PreferencesSaveStatus::BlockedByFutureVersion:
        dirty_ = true;
        if (replace_future_version_ && !replaced_future_version) {
            due_ = options_.now();
            notice_ = PreferencesAutosaveNotice::None;
        } else {
            due_.reset();
            notice_ = PreferencesAutosaveNotice::FutureVersionBlocked;
        }
        break;
    }
}

PreferencesWriteRequest PreferencesAutosave::latestRequest() const {
    return {.revision = latest_revision_, .preferences = latest_, .replace_future_version = replace_future_version_};
}

} // namespace music_player::m0
