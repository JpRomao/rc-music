#include "m0/application/PreferencesAutosave.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

using namespace std::chrono_literals;

class ManualClock final {
  public:
    [[nodiscard]] PreferencesAutosave::TimePoint now() const noexcept { return now_; }
    void advance(const std::chrono::milliseconds duration) noexcept { now_ += duration; }

  private:
    PreferencesAutosave::TimePoint now_{};
};

class RecordingWriter final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest request) override {
        if (shutdown_)
            return PreferencesWriterCommand::Shutdown;
        if (active_.has_value())
            return PreferencesWriterCommand::Busy;
        active_ = request;
        starts.push_back(std::move(request));
        return PreferencesWriterCommand::Accepted;
    }

    std::optional<PreferencesWriteCompletion> poll() override {
        if (completions.empty())
            return std::nullopt;
        auto completion = completions.front();
        completions.pop_front();
        if (active_.has_value() && active_->revision == completion.revision)
            active_.reset();
        return completion;
    }

    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        flushes.push_back(request);
        active_.reset();
        return PreferencesWriteCompletion{.revision = request.revision, .status = flush_status};
    }

    bool shutdown() override {
        shutdown_ = true;
        return true;
    }

    void complete(const PreferencesSaveStatus status) {
        REQUIRE(active_.has_value());
        completions.push_back({.revision = active_->revision, .status = status});
    }

    std::vector<PreferencesWriteRequest> starts;
    std::vector<PreferencesWriteRequest> flushes;
    std::deque<PreferencesWriteCompletion> completions;
    PreferencesSaveStatus flush_status{PreferencesSaveStatus::Saved};

  private:
    std::optional<PreferencesWriteRequest> active_;
    bool shutdown_{false};
};

Preferences changedPreferences() {
    return {.theme = ThemeMode::Dark,
            .motion = MotionMode::Reduced,
            .volume = 0.35,
            .muted = true,
            .skip_backward_seconds = 7,
            .skip_forward_seconds = 20,
            .close_behavior = CloseBehavior::Exit,
            .restore_last_session = false,
            .start_minimized = true,
            .queue_end_behavior = QueueEndBehavior::StopClearSelection};
}

PreferencesAutosave makeAutosave(RecordingWriter& writer, ManualClock& clock, const Preferences& initial = {}) {
    return PreferencesAutosave{writer, initial, {.debounce = 500ms, .now = [&clock] { return clock.now(); }}};
}

} // namespace

TEST_CASE("preferences autosave waits for the trailing debounce and writes the latest full snapshot", "[preferences]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences first = changedPreferences();
    Preferences latest = first;
    latest.volume = 0.61;

    REQUIRE(autosave.schedule(first));
    clock.advance(300ms);
    REQUIRE(autosave.schedule(latest));
    clock.advance(499ms);
    autosave.poll();
    CHECK(writer.starts.empty());

    clock.advance(1ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().revision == 2);
    CHECK(writer.starts.front().preferences == latest);
    CHECK_FALSE(writer.starts.front().replace_future_version);
    CHECK(autosave.writeActive());
    CHECK(autosave.dirty());

    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();
    CHECK_FALSE(autosave.writeActive());
    CHECK_FALSE(autosave.dirty());
    CHECK(autosave.notice() == PreferencesAutosaveNotice::None);
}

TEST_CASE("each queue mode creates a revision and survives later preference changes", "[preferences][queue-modes]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences latest;
    SECTION("repeat") { latest.repeat_queue_enabled = true; }
    SECTION("shuffle") { latest.shuffle_queue_enabled = true; }
    REQUIRE(autosave.schedule(latest));
    CHECK(autosave.latestRevision() == 1);
    latest.volume = 0.6;
    REQUIRE(autosave.schedule(latest));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().revision == 2);
    CHECK(writer.starts.front().preferences == latest);
}

TEST_CASE("window placement creates a revision and survives later preference changes",
          "[preferences][window-placement]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    const WindowPlacement placement{-1'920, 40, 960, 640};
    Preferences placement_only;
    placement_only.window_placement = placement;

    REQUIRE(autosave.schedule(placement_only));
    CHECK(autosave.dirty());
    CHECK(autosave.latestRevision() == 1);

    Preferences latest = placement_only;
    latest.theme = ThemeMode::Dark;
    REQUIRE(autosave.schedule(latest));
    CHECK(autosave.latestRevision() == 2);

    clock.advance(500ms);
    autosave.poll();

    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().revision == 2);
    CHECK(writer.starts.front().preferences == latest);
    REQUIRE(writer.starts.front().preferences.window_placement.has_value());
    CHECK(*writer.starts.front().preferences.window_placement == placement);
}

TEST_CASE("newer window placement remains dirty after an older revision completes", "[preferences][window-placement]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences first;
    first.window_placement = WindowPlacement{-1'920, 40, 960, 640};
    Preferences latest = first;
    const WindowPlacement latest_placement{120, 80, 1'200, 760};
    latest.window_placement = latest_placement;

    REQUIRE(autosave.schedule(first));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);

    REQUIRE(autosave.schedule(latest));
    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();

    CHECK(autosave.dirty());
    CHECK_FALSE(autosave.writeActive());
    CHECK(autosave.latestRevision() == 2);
    CHECK(writer.starts.size() == 1);

    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().revision == 2);
    REQUIRE(writer.starts.back().preferences.window_placement.has_value());
    CHECK(*writer.starts.back().preferences.window_placement == latest_placement);
}

TEST_CASE("a newer revision remains dirty while an older save finishes", "[preferences]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences first = changedPreferences();
    Preferences latest = first;
    latest.skip_forward_seconds = 77;

    REQUIRE(autosave.schedule(first));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);

    clock.advance(10ms);
    REQUIRE(autosave.schedule(latest));
    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();
    CHECK(autosave.dirty());
    CHECK_FALSE(autosave.writeActive());
    CHECK(writer.starts.size() == 1);

    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().revision == 2);
    CHECK(writer.starts.back().preferences == latest);
}

TEST_CASE("an older failed revision cannot cancel the newer revision debounce", "[preferences][regression]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences first = changedPreferences();
    Preferences latest = first;
    latest.skip_forward_seconds = 77;

    REQUIRE(autosave.schedule(first));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);

    REQUIRE(autosave.schedule(latest));
    writer.complete(PreferencesSaveStatus::Failed);
    autosave.poll();
    CHECK(autosave.dirty());

    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().revision == 2);
    CHECK(writer.starts.back().preferences == latest);
}

TEST_CASE("failed and future-version saves stay dirty until an explicit retry policy", "[preferences]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    CHECK_FALSE(autosave.needsPolling());
    REQUIRE(autosave.schedule(changedPreferences()));
    CHECK(autosave.needsPolling());
    clock.advance(500ms);
    autosave.poll();
    CHECK(autosave.needsPolling());
    writer.complete(PreferencesSaveStatus::Failed);
    autosave.poll();
    CHECK(autosave.dirty());
    CHECK_FALSE(autosave.needsPolling());
    CHECK(autosave.notice() == PreferencesAutosaveNotice::SaveFailed);

    REQUIRE(autosave.retry());
    CHECK(autosave.needsPolling());
    autosave.poll();
    REQUIRE(writer.starts.size() == 2);
    writer.complete(PreferencesSaveStatus::BlockedByFutureVersion);
    autosave.poll();
    CHECK(autosave.dirty());
    CHECK_FALSE(autosave.needsPolling());
    CHECK(autosave.notice() == PreferencesAutosaveNotice::FutureVersionBlocked);

    REQUIRE(autosave.confirmFutureVersionReplacement());
    CHECK(autosave.notice() == PreferencesAutosaveNotice::None);
    CHECK(autosave.needsPolling());
    autosave.poll();
    REQUIRE(writer.starts.size() == 3);
    CHECK(writer.starts.back().replace_future_version);
    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();
    CHECK_FALSE(autosave.dirty());
    CHECK_FALSE(autosave.needsPolling());
    CHECK(autosave.notice() == PreferencesAutosaveNotice::None);
}

TEST_CASE("retry remains actionable while a timed-out write is still active", "[preferences][regression]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    auto latest = Preferences{};
    latest.theme = ThemeMode::Dark;

    REQUIRE(autosave.schedule(latest));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    REQUIRE(autosave.writeActive());

    REQUIRE(autosave.retry());
    writer.complete(PreferencesSaveStatus::Failed);
    autosave.poll();

    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().revision == autosave.latestRevision());
    CHECK(writer.starts.back().preferences == latest);
    CHECK(autosave.writeActive());
}

TEST_CASE("future-version replacement waits behind an active non-replacing write", "[preferences][regression]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.schedule(changedPreferences()));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK_FALSE(writer.starts.front().replace_future_version);

    REQUIRE(autosave.confirmFutureVersionReplacement());
    writer.complete(PreferencesSaveStatus::BlockedByFutureVersion);
    autosave.poll();

    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().replace_future_version);
    CHECK(autosave.writeActive());
    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();
    CHECK_FALSE(autosave.dirty());
    CHECK(autosave.notice() == PreferencesAutosaveNotice::None);
}

TEST_CASE("exit coordination bypasses the debounce without waiting for storage", "[preferences]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    const Preferences changed = changedPreferences();

    REQUIRE(autosave.schedule(changed));
    autosave.requestSaveNow();

    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().preferences == changed);
    CHECK(autosave.writeActive());
}

TEST_CASE("exit coordination starts the newest revision after an older active save completes", "[preferences]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences first = changedPreferences();
    Preferences latest = first;
    latest.skip_backward_seconds = 9;

    REQUIRE(autosave.schedule(first));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    REQUIRE(autosave.schedule(latest));
    writer.complete(PreferencesSaveStatus::Saved);

    autosave.requestSaveNow();

    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().preferences == latest);
    CHECK(autosave.writeActive());
}

TEST_CASE("exit coordination keeps the newest revision urgent while an older save remains active",
          "[preferences][regression]") {
    RecordingWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    Preferences first = changedPreferences();
    Preferences latest = first;
    latest.skip_forward_seconds = 19;

    REQUIRE(autosave.schedule(first));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    REQUIRE(autosave.schedule(latest));

    autosave.requestSaveNow();
    CHECK(writer.starts.size() == 1);
    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();

    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().preferences == latest);
    CHECK(autosave.writeActive());
}

TEST_CASE("repair bypasses debounce and shutdown flush writes only a dirty latest revision", "[preferences]") {
    RecordingWriter writer;
    ManualClock clock;
    const Preferences recovered = changedPreferences();
    auto autosave = makeAutosave(writer, clock, recovered);

    autosave.scheduleRepair();
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().preferences == recovered);
    writer.complete(PreferencesSaveStatus::Saved);
    autosave.poll();
    CHECK_FALSE(autosave.dirty());

    CHECK_FALSE(autosave.schedule(recovered));
    CHECK(autosave.flush() == PreferencesSaveStatus::Saved);
    CHECK(writer.flushes.empty());

    Preferences latest = recovered;
    latest.skip_backward_seconds = 9;
    REQUIRE(autosave.schedule(latest));
    CHECK(autosave.flush() == PreferencesSaveStatus::Saved);
    REQUIRE(writer.flushes.size() == 1);
    CHECK(writer.flushes.front().preferences == latest);
    CHECK_FALSE(autosave.dirty());
}

} // namespace music_player::m0::test
