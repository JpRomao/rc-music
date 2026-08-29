#include "m0/adapters/SingleWorkerMetadataReader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace music_player::m0::test {
namespace {

using namespace std::chrono_literals;

class ControlledMetadataExtractor final : public MetadataExtractor {
  public:
    void block(const std::filesystem::path& source_path) {
        std::lock_guard lock{mutex_};
        blocked_.insert(source_path);
    }

    void release(const std::filesystem::path& source_path) {
        {
            std::lock_guard lock{mutex_};
            blocked_.erase(source_path);
        }
        changed_.notify_all();
    }

    void releaseAll() {
        {
            std::lock_guard lock{mutex_};
            blocked_.clear();
        }
        changed_.notify_all();
    }

    void throwFor(const std::filesystem::path& source_path) {
        std::lock_guard lock{mutex_};
        throwing_.insert(source_path);
    }

    [[nodiscard]] bool waitForCallCount(const std::size_t count, const std::chrono::milliseconds timeout = 2s) const {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [&] { return calls_.size() >= count; });
    }

    [[nodiscard]] std::vector<std::filesystem::path> calls() const {
        std::lock_guard lock{mutex_};
        return calls_;
    }

    [[nodiscard]] std::size_t maxConcurrency() const {
        std::lock_guard lock{mutex_};
        return max_concurrency_;
    }

    [[nodiscard]] std::thread::id workerThread() const {
        std::lock_guard lock{mutex_};
        return worker_thread_;
    }

    [[nodiscard]] MetadataReadResult read(const std::filesystem::path& source_path) const override {
        bool should_throw = false;
        {
            std::unique_lock lock{mutex_};
            calls_.push_back(source_path);
            ++active_count_;
            max_concurrency_ = std::max(max_concurrency_, active_count_);
            worker_thread_ = std::this_thread::get_id();
            changed_.notify_all();
            changed_.wait(lock, [&] { return !blocked_.contains(source_path); });
            should_throw = throwing_.contains(source_path);
            --active_count_;
            changed_.notify_all();
        }

        if (should_throw) {
            throw std::runtime_error{"injected metadata extraction failure"};
        }

        TrackMetadata metadata{};
        metadata.source_path = source_path;
        metadata.display_title = source_path.stem().string();
        return metadata;
    }

  private:
    mutable std::mutex mutex_{};
    mutable std::condition_variable changed_{};
    mutable std::set<std::filesystem::path> blocked_{};
    mutable std::set<std::filesystem::path> throwing_{};
    mutable std::vector<std::filesystem::path> calls_{};
    mutable std::size_t active_count_{0};
    mutable std::size_t max_concurrency_{0};
    mutable std::thread::id worker_thread_{};
};

class ManualMetadataClock final {
  public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() {
        std::lock_guard lock{mutex_};
        if (count_observations_) {
            ++observations_;
            changed_.notify_all();
        }
        return now_;
    }

    void advanceAndCount(const std::chrono::milliseconds amount) {
        std::lock_guard lock{mutex_};
        now_ += amount;
        observations_ = 0;
        count_observations_ = true;
    }

    [[nodiscard]] bool waitForObservations(const std::size_t count,
                                           const std::chrono::milliseconds timeout = 2s) const {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [&] { return observations_ >= count; });
    }

  private:
    mutable std::mutex mutex_{};
    mutable std::condition_variable changed_{};
    std::chrono::steady_clock::time_point now_{};
    std::size_t observations_{0};
    bool count_observations_{false};
};

struct ReleaseBlockedReads final {
    ControlledMetadataExtractor* extractor{};

    ~ReleaseBlockedReads() {
        if (extractor != nullptr) {
            extractor->releaseAll();
        }
    }
};

[[nodiscard]] std::optional<MetadataReadCompletion> waitForCompletion(MetadataReader& reader,
                                                                      const std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto completion = reader.tryTakeCompletion(); completion.has_value()) {
            return completion;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

[[nodiscard]] std::unique_ptr<ControlledMetadataExtractor> controlledExtractor(ControlledMetadataExtractor*& observer) {
    auto extractor = std::make_unique<ControlledMetadataExtractor>();
    observer = extractor.get();
    return extractor;
}

} // namespace

TEST_CASE("metadata reader validates generations and completes on its worker", "[m0][qt][core][metadata-reader]") {
    ControlledMetadataExtractor* extractor = nullptr;
    SingleWorkerMetadataReader reader{controlledExtractor(extractor)};
    ReleaseBlockedReads release{extractor};
    const auto caller_thread = std::this_thread::get_id();

    CHECK(reader.startRead({MetadataGeneration{0}, std::filesystem::path{"zero.wav"}}) ==
          MetadataCommandResult::InvalidArgument);
    CHECK(reader.startRead({MetadataGeneration{1}, {}}) == MetadataCommandResult::InvalidArgument);
    CHECK(reader.startRead({MetadataGeneration{2}, std::filesystem::path{"current.wav"}}) ==
          MetadataCommandResult::Accepted);

    const auto completion = waitForCompletion(reader);
    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{2});
    CHECK(completion->source_path == std::filesystem::path{"current.wav"});
    REQUIRE(std::holds_alternative<TrackMetadata>(completion->result));
    CHECK(std::get<TrackMetadata>(completion->result).display_title == "current");
    CHECK(extractor->workerThread() != caller_thread);
    CHECK(reader.startRead({MetadataGeneration{1}, std::filesystem::path{"old.wav"}}) ==
          MetadataCommandResult::StaleGeneration);
    CHECK(reader.startRead({MetadataGeneration{2}, std::filesystem::path{"duplicate.wav"}}) ==
          MetadataCommandResult::StaleGeneration);

    CHECK(reader.shutdown() == MetadataCommandResult::Accepted);
    CHECK(reader.shutdown() == MetadataCommandResult::Accepted);
    CHECK(reader.startRead({MetadataGeneration{3}, std::filesystem::path{"after.wav"}}) ==
          MetadataCommandResult::Shutdown);
    CHECK(reader.cancelRead(MetadataGeneration{2}) == MetadataCommandResult::Shutdown);
}

TEST_CASE("MR-016 cancellation makes the active result unobservable", "[m0][qt][core][metadata-reader][MR-016]") {
    const std::filesystem::path first{"first.wav"};
    const std::filesystem::path second{"second.wav"};
    ControlledMetadataExtractor* extractor = nullptr;
    auto owned_extractor = controlledExtractor(extractor);
    extractor->block(first);
    extractor->block(second);
    SingleWorkerMetadataReader reader{std::move(owned_extractor)};
    ReleaseBlockedReads release{extractor};

    REQUIRE(reader.startRead({MetadataGeneration{1}, first}) == MetadataCommandResult::Accepted);
    REQUIRE(extractor->waitForCallCount(1));
    CHECK(reader.cancelRead(MetadataGeneration{1}) == MetadataCommandResult::Accepted);
    REQUIRE(reader.startRead({MetadataGeneration{2}, second}) == MetadataCommandResult::Accepted);
    extractor->release(first);
    REQUIRE(extractor->waitForCallCount(2));
    CHECK_FALSE(reader.tryTakeCompletion().has_value());

    extractor->release(second);
    const auto completion = waitForCompletion(reader);
    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{2});
    CHECK(completion->source_path == second);
}

TEST_CASE("MR-017 keeps only the newest pending generation and never overlaps extraction",
          "[m0][qt][core][metadata-reader][MR-017]") {
    const std::filesystem::path first{"first.wav"};
    const std::filesystem::path replaced{"replaced.wav"};
    const std::filesystem::path newest{"newest.wav"};
    ControlledMetadataExtractor* extractor = nullptr;
    auto owned_extractor = controlledExtractor(extractor);
    extractor->block(first);
    extractor->block(newest);
    SingleWorkerMetadataReader reader{std::move(owned_extractor)};
    ReleaseBlockedReads release{extractor};

    REQUIRE(reader.startRead({MetadataGeneration{1}, first}) == MetadataCommandResult::Accepted);
    REQUIRE(extractor->waitForCallCount(1));
    REQUIRE(reader.startRead({MetadataGeneration{2}, replaced}) == MetadataCommandResult::Accepted);
    REQUIRE(reader.startRead({MetadataGeneration{3}, newest}) == MetadataCommandResult::Accepted);
    extractor->release(first);
    REQUIRE(extractor->waitForCallCount(2));

    CHECK(extractor->calls() == std::vector<std::filesystem::path>{first, newest});
    CHECK(extractor->maxConcurrency() == 1);
    CHECK_FALSE(reader.tryTakeCompletion().has_value());
    extractor->release(newest);
    const auto completion = waitForCompletion(reader);
    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{3});
    CHECK(completion->source_path == newest);
}

TEST_CASE("a delayed cancellation cannot cancel the newer generation", "[m0][qt][core][metadata-reader][MR-017]") {
    const std::filesystem::path first{"first.wav"};
    const std::filesystem::path second{"second.wav"};
    ControlledMetadataExtractor* extractor = nullptr;
    auto owned_extractor = controlledExtractor(extractor);
    extractor->block(first);
    extractor->block(second);
    SingleWorkerMetadataReader reader{std::move(owned_extractor)};
    ReleaseBlockedReads release{extractor};

    REQUIRE(reader.startRead({MetadataGeneration{1}, first}) == MetadataCommandResult::Accepted);
    REQUIRE(extractor->waitForCallCount(1));
    REQUIRE(reader.startRead({MetadataGeneration{2}, second}) == MetadataCommandResult::Accepted);
    CHECK(reader.cancelRead(MetadataGeneration{1}) == MetadataCommandResult::StaleGeneration);
    extractor->release(first);
    REQUIRE(extractor->waitForCallCount(2));
    CHECK_FALSE(reader.tryTakeCompletion().has_value());

    extractor->release(second);
    const auto completion = waitForCompletion(reader);
    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{2});
}

TEST_CASE("MR-020 expires queued work without accumulation and remains usable",
          "[m0][qt][core][metadata-reader][MR-020]") {
    const std::filesystem::path first{"first.wav"};
    const std::filesystem::path replaced{"replaced.wav"};
    const std::filesystem::path expired{"expired.wav"};
    const std::filesystem::path following{"following.wav"};
    auto clock = std::make_shared<ManualMetadataClock>();
    ControlledMetadataExtractor* extractor = nullptr;
    auto owned_extractor = controlledExtractor(extractor);
    extractor->block(first);
    SingleWorkerMetadataReader reader{
        std::move(owned_extractor),
        {.job_budget = 2s, .now = [clock] { return clock->now(); }},
    };
    ReleaseBlockedReads release{extractor};

    REQUIRE(reader.startRead({MetadataGeneration{1}, first}) == MetadataCommandResult::Accepted);
    REQUIRE(extractor->waitForCallCount(1));
    REQUIRE(reader.startRead({MetadataGeneration{2}, replaced}) == MetadataCommandResult::Accepted);
    REQUIRE(reader.startRead({MetadataGeneration{3}, expired}) == MetadataCommandResult::Accepted);
    clock->advanceAndCount(3s);
    extractor->release(first);
    REQUIRE(clock->waitForObservations(2));

    CHECK(extractor->calls() == std::vector<std::filesystem::path>{first});
    CHECK(extractor->maxConcurrency() == 1);
    CHECK_FALSE(reader.tryTakeCompletion().has_value());

    REQUIRE(reader.startRead({MetadataGeneration{4}, following}) == MetadataCommandResult::Accepted);
    REQUIRE(extractor->waitForCallCount(2));
    const auto completion = waitForCompletion(reader);
    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{4});
    CHECK(completion->source_path == following);
}

TEST_CASE("extractor exceptions become controlled read failures", "[m0][qt][core][metadata-reader][error]") {
    const std::filesystem::path source{"throws.wav"};
    ControlledMetadataExtractor* extractor = nullptr;
    auto owned_extractor = controlledExtractor(extractor);
    extractor->throwFor(source);
    SingleWorkerMetadataReader reader{std::move(owned_extractor)};
    ReleaseBlockedReads release{extractor};

    REQUIRE(reader.startRead({MetadataGeneration{1}, source}) == MetadataCommandResult::Accepted);
    const auto completion = waitForCompletion(reader);
    REQUIRE(completion.has_value());
    REQUIRE(std::holds_alternative<MetadataReadError>(completion->result));
    const auto& error = std::get<MetadataReadError>(completion->result);
    CHECK(error.code == MetadataReadErrorCode::ReadFailed);
    CHECK_FALSE(error.message.empty());
}

TEST_CASE("shutdown invalidates active work before waiting for the worker",
          "[m0][qt][core][metadata-reader][shutdown]") {
    const std::filesystem::path source{"blocked.wav"};
    ControlledMetadataExtractor* extractor = nullptr;
    auto owned_extractor = controlledExtractor(extractor);
    extractor->block(source);
    SingleWorkerMetadataReader reader{std::move(owned_extractor)};
    ReleaseBlockedReads release{extractor};

    REQUIRE(reader.startRead({MetadataGeneration{1}, source}) == MetadataCommandResult::Accepted);
    REQUIRE(extractor->waitForCallCount(1));

    auto shutdown_result = MetadataCommandResult::InvalidArgument;
    std::jthread shutdown_thread{[&] { shutdown_result = reader.shutdown(); }};
    auto shutdown_became_visible = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (reader.startRead({MetadataGeneration{0}, std::filesystem::path{"probe.wav"}}) ==
            MetadataCommandResult::Shutdown) {
            shutdown_became_visible = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }

    extractor->release(source);
    shutdown_thread.join();

    CHECK(shutdown_became_visible);
    CHECK(shutdown_result == MetadataCommandResult::Accepted);
    CHECK_FALSE(reader.tryTakeCompletion().has_value());
}

} // namespace music_player::m0::test
