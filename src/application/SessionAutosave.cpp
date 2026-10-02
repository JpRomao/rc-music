#include "m0/application/SessionAutosave.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

bool validSaveStatus(const SessionSaveStatus status) {
    switch (status) {
    case SessionSaveStatus::Saved:
    case SessionSaveStatus::Failed:
    case SessionSaveStatus::BlockedByFutureVersion:
        return true;
    }
    return false;
}

} // namespace

SessionAutosave::SessionAutosave(SessionWriter& writer, SessionSnapshot initial, SessionAutosaveOptions options)
    : writer_(writer), latest_(std::move(initial)), options_(std::move(options)) {
    if (!isValidSessionSnapshot(latest_))
        throw std::invalid_argument{"session autosave initial snapshot is invalid"};
    if (!options_.now)
        throw std::invalid_argument{"session autosave clock is required"};
    if (options_.structural_debounce < std::chrono::milliseconds::zero())
        throw std::invalid_argument{"session autosave structural debounce cannot be negative"};
    if (options_.maximum_dirty_age < options_.structural_debounce)
        throw std::invalid_argument{"session autosave maximum dirty age cannot be below structural debounce"};
    if (options_.position_checkpoint <= std::chrono::milliseconds::zero())
        throw std::invalid_argument{"session autosave position checkpoint must be positive"};
}

bool SessionAutosave::scheduleStructural(const SessionSnapshot& snapshot) {
    if (!isValidSessionSnapshot(snapshot) || snapshot == latest_)
        return false;

    // Prepare allocation and clock lookup before replacing any authoritative
    // state. A failed copy must not leave a partially updated latest snapshot.
    SessionSnapshot replacement = snapshot;
    const auto changed_at = automatic_rearming_suspended_ ? TimePoint{} : options_.now();
    latest_ = std::move(replacement);
    ++latest_revision_;
    dirty_ = true;
    if (!automatic_rearming_suspended_)
        armStructuralWrite(changed_at);
    return true;
}

bool SessionAutosave::observeConfirmedPosition(const std::int64_t position_ms) {
    if (!positionIsValid(position_ms) || latest_.position_ms == position_ms)
        return false;

    latest_.position_ms = position_ms;
    ++latest_revision_;
    dirty_ = true;
    if (!automatic_rearming_suspended_ && !position_due_.has_value())
        position_due_ = options_.now() + options_.position_checkpoint;
    return true;
}

bool SessionAutosave::checkpointConfirmedPosition(const std::int64_t position_ms) {
    if (!positionIsValid(position_ms))
        return false;

    if (latest_.position_ms != position_ms) {
        latest_.position_ms = position_ms;
        ++latest_revision_;
        dirty_ = true;
    }
    if (!dirty_)
        return false;

    if (!automatic_rearming_suspended_)
        immediate_due_ = options_.now();
    return true;
}

void SessionAutosave::scheduleRepair() {
    ++latest_revision_;
    dirty_ = true;
    if (!automatic_rearming_suspended_)
        immediate_due_ = options_.now();
}

void SessionAutosave::requestSaveNow() {
    poll();
    if (!dirty_)
        return;

    automatic_rearming_suspended_ = false;
    immediate_due_ = options_.now();
    if (!active_revision_.has_value())
        tryStartDueWrite();
}

void SessionAutosave::poll() {
    if (const auto completion = writer_.poll(); completion.has_value()) {
        if (active_revision_.has_value() && *active_revision_ == completion->revision) {
            const bool replaced_future_version = active_replaces_future_version_.value_or(false);
            const bool retry_after_completion = retry_after_active_;
            active_revision_.reset();
            active_replaces_future_version_.reset();
            retry_after_active_ = false;
            const auto matching =
                validSaveStatus(completion->status)
                    ? *completion
                    : SessionWriteCompletion{.revision = completion->revision, .status = SessionSaveStatus::Failed};
            applyCompletion(matching, replaced_future_version);
            if (retry_after_completion && dirty_ && matching.revision == latest_revision_ &&
                matching.status == SessionSaveStatus::Failed) {
                automatic_rearming_suspended_ = false;
                immediate_due_ = options_.now();
            }
        }
    }
    tryStartDueWrite();
}

SessionSaveStatus SessionAutosave::flush() {
    poll();
    if (!dirty_)
        return SessionSaveStatus::Saved;

    const SessionWriteRequest request = latestRequest();
    const auto completion = writer_.flush(request);
    active_revision_.reset();
    active_replaces_future_version_.reset();
    retry_after_active_ = false;
    if (!completion.has_value() || completion->revision != request.revision || !validSaveStatus(completion->status)) {
        applyCompletion({.revision = request.revision, .status = SessionSaveStatus::Failed}, false);
        return SessionSaveStatus::Failed;
    }

    applyCompletion(*completion, completion->revision == request.revision && request.replace_future_version);
    return completion->status;
}

bool SessionAutosave::retry() {
    if (!dirty_)
        return false;

    automatic_rearming_suspended_ = false;
    immediate_due_ = options_.now();
    retry_after_active_ = active_revision_.has_value();
    return true;
}

bool SessionAutosave::confirmFutureVersionReplacement() {
    if (!dirty_)
        return false;

    if (!replace_future_version_) {
        replace_future_version_ = true;
        ++latest_revision_;
    }
    automatic_rearming_suspended_ = false;
    immediate_due_ = options_.now();
    retry_after_active_ = active_revision_.has_value();
    notice_ = SessionAutosaveNotice::None;
    return true;
}

bool SessionAutosave::dirty() const noexcept { return dirty_; }

bool SessionAutosave::writeActive() const noexcept { return active_revision_.has_value(); }

bool SessionAutosave::needsPolling() const noexcept {
    return active_revision_.has_value() || (dirty_ && nextWakeup().has_value());
}

SessionRevision SessionAutosave::latestRevision() const noexcept { return latest_revision_; }

SessionRevision SessionAutosave::savedRevision() const noexcept { return saved_revision_; }

SessionAutosaveNotice SessionAutosave::notice() const noexcept { return notice_; }

std::optional<SessionAutosave::TimePoint> SessionAutosave::nextWakeup() const noexcept {
    std::optional<TimePoint> wakeup;
    const auto include = [&wakeup](const std::optional<TimePoint>& candidate) {
        if (candidate.has_value() && (!wakeup.has_value() || *candidate < *wakeup))
            wakeup = candidate;
    };
    include(immediate_due_);
    include(structural_due_);
    include(position_due_);
    return wakeup;
}

bool SessionAutosave::positionIsValid(const std::int64_t position_ms) const noexcept {
    return position_ms >= 0 && (latest_.queue.current_occurrence_id.has_value() || position_ms == 0);
}

void SessionAutosave::armStructuralWrite(const TimePoint now) {
    if (!first_structural_dirty_at_.has_value())
        first_structural_dirty_at_ = now;

    const TimePoint trailing_due = now + options_.structural_debounce;
    const TimePoint maximum_due = *first_structural_dirty_at_ + options_.maximum_dirty_age;
    structural_due_ = std::min(trailing_due, maximum_due);
}

void SessionAutosave::tryStartDueWrite() {
    const auto wakeup = nextWakeup();
    if (!dirty_ || active_revision_.has_value() || automatic_rearming_suspended_ || !wakeup.has_value() ||
        options_.now() < *wakeup) {
        return;
    }

    const SessionWriteRequest request = latestRequest();
    switch (writer_.start(request)) {
    case SessionWriterCommand::Accepted:
        active_revision_ = request.revision;
        active_replaces_future_version_ = request.replace_future_version;
        immediate_due_.reset();
        structural_due_.reset();
        position_due_.reset();
        first_structural_dirty_at_.reset();
        break;
    case SessionWriterCommand::Busy:
        break;
    case SessionWriterCommand::Shutdown:
        immediate_due_.reset();
        structural_due_.reset();
        position_due_.reset();
        first_structural_dirty_at_.reset();
        automatic_rearming_suspended_ = true;
        notice_ = SessionAutosaveNotice::SaveFailed;
        break;
    }
}

void SessionAutosave::applyCompletion(const SessionWriteCompletion& completion, const bool replaced_future_version) {
    // Callers only pass an exact matching active/promptly flushed revision.
    // This acknowledges a saved prefix while newer snapshots remain dirty.
    if (completion.status == SessionSaveStatus::Saved)
        saved_revision_ = std::max(saved_revision_, completion.revision);
    if (completion.revision != latest_revision_)
        return;

    switch (completion.status) {
    case SessionSaveStatus::Saved:
        dirty_ = false;
        immediate_due_.reset();
        structural_due_.reset();
        position_due_.reset();
        first_structural_dirty_at_.reset();
        replace_future_version_ = false;
        automatic_rearming_suspended_ = false;
        notice_ = SessionAutosaveNotice::None;
        break;
    case SessionSaveStatus::Failed:
        dirty_ = true;
        immediate_due_.reset();
        structural_due_.reset();
        position_due_.reset();
        first_structural_dirty_at_.reset();
        automatic_rearming_suspended_ = true;
        notice_ = completion.requested_snapshot_recoverable_after_failure == std::optional<bool>{true}
                      ? SessionAutosaveNotice::SaveIncomplete
                      : SessionAutosaveNotice::SaveFailed;
        break;
    case SessionSaveStatus::BlockedByFutureVersion:
        dirty_ = true;
        first_structural_dirty_at_.reset();
        if (replace_future_version_ && !replaced_future_version) {
            automatic_rearming_suspended_ = false;
            immediate_due_ = options_.now();
            notice_ = SessionAutosaveNotice::None;
        } else {
            immediate_due_.reset();
            structural_due_.reset();
            position_due_.reset();
            automatic_rearming_suspended_ = true;
            notice_ = SessionAutosaveNotice::FutureVersionBlocked;
        }
        break;
    }
}

SessionWriteRequest SessionAutosave::latestRequest() const {
    return {.revision = latest_revision_, .snapshot = latest_, .replace_future_version = replace_future_version_};
}

} // namespace music_player::m0
