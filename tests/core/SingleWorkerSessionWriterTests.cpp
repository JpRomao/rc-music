#include "m0/adapters/SingleWorkerSessionWriter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class ControlledSessionStore final : public SessionStore {
  public:
    SessionLoadResult load() override {
        ++load_calls;
        load_threads.push_back(std::this_thread::get_id());
        if (throw_on_load)
            throw std::runtime_error{"injected session load failure"};
        return load_result;
    }

    SessionSaveStatus save(const SessionSaveRequest& request) override {
        std::unique_lock lock{mutex_};
        const std::size_t call_number = ++entered_calls_;
        save_threads.push_back(std::this_thread::get_id());
        condition_.notify_all();
        condition_.wait(lock, [this, call_number] { return released_calls_ >= call_number; });
        requests.push_back(request);
        if (throw_on_save)
            throw std::runtime_error{"injected session save failure"};
        return next_status;
    }

    void waitUntilEntered(const std::size_t call_count = 1) {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [this, call_count] { return entered_calls_ >= call_count; });
    }

    void release() {
        std::lock_guard lock{mutex_};
        ++released_calls_;
        condition_.notify_all();
    }

    std::vector<SessionSaveRequest> requests;
    std::vector<std::thread::id> save_threads;
    std::vector<std::thread::id> load_threads;
    SessionLoadResult load_result{};
    SessionSaveStatus next_status{SessionSaveStatus::Saved};
    std::size_t load_calls{0};
    bool throw_on_save{false};
    bool throw_on_load{false};

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t entered_calls_{0};
    std::size_t released_calls_{0};
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

[[nodiscard]] SessionWriteRequest request(const SessionRevision revision, const std::uint64_t occurrence_id,
                                          const std::int64_t position_ms = 0,
                                          const bool replace_future_version = false) {
    return {.revision = revision,
            .snapshot = snapshot(occurrence_id, position_ms),
            .replace_future_version = replace_future_version};
}

} // namespace

TEST_CASE("single session worker serializes one background save and reports a typed completion",
          "[m0][qt][core][session][FR-043][NFR-REL-003]") {
    ControlledSessionStore store;
    SingleWorkerSessionWriter writer{store};
    const auto caller_thread = std::this_thread::get_id();

    REQUIRE(writer.start(request(1, 41, 1200)) == SessionWriterCommand::Accepted);
    store.waitUntilEntered();
    CHECK(writer.start(request(2, 42, 1300)) == SessionWriterCommand::Busy);
    CHECK_FALSE(writer.poll().has_value());
    store.release();
    REQUIRE(writer.shutdown());

    const auto completion = writer.poll();
    REQUIRE(completion.has_value());
    CHECK(completion->revision == 1);
    CHECK(completion->status == SessionSaveStatus::Saved);
    CHECK_FALSE(completion->requested_snapshot_recoverable_after_failure.has_value());
    REQUIRE(store.requests.size() == 1);
    REQUIRE(store.save_threads.size() == 1);
    CHECK(store.save_threads.front() != caller_thread);
    CHECK(store.requests.front().snapshot == snapshot(41, 1200));
    CHECK_FALSE(store.requests.front().replace_future_version);
    CHECK(store.load_calls == 0);
    CHECK(writer.shutdown());
}

TEST_CASE("session flush waits for an active revision and then writes the supplied latest revision",
          "[m0][qt][core][session][FR-043][NFR-REL-003]") {
    ControlledSessionStore store;
    SingleWorkerSessionWriter writer{store};
    REQUIRE(writer.start(request(1, 51, 2100)) == SessionWriterCommand::Accepted);
    store.waitUntilEntered();

    std::optional<SessionWriteCompletion> flushed;
    std::thread flushing{[&] { flushed = writer.flush(request(2, 52, 2200, true)); }};
    store.release();
    store.waitUntilEntered(2);
    store.release();
    flushing.join();

    REQUIRE(flushed.has_value());
    CHECK(flushed->revision == 2);
    CHECK(flushed->status == SessionSaveStatus::Saved);
    CHECK_FALSE(flushed->requested_snapshot_recoverable_after_failure.has_value());
    REQUIRE(store.requests.size() == 2);
    CHECK(store.requests[0].snapshot == snapshot(51, 2100));
    CHECK(store.requests[1].snapshot == snapshot(52, 2200));
    CHECK(store.requests[1].replace_future_version);
    CHECK(writer.shutdown());
}

TEST_CASE("session flush consumes the completion instead of leaving it observable through poll",
          "[m0][qt][core][session][NFR-REL-003][regression]") {
    ControlledSessionStore store;
    SingleWorkerSessionWriter writer{store};
    const auto requested = request(21, 211, 2100);

    std::optional<SessionWriteCompletion> flushed;
    std::thread flushing{[&] { flushed = writer.flush(requested); }};
    store.waitUntilEntered();
    store.release();
    flushing.join();

    REQUIRE(flushed.has_value());
    CHECK(flushed->revision == requested.revision);
    CHECK(flushed->status == SessionSaveStatus::Saved);
    CHECK_FALSE(writer.poll().has_value());
    CHECK(writer.shutdown());
}

TEST_CASE("concurrent session shutdown callers safely drain one accepted save",
          "[m0][qt][core][session][NFR-REL-003][regression]") {
    ControlledSessionStore store;
    SingleWorkerSessionWriter writer{store};
    const auto requested = request(22, 221, 2200);
    REQUIRE(writer.start(requested) == SessionWriterCommand::Accepted);
    store.waitUntilEntered();

    std::mutex callers_mutex;
    std::condition_variable callers_condition;
    std::size_t callers_ready = 0;
    bool invoke_shutdown = false;
    bool first_result = false;
    bool second_result = false;
    const auto shutdown_caller = [&](bool& result) {
        {
            std::unique_lock lock{callers_mutex};
            ++callers_ready;
            callers_condition.notify_all();
            callers_condition.wait(lock, [&] { return invoke_shutdown; });
        }
        result = writer.shutdown();
    };

    std::thread first{shutdown_caller, std::ref(first_result)};
    std::thread second{shutdown_caller, std::ref(second_result)};
    {
        std::unique_lock lock{callers_mutex};
        callers_condition.wait(lock, [&] { return callers_ready == 2; });
        invoke_shutdown = true;
        callers_condition.notify_all();
    }
    store.release();
    first.join();
    second.join();

    CHECK(first_result);
    CHECK(second_result);
    REQUIRE(store.requests.size() == 1);
    CHECK(store.requests.front().snapshot == requested.snapshot);
    const auto completion = writer.poll();
    REQUIRE(completion.has_value());
    CHECK(completion->revision == requested.revision);
    CHECK(completion->status == SessionSaveStatus::Saved);
    CHECK(writer.shutdown());
}

TEST_CASE("failed session saves stay failed while exact primary or backup readback is recorded",
          "[m0][qt][core][session][FR-044][NFR-REL-003]") {
    ControlledSessionStore store;
    const auto caller_thread = std::this_thread::get_id();
    store.next_status = SessionSaveStatus::Failed;
    const auto requested = request(7, 71, 7000);
    store.load_result.snapshot = requested.snapshot;
    SECTION("primary") { store.load_result.status = SessionLoadStatus::Loaded; }
    SECTION("backup") { store.load_result.status = SessionLoadStatus::RecoveredFromBackup; }
    SingleWorkerSessionWriter writer{store};

    REQUIRE(writer.start(requested) == SessionWriterCommand::Accepted);
    store.waitUntilEntered();
    store.release();
    REQUIRE(writer.shutdown());

    const auto completion = writer.poll();
    REQUIRE(completion.has_value());
    CHECK(completion->revision == requested.revision);
    CHECK(completion->status == SessionSaveStatus::Failed);
    CHECK(completion->requested_snapshot_recoverable_after_failure == std::optional<bool>{true});
    CHECK(store.load_calls == 1);
    REQUIRE(store.save_threads.size() == 1);
    REQUIRE(store.load_threads.size() == 1);
    CHECK(store.save_threads.front() != caller_thread);
    CHECK(store.load_threads.front() == store.save_threads.front());
}

TEST_CASE("session failure reconciliation distinguishes mismatch from unavailable readback",
          "[m0][qt][core][session][FR-044][NFR-REL-003]") {
    ControlledSessionStore store;
    store.next_status = SessionSaveStatus::Failed;
    const auto requested = request(8, 81, 8000);
    bool expect_observation = true;
    SECTION("different loaded snapshot") {
        store.load_result = {.snapshot = snapshot(82, 8100), .status = SessionLoadStatus::Loaded};
    }
    SECTION("matching snapshot under a non-recoverable status") {
        store.load_result = {.snapshot = requested.snapshot, .status = SessionLoadStatus::FutureVersion};
    }
    SECTION("readback throws") {
        store.throw_on_load = true;
        expect_observation = false;
    }
    SingleWorkerSessionWriter writer{store};

    std::optional<SessionWriteCompletion> completion;
    std::thread flushing{[&] { completion = writer.flush(requested); }};
    store.waitUntilEntered();
    store.release();
    flushing.join();

    REQUIRE(completion.has_value());
    CHECK(completion->status == SessionSaveStatus::Failed);
    if (expect_observation) {
        CHECK(completion->requested_snapshot_recoverable_after_failure == std::optional<bool>{false});
    } else {
        CHECK_FALSE(completion->requested_snapshot_recoverable_after_failure.has_value());
    }
    CHECK(writer.shutdown());
}

TEST_CASE("session worker reconciles store exceptions without promoting the failed status",
          "[m0][qt][core][session][NFR-REL-003][regression]") {
    ControlledSessionStore store;
    store.throw_on_save = true;
    const auto requested = request(9, 91, 9000);
    store.load_result = {.snapshot = requested.snapshot, .status = SessionLoadStatus::Loaded};
    SingleWorkerSessionWriter writer{store};

    std::optional<SessionWriteCompletion> completion;
    std::thread flushing{[&] { completion = writer.flush(requested); }};
    store.waitUntilEntered();
    store.release();
    flushing.join();

    REQUIRE(completion.has_value());
    CHECK(completion->status == SessionSaveStatus::Failed);
    CHECK(completion->requested_snapshot_recoverable_after_failure == std::optional<bool>{true});
    CHECK(writer.shutdown());
}

TEST_CASE("session worker contains consecutive save and reconciliation exceptions",
          "[m0][qt][core][session][NFR-REL-003][regression]") {
    ControlledSessionStore store;
    store.throw_on_save = true;
    store.throw_on_load = true;
    const auto requested = request(91, 911, 9100);
    SingleWorkerSessionWriter writer{store};

    std::optional<SessionWriteCompletion> completion;
    std::thread flushing{[&] { completion = writer.flush(requested); }};
    store.waitUntilEntered();
    store.release();
    flushing.join();

    REQUIRE(completion.has_value());
    CHECK(completion->revision == requested.revision);
    CHECK(completion->status == SessionSaveStatus::Failed);
    CHECK_FALSE(completion->requested_snapshot_recoverable_after_failure.has_value());
    CHECK(store.load_calls == 1);
    REQUIRE(store.save_threads.size() == 1);
    REQUIRE(store.load_threads.size() == 1);
    CHECK(store.load_threads.front() == store.save_threads.front());
    CHECK(writer.shutdown());
}

TEST_CASE("future-version blocking is not misreported as failed reconciliation", "[m0][qt][core][session][FR-044]") {
    ControlledSessionStore store;
    store.next_status = SessionSaveStatus::BlockedByFutureVersion;
    SingleWorkerSessionWriter writer{store};

    std::optional<SessionWriteCompletion> completion;
    std::thread flushing{[&] { completion = writer.flush(request(10, 101, 10'000)); }};
    store.waitUntilEntered();
    store.release();
    flushing.join();

    REQUIRE(completion.has_value());
    CHECK(completion->status == SessionSaveStatus::BlockedByFutureVersion);
    CHECK_FALSE(completion->requested_snapshot_recoverable_after_failure.has_value());
    CHECK(store.load_calls == 0);
    CHECK(writer.shutdown());
}

TEST_CASE("session flush retries a failed revision and preserves future-version authorization",
          "[m0][qt][core][session][FR-044][NFR-REL-003][regression]") {
    ControlledSessionStore store;
    store.next_status = SessionSaveStatus::Failed;
    const auto requested = request(11, 111, 11'000, true);
    store.load_result = {.snapshot = requested.snapshot, .status = SessionLoadStatus::Loaded};
    SingleWorkerSessionWriter writer{store};

    std::optional<SessionWriteCompletion> failed;
    std::thread first_flush{[&] { failed = writer.flush(requested); }};
    store.waitUntilEntered();
    store.release();
    first_flush.join();
    REQUIRE(failed.has_value());
    REQUIRE(failed->status == SessionSaveStatus::Failed);

    store.next_status = SessionSaveStatus::Saved;
    std::optional<SessionWriteCompletion> saved;
    std::thread retrying{[&] { saved = writer.flush(requested); }};
    store.waitUntilEntered(2);
    store.release();
    retrying.join();

    REQUIRE(saved.has_value());
    CHECK(saved->revision == requested.revision);
    CHECK(saved->status == SessionSaveStatus::Saved);
    CHECK_FALSE(saved->requested_snapshot_recoverable_after_failure.has_value());
    REQUIRE(store.requests.size() == 2);
    CHECK(store.requests[0].replace_future_version);
    CHECK(store.requests[1].replace_future_version);
    CHECK(writer.shutdown());
    CHECK(writer.start(request(12, 121)) == SessionWriterCommand::Shutdown);
    CHECK_FALSE(writer.flush(request(12, 121)).has_value());
}

} // namespace music_player::m0::test
