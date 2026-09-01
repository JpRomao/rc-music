#include "m0/adapters/SingleWorkerPreferencesWriter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class ControlledStore final : public PreferencesStore {
  public:
    PreferencesLoadResult load() override { return {}; }

    PreferencesSaveStatus save(const PreferencesSaveRequest& request) override {
        std::unique_lock lock{mutex_};
        const std::size_t call_number = ++entered_calls_;
        condition_.notify_all();
        condition_.wait(lock, [this, call_number] { return released_calls_ >= call_number; });
        requests.push_back(request);
        if (throw_on_save)
            throw std::runtime_error{"injected store failure"};
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

    std::vector<PreferencesSaveRequest> requests;
    PreferencesSaveStatus next_status{PreferencesSaveStatus::Saved};
    bool throw_on_save{false};

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t entered_calls_{0};
    std::size_t released_calls_{0};
};

PreferencesWriteRequest request(const std::uint64_t revision, const int skip_forward_seconds,
                                const bool replace_future = false) {
    Preferences preferences;
    preferences.skip_forward_seconds = skip_forward_seconds;
    return {.revision = revision, .preferences = preferences, .replace_future_version = replace_future};
}

} // namespace

TEST_CASE("single worker writes off the caller and reports a typed completion", "[preferences]") {
    ControlledStore store;
    SingleWorkerPreferencesWriter writer{store};

    CHECK(writer.start(request(1, 20)) == PreferencesWriterCommand::Accepted);
    store.waitUntilEntered();
    CHECK_FALSE(writer.poll().has_value());
    store.release();
    CHECK(writer.shutdown());

    const auto completion = writer.poll();
    REQUIRE(completion.has_value());
    CHECK(completion->revision == 1);
    CHECK(completion->status == PreferencesSaveStatus::Saved);
    REQUIRE(store.requests.size() == 1);
    CHECK(store.requests.front().preferences.skip_forward_seconds == 20);
    CHECK(writer.shutdown());
}

TEST_CASE("flush waits for an active revision and then persists the supplied latest revision",
          "[preferences][window-placement]") {
    ControlledStore store;
    SingleWorkerPreferencesWriter writer{store};
    PreferencesWriteRequest active_request = request(1, 20);
    const WindowPlacement active_placement{-1'920, 40, 960, 640};
    active_request.preferences.window_placement = active_placement;
    REQUIRE(writer.start(active_request) == PreferencesWriterCommand::Accepted);
    store.waitUntilEntered();

    PreferencesWriteRequest latest_request = request(2, 33, true);
    const WindowPlacement latest_placement{120, 80, 1'200, 760};
    latest_request.preferences.window_placement = latest_placement;
    std::optional<PreferencesWriteCompletion> flushed;
    std::thread flushing{[&] { flushed = writer.flush(latest_request); }};
    store.release();
    store.waitUntilEntered(2);
    store.release();
    flushing.join();

    REQUIRE(flushed.has_value());
    CHECK(flushed->revision == 2);
    CHECK(flushed->status == PreferencesSaveStatus::Saved);
    REQUIRE(store.requests.size() == 2);
    CHECK(store.requests[0].preferences.skip_forward_seconds == 20);
    REQUIRE(store.requests[0].preferences.window_placement.has_value());
    CHECK(*store.requests[0].preferences.window_placement == active_placement);
    CHECK(store.requests[1].preferences.skip_forward_seconds == 33);
    REQUIRE(store.requests[1].preferences.window_placement.has_value());
    CHECK(*store.requests[1].preferences.window_placement == latest_placement);
    CHECK(store.requests[1].replace_future_version);
    CHECK(writer.shutdown());
}

TEST_CASE("single worker translates store exceptions and rejects work after shutdown", "[preferences]") {
    ControlledStore store;
    store.throw_on_save = true;
    SingleWorkerPreferencesWriter writer{store};
    REQUIRE(writer.start(request(7, 25)) == PreferencesWriterCommand::Accepted);
    store.waitUntilEntered();
    store.release();
    REQUIRE(writer.shutdown());

    const auto completion = writer.poll();
    REQUIRE(completion.has_value());
    CHECK(completion->revision == 7);
    CHECK(completion->status == PreferencesSaveStatus::Failed);
    CHECK(writer.start(request(8, 30)) == PreferencesWriterCommand::Shutdown);
    CHECK_FALSE(writer.flush(request(8, 30)).has_value());
}

TEST_CASE("flush retries a failed revision instead of returning its stale completion", "[preferences][regression]") {
    ControlledStore store;
    store.next_status = PreferencesSaveStatus::Failed;
    SingleWorkerPreferencesWriter writer{store};
    std::optional<PreferencesWriteCompletion> failed;
    std::thread first_flush{[&] { failed = writer.flush(request(9, 25)); }};
    store.waitUntilEntered();
    store.release();
    first_flush.join();

    REQUIRE(failed.has_value());
    REQUIRE(failed->revision == 9);
    REQUIRE(failed->status == PreferencesSaveStatus::Failed);

    store.next_status = PreferencesSaveStatus::Saved;
    std::optional<PreferencesWriteCompletion> flushed;
    std::thread flushing{[&] { flushed = writer.flush(request(9, 25)); }};
    store.waitUntilEntered(2);
    store.release();
    flushing.join();

    REQUIRE(flushed.has_value());
    CHECK(flushed->revision == 9);
    CHECK(flushed->status == PreferencesSaveStatus::Saved);
    CHECK(writer.shutdown());
}

} // namespace music_player::m0::test
