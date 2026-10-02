#include "m0/application/SessionAutosave.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

using namespace std::chrono_literals;

class ManualClock final {
  public:
    [[nodiscard]] SessionAutosave::TimePoint now() const noexcept { return now_; }
    void advance(const std::chrono::milliseconds duration) noexcept { now_ += duration; }

  private:
    SessionAutosave::TimePoint now_{};
};

class RecordingSessionWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest request) override {
        if (shutdown_)
            return SessionWriterCommand::Shutdown;
        if (active_.has_value())
            return SessionWriterCommand::Busy;
        active_ = request;
        starts.push_back(std::move(request));
        return SessionWriterCommand::Accepted;
    }

    std::optional<SessionWriteCompletion> poll() override {
        if (completions.empty())
            return std::nullopt;
        auto completion = completions.front();
        completions.pop_front();
        if (active_.has_value() && active_->revision == completion.revision)
            active_.reset();
        return completion;
    }

    std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override {
        flushes.push_back(request);
        active_.reset();
        if (!flush_returns_completion)
            return std::nullopt;
        return SessionWriteCompletion{
            .revision = flush_completion_revision.value_or(request.revision),
            .status = flush_status,
            .requested_snapshot_recoverable_after_failure = flush_recoverable_after_failure,
        };
    }

    bool shutdown() override {
        shutdown_ = true;
        return true;
    }

    void complete(const SessionSaveStatus status, std::optional<bool> recoverable_after_failure = std::nullopt) {
        REQUIRE(active_.has_value());
        completions.push_back({.revision = active_->revision,
                               .status = status,
                               .requested_snapshot_recoverable_after_failure = recoverable_after_failure});
    }

    std::vector<SessionWriteRequest> starts;
    std::vector<SessionWriteRequest> flushes;
    std::deque<SessionWriteCompletion> completions;
    SessionSaveStatus flush_status{SessionSaveStatus::Saved};
    std::optional<bool> flush_recoverable_after_failure{};
    bool flush_returns_completion{true};
    std::optional<SessionRevision> flush_completion_revision{};

  private:
    std::optional<SessionWriteRequest> active_;
    bool shutdown_{false};
};

class RevisionSensitiveFlushWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest request) override {
        if (active_.has_value())
            return SessionWriterCommand::Busy;
        active_ = request;
        starts.push_back(std::move(request));
        return SessionWriterCommand::Accepted;
    }

    std::optional<SessionWriteCompletion> poll() override { return std::nullopt; }

    std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override {
        flushes.push_back(request);
        if (!active_.has_value())
            return SessionWriteCompletion{.revision = request.revision, .status = SessionSaveStatus::Saved};

        const SessionWriteRequest completed_active = *active_;
        active_.reset();
        if (completed_active.revision == request.revision) {
            return SessionWriteCompletion{.revision = completed_active.revision,
                                          .status = SessionSaveStatus::BlockedByFutureVersion};
        }

        replacement_writes.push_back(request);
        return SessionWriteCompletion{.revision = request.revision, .status = SessionSaveStatus::Saved};
    }

    bool shutdown() override { return true; }

    std::vector<SessionWriteRequest> starts;
    std::vector<SessionWriteRequest> flushes;
    std::vector<SessionWriteRequest> replacement_writes;

  private:
    std::optional<SessionWriteRequest> active_;
};

[[nodiscard]] SessionSnapshot snapshot(const std::uint64_t occurrence_id, const std::int64_t position_ms = 0) {
    return {
        .queue =
            {
                .items = {{{occurrence_id}, std::filesystem::path{LR"(C:\Música\Abertura 日本語.mp3)"}}},
                .current_occurrence_id = LocalQueueOccurrenceId{occurrence_id},
            },
        .position_ms = position_ms,
    };
}

[[nodiscard]] SessionAutosaveOptions options(ManualClock& clock) {
    return {
        .structural_debounce = 500ms,
        .maximum_dirty_age = 5s,
        .position_checkpoint = 10s,
        .now = [&clock] { return clock.now(); },
    };
}

[[nodiscard]] SessionAutosave makeAutosave(RecordingSessionWriter& writer, ManualClock& clock,
                                           SessionSnapshot initial = snapshot(1)) {
    return SessionAutosave{writer, std::move(initial), options(clock)};
}

} // namespace

TEST_CASE("session autosave rejects an invalid initial snapshot and invalid scheduling options",
          "[m0][qt][core][session][FR-043][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;

    auto invalid = snapshot(1);
    invalid.position_ms = -1;
    CHECK_THROWS_AS((SessionAutosave{writer, invalid, options(clock)}), std::invalid_argument);

    auto invalid_options = options(clock);
    SECTION("missing clock") { invalid_options.now = {}; }
    SECTION("negative structural debounce") { invalid_options.structural_debounce = -1ms; }
    SECTION("maximum dirty age below debounce") { invalid_options.maximum_dirty_age = 499ms; }
    SECTION("non-positive position checkpoint") { invalid_options.position_checkpoint = 0ms; }
    CHECK_THROWS_AS((SessionAutosave{writer, snapshot(1), invalid_options}), std::invalid_argument);
}

TEST_CASE("invalid structural snapshots and confirmed positions do not mutate autosave state",
          "[m0][qt][core][session][FR-043][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    auto invalid = snapshot(2);
    invalid.position_ms = -1;

    CHECK_FALSE(autosave.scheduleStructural(invalid));
    CHECK_FALSE(autosave.observeConfirmedPosition(-1));
    CHECK_FALSE(autosave.checkpointConfirmedPosition(-1));
    CHECK(autosave.latestRevision() == 0);
    CHECK_FALSE(autosave.dirty());
    CHECK_FALSE(autosave.nextWakeup().has_value());

    RecordingSessionWriter empty_writer;
    SessionAutosave empty{empty_writer, SessionSnapshot{}, options(clock)};
    CHECK_FALSE(empty.observeConfirmedPosition(1));
    CHECK_FALSE(empty.checkpointConfirmedPosition(1));
    CHECK(empty.latestRevision() == 0);
    CHECK_FALSE(empty.dirty());
}

TEST_CASE("structural session changes use trailing debounce bounded by maximum dirty age",
          "[m0][qt][core][session][FR-043][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));

    for (std::uint64_t occurrence_id = 3; occurrence_id <= 14; ++occurrence_id) {
        clock.advance(400ms);
        REQUIRE(autosave.scheduleStructural(snapshot(occurrence_id)));
    }
    REQUIRE(autosave.nextWakeup().has_value());
    CHECK(*autosave.nextWakeup() == clock.now() + 200ms);

    clock.advance(199ms);
    autosave.poll();
    CHECK(writer.starts.empty());
    clock.advance(1ms);
    autosave.poll();

    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().revision == 13);
    CHECK(writer.starts.front().snapshot == snapshot(14));
    CHECK_FALSE(writer.starts.front().replace_future_version);
}

TEST_CASE("the first confirmed position tick arms one checkpoint that later ticks do not postpone",
          "[m0][qt][core][session][FR-043]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.observeConfirmedPosition(100));
    REQUIRE(autosave.nextWakeup().has_value());
    const auto first_checkpoint = *autosave.nextWakeup();
    clock.advance(9s);
    REQUIRE(autosave.observeConfirmedPosition(900));
    CHECK(autosave.nextWakeup() == std::optional<SessionAutosave::TimePoint>{first_checkpoint});

    clock.advance(999ms);
    autosave.poll();
    CHECK(writer.starts.empty());
    clock.advance(1ms);
    autosave.poll();

    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().revision == 2);
    CHECK(writer.starts.front().snapshot == snapshot(1, 900));
}

TEST_CASE("structural debounce cannot hide an earlier confirmed-position checkpoint",
          "[m0][qt][core][session][FR-043][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.observeConfirmedPosition(100));
    const auto position_checkpoint = autosave.nextWakeup();
    REQUIRE(position_checkpoint.has_value());

    clock.advance(9'400ms);
    REQUIRE(autosave.scheduleStructural(snapshot(2, 100)));
    clock.advance(200ms);
    REQUIRE(autosave.scheduleStructural(snapshot(3, 100)));

    CHECK(autosave.nextWakeup() == position_checkpoint);
    clock.advance(399ms);
    autosave.poll();
    CHECK(writer.starts.empty());
    clock.advance(1ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().snapshot == snapshot(3, 100));
}

TEST_CASE("a confirmed position checkpoint makes the newest position immediately eligible",
          "[m0][qt][core][session][FR-043]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.observeConfirmedPosition(100));
    clock.advance(100ms);
    REQUIRE(autosave.checkpointConfirmedPosition(200));
    autosave.poll();

    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().revision == 2);
    CHECK(writer.starts.front().snapshot == snapshot(1, 200));
}

TEST_CASE("a confirmed checkpoint makes an unchanged dirty position immediately eligible",
          "[m0][qt][core][session][FR-043][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.observeConfirmedPosition(100));
    clock.advance(100ms);
    REQUIRE(autosave.checkpointConfirmedPosition(100));
    autosave.poll();

    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().snapshot == snapshot(1, 100));
}

TEST_CASE("a newer position remains dirty when an older structural save succeeds",
          "[m0][qt][core][session][FR-043][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2, 20)));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    REQUIRE(autosave.observeConfirmedPosition(30));

    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK(autosave.dirty());
    CHECK_FALSE(autosave.writeActive());
    CHECK(writer.starts.size() == 1);
    CHECK(autosave.nextWakeup() == std::optional<SessionAutosave::TimePoint>{clock.now() + 10s});
}

TEST_CASE("an older failed revision cannot suspend the newer position checkpoint",
          "[m0][qt][core][session][NFR-REL-003][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2, 20)));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(autosave.observeConfirmedPosition(30));
    const auto next_checkpoint = autosave.nextWakeup();
    writer.complete(SessionSaveStatus::Failed, true);
    autosave.poll();

    CHECK(autosave.notice() == SessionAutosaveNotice::None);
    CHECK(autosave.nextWakeup() == next_checkpoint);
    clock.advance(10s);
    autosave.poll();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().snapshot == snapshot(2, 30));
}

TEST_CASE("an exactly recoverable failed save remains dirty and reports incomplete durability",
          "[m0][qt][core][session][FR-044][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2, 20)));
    clock.advance(500ms);
    autosave.poll();
    writer.complete(SessionSaveStatus::Failed, true);
    autosave.poll();

    CHECK(autosave.dirty());
    CHECK_FALSE(autosave.needsPolling());
    CHECK_FALSE(autosave.nextWakeup().has_value());
    CHECK(autosave.notice() == SessionAutosaveNotice::SaveIncomplete);

    REQUIRE(autosave.observeConfirmedPosition(30));
    REQUIRE(autosave.checkpointConfirmedPosition(40));
    clock.advance(20s);
    autosave.poll();
    CHECK(writer.starts.size() == 1);
    CHECK_FALSE(autosave.nextWakeup().has_value());

    autosave.requestSaveNow();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().snapshot == snapshot(2, 40));
}

TEST_CASE("unconfirmed failed saves report SaveFailed and suspend automatic rearming",
          "[m0][qt][core][session][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    clock.advance(500ms);
    autosave.poll();
    SECTION("readback differs") { writer.complete(SessionSaveStatus::Failed, false); }
    SECTION("readback is unavailable") { writer.complete(SessionSaveStatus::Failed); }
    autosave.poll();

    CHECK(autosave.dirty());
    CHECK(autosave.notice() == SessionAutosaveNotice::SaveFailed);
    REQUIRE(autosave.scheduleStructural(snapshot(3)));
    clock.advance(20s);
    autosave.poll();
    CHECK(writer.starts.size() == 1);
    CHECK_FALSE(autosave.nextWakeup().has_value());
}

TEST_CASE("future-version blocking stays dirty until explicit replacement and preserves authorization on retry",
          "[m0][qt][core][session][FR-044][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2, 20)));
    clock.advance(500ms);
    autosave.poll();
    writer.complete(SessionSaveStatus::BlockedByFutureVersion);
    autosave.poll();
    CHECK(autosave.notice() == SessionAutosaveNotice::FutureVersionBlocked);
    CHECK_FALSE(autosave.needsPolling());

    REQUIRE(autosave.observeConfirmedPosition(30));
    clock.advance(20s);
    autosave.poll();
    CHECK(writer.starts.size() == 1);

    REQUIRE(autosave.confirmFutureVersionReplacement());
    CHECK(autosave.notice() == SessionAutosaveNotice::None);
    autosave.poll();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().snapshot == snapshot(2, 30));
    CHECK(writer.starts.back().replace_future_version);
    writer.complete(SessionSaveStatus::Failed, true);
    autosave.poll();
    CHECK(autosave.notice() == SessionAutosaveNotice::SaveIncomplete);

    REQUIRE(autosave.retry());
    autosave.poll();
    REQUIRE(writer.starts.size() == 3);
    CHECK(writer.starts.back().revision == autosave.latestRevision());
    CHECK(writer.starts.back().replace_future_version);
}

TEST_CASE("future-version confirmation waits behind an active non-replacing session write",
          "[m0][qt][core][session][FR-044][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2, 20)));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK_FALSE(writer.starts.front().replace_future_version);

    REQUIRE(autosave.confirmFutureVersionReplacement());
    writer.complete(SessionSaveStatus::BlockedByFutureVersion);
    autosave.poll();

    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().revision == autosave.latestRevision());
    CHECK(writer.starts.back().replace_future_version);
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK_FALSE(autosave.dirty());
    CHECK(autosave.notice() == SessionAutosaveNotice::None);
}

TEST_CASE("future-version confirmation gives a flush behind an active non-replacing write a distinct revision",
          "[m0][qt][core][session][FR-044][NFR-REL-003][regression]") {
    RevisionSensitiveFlushWriter writer;
    ManualClock clock;
    SessionAutosave autosave{writer, snapshot(1), options(clock)};

    const auto changed = snapshot(2, 20);
    REQUIRE(autosave.scheduleStructural(changed));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    const SessionRevision non_replacing_revision = writer.starts.front().revision;
    CHECK_FALSE(writer.starts.front().replace_future_version);

    REQUIRE(autosave.confirmFutureVersionReplacement());
    const SessionRevision replacing_revision = autosave.latestRevision();
    CHECK(replacing_revision == non_replacing_revision + 1);
    REQUIRE(autosave.confirmFutureVersionReplacement());
    CHECK(autosave.latestRevision() == replacing_revision);

    CHECK(autosave.flush() == SessionSaveStatus::Saved);
    REQUIRE(writer.flushes.size() == 1);
    CHECK(writer.flushes.front().revision == replacing_revision);
    CHECK(writer.flushes.front().snapshot == changed);
    CHECK(writer.flushes.front().replace_future_version);
    REQUIRE(writer.replacement_writes.size() == 1);
    CHECK(writer.replacement_writes.front().revision == writer.flushes.front().revision);
    CHECK(writer.replacement_writes.front().snapshot == writer.flushes.front().snapshot);
    CHECK(writer.replacement_writes.front().replace_future_version == writer.flushes.front().replace_future_version);
    CHECK_FALSE(autosave.dirty());
    CHECK(autosave.notice() == SessionAutosaveNotice::None);
}

TEST_CASE("save-now keeps a newer confirmed position urgent behind an active write",
          "[m0][qt][core][session][FR-043][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);

    REQUIRE(autosave.scheduleStructural(snapshot(2, 20)));
    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    REQUIRE(autosave.observeConfirmedPosition(30));

    autosave.requestSaveNow();
    CHECK(writer.starts.size() == 1);
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();

    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().snapshot == snapshot(2, 30));
}

TEST_CASE("repair and flush persist only a dirty latest session and retain failure state",
          "[m0][qt][core][session][FR-044][NFR-REL-003]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    const auto recovered = snapshot(7, 700);
    auto autosave = makeAutosave(writer, clock, recovered);

    autosave.scheduleRepair();
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().snapshot == recovered);
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK_FALSE(autosave.dirty());

    CHECK(autosave.flush() == SessionSaveStatus::Saved);
    CHECK(writer.flushes.empty());
    REQUIRE(autosave.observeConfirmedPosition(710));
    CHECK(autosave.flush() == SessionSaveStatus::Saved);
    REQUIRE(writer.flushes.size() == 1);
    CHECK(writer.flushes.front().snapshot == snapshot(7, 710));
    CHECK_FALSE(autosave.dirty());

    REQUIRE(autosave.observeConfirmedPosition(720));
    writer.flush_status = SessionSaveStatus::Failed;
    writer.flush_recoverable_after_failure = true;
    CHECK(autosave.flush() == SessionSaveStatus::Failed);
    CHECK(autosave.dirty());
    CHECK(autosave.notice() == SessionAutosaveNotice::SaveIncomplete);
    CHECK_FALSE(autosave.nextWakeup().has_value());
}

TEST_CASE("session saved revision acknowledges an active save even behind a newer snapshot",
          "[m0][qt][core][session][FR-043][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    CHECK(autosave.savedRevision() == 0);
    CHECK(autosave.flush() == SessionSaveStatus::Saved);
    CHECK(autosave.savedRevision() == 0);
    CHECK(writer.flushes.empty());

    auto first = snapshot(2);
    first.download_import_receipts = {{"download-1", std::filesystem::path{LR"(C:\Music\first.mp3)"}}};
    REQUIRE(autosave.scheduleStructural(first));
    autosave.requestSaveNow();
    REQUIRE(writer.starts.size() == 1);
    auto newer = snapshot(3);
    newer.download_import_receipts = first.download_import_receipts;
    newer.download_import_receipts.push_back({"download-2", std::filesystem::path{LR"(C:\Music\second.mp3)"}});
    REQUIRE(autosave.scheduleStructural(newer));
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK(autosave.savedRevision() == 1);
    CHECK(autosave.latestRevision() == 2);
    CHECK(autosave.dirty());
    CHECK(writer.starts.front().snapshot.download_import_receipts == first.download_import_receipts);

    autosave.requestSaveNow();
    REQUIRE(writer.starts.size() == 2);
    CHECK(writer.starts.back().snapshot.download_import_receipts == newer.download_import_receipts);
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK(autosave.savedRevision() == 2);
    CHECK_FALSE(autosave.dirty());
}

TEST_CASE("session autosave ignores a saved completion for a revision never submitted",
          "[m0][qt][core][session][FR-043][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    writer.completions.push_back({.revision = autosave.latestRevision(), .status = SessionSaveStatus::Saved});
    autosave.poll();
    CHECK(autosave.savedRevision() == 0);
    CHECK(autosave.dirty());
    CHECK(writer.starts.empty());
    CHECK(autosave.notice() == SessionAutosaveNotice::None);

    clock.advance(500ms);
    autosave.poll();
    REQUIRE(writer.starts.size() == 1);
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK(autosave.savedRevision() == 1);
    CHECK_FALSE(autosave.dirty());
}

TEST_CASE("session autosave ignores stale or future completion while retaining the active write",
          "[m0][qt][core][session][FR-043][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    autosave.requestSaveNow();
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    REQUIRE(autosave.scheduleStructural(snapshot(3)));
    autosave.requestSaveNow();
    REQUIRE(writer.starts.size() == 2);
    SECTION("duplicate old completion") {
        writer.completions.push_back({.revision = 1, .status = SessionSaveStatus::Saved});
    }
    SECTION("never-submitted future completion") {
        writer.completions.push_back({.revision = 3, .status = SessionSaveStatus::Saved});
    }
    autosave.poll();
    CHECK(autosave.savedRevision() == 1);
    CHECK(autosave.writeActive());
    CHECK(autosave.dirty());
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    CHECK(autosave.savedRevision() == 2);
    CHECK_FALSE(autosave.writeActive());
    CHECK_FALSE(autosave.dirty());
}

TEST_CASE("session failed or blocked writes never advance acknowledged durability",
          "[m0][qt][core][session][FR-043][FR-044][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    autosave.requestSaveNow();
    writer.complete(SessionSaveStatus::Saved);
    autosave.poll();
    REQUIRE(autosave.scheduleStructural(snapshot(3)));
    autosave.requestSaveNow();
    SECTION("failed despite exact readback") { writer.complete(SessionSaveStatus::Failed, true); }
    SECTION("future-version blocked") { writer.complete(SessionSaveStatus::BlockedByFutureVersion); }
    SECTION("unknown save status") { writer.complete(static_cast<SessionSaveStatus>(99)); }
    autosave.poll();
    CHECK(autosave.savedRevision() == 1);
    CHECK(autosave.dirty());
}

TEST_CASE("session flush acknowledges only its exact requested saved revision",
          "[m0][qt][core][session][FR-043][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    CHECK(autosave.savedRevision() == 0);
    CHECK(autosave.flush() == SessionSaveStatus::Saved);
    CHECK(autosave.savedRevision() == 1);
    CHECK_FALSE(autosave.dirty());
}

TEST_CASE("session malformed or failed flush cannot acknowledge durability or clean dirty state",
          "[m0][qt][core][session][FR-043][FR-044][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    SECTION("wrong newer revision") { writer.flush_completion_revision = 2; }
    SECTION("wrong older revision") { writer.flush_completion_revision = 0; }
    SECTION("unknown status") { writer.flush_status = static_cast<SessionSaveStatus>(99); }
    SECTION("failed with exact readback") {
        writer.flush_status = SessionSaveStatus::Failed;
        writer.flush_recoverable_after_failure = true;
    }
    SECTION("no completion") { writer.flush_returns_completion = false; }
    CHECK(autosave.flush() == SessionSaveStatus::Failed);
    CHECK(autosave.savedRevision() == 0);
    CHECK(autosave.dirty());
}

TEST_CASE("session polling cannot convert a duplicate of a failed flush into a saved acknowledgement",
          "[m0][qt][core][session][FR-043][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    auto autosave = makeAutosave(writer, clock);
    REQUIRE(autosave.scheduleStructural(snapshot(2)));
    writer.flush_status = SessionSaveStatus::Failed;
    CHECK(autosave.flush() == SessionSaveStatus::Failed);
    writer.completions.push_back({.revision = 1, .status = SessionSaveStatus::Saved});
    autosave.poll();
    CHECK(autosave.savedRevision() == 0);
    CHECK(autosave.dirty());
    CHECK(autosave.notice() == SessionAutosaveNotice::SaveFailed);
}

TEST_CASE("session structural preparation failure preserves the previous snapshot and revision",
          "[m0][qt][core][session][FR-043][FR-051][regression]") {
    RecordingSessionWriter writer;
    ManualClock clock;
    bool fail_clock = false;
    auto scheduling = options(clock);
    scheduling.now = [&] {
        if (fail_clock)
            throw std::runtime_error{"injected clock failure"};
        return clock.now();
    };
    SessionAutosave autosave{writer, snapshot(1), std::move(scheduling)};
    auto changed = snapshot(2);
    changed.download_import_receipts = {{"download-1", std::filesystem::path{LR"(C:\Music\first.mp3)"}}};
    fail_clock = true;
    CHECK_THROWS_AS(autosave.scheduleStructural(changed), std::runtime_error);
    CHECK(autosave.latestRevision() == 0);
    CHECK(autosave.savedRevision() == 0);
    CHECK_FALSE(autosave.dirty());
    CHECK_FALSE(autosave.nextWakeup().has_value());
    fail_clock = false;
    REQUIRE(autosave.scheduleStructural(changed));
    CHECK(autosave.latestRevision() == 1);
    autosave.requestSaveNow();
    REQUIRE(writer.starts.size() == 1);
    CHECK(writer.starts.front().snapshot == changed);
}

} // namespace music_player::m0::test
