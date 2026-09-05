#include "m0/adapters/SingleWorkerDiagnosticWriter.hpp"
#include "m0/domain/DiagnosticRecord.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class ControlledDiagnosticStore final : public DiagnosticLogStore {
  public:
    ControlledDiagnosticStore() {
        records.reserve(1024);
        threads.reserve(1024);
    }

    bool append(const DiagnosticRecord& record) noexcept override {
        std::unique_lock lock{mutex_};
        ++entered_;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
        records.push_back(record);
        threads.push_back(std::this_thread::get_id());
        if (failures_left > 0) {
            --failures_left;
            return false;
        }
        return true;
    }

    bool waitUntilEntered() {
        std::unique_lock lock{mutex_};
        return condition_.wait_for(lock, std::chrono::seconds{2}, [this] { return entered_ > 0; });
    }

    void release() {
        std::lock_guard lock{mutex_};
        released_ = true;
        condition_.notify_all();
    }

    std::vector<DiagnosticRecord> records;
    std::vector<std::thread::id> threads;
    std::size_t failures_left{0};

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t entered_{0};
    bool released_{false};
};

// Release the fake before writer destruction even when a REQUIRE fails.
struct ReleaseStoreOnExit final {
    ControlledDiagnosticStore& store;
    ~ReleaseStoreOnExit() { store.release(); }
};

DiagnosticEntry entry(const std::uint64_t count) {
    return {DiagnosticSeverity::Warning, DiagnosticEvent::SourceSkipped, DiagnosticCode::InvalidMedia, count};
}

std::uint64_t reportedDrops(const std::vector<DiagnosticRecord>& records) {
    std::uint64_t count = 0;
    for (const auto& record : records) {
        if (record.entry.event == DiagnosticEvent::EventsDropped)
            count += record.entry.count;
    }
    return count;
}

} // namespace

TEST_CASE("diagnostic records contain only closed names and decimal fields", "[diagnostic-log][NFR-PRIV-003]") {
    static_assert(std::is_trivially_copyable_v<DiagnosticEntry>);
    static_assert(std::is_trivially_copyable_v<DiagnosticRecord>);
    CHECK(formatDiagnosticRecord({1'234, entry(7)}) ==
          "utc_ms=1234 severity=warning event=source_skipped code=invalid_media count=7\n");
    CHECK(formatDiagnosticRecord({}) == "utc_ms=0 severity=info event=application_started code=none count=1\n");

    const DiagnosticRecord invalid{std::numeric_limits<std::int64_t>::min(),
                                   {static_cast<DiagnosticSeverity>(-1), static_cast<DiagnosticEvent>(999),
                                    static_cast<DiagnosticCode>(-7), std::numeric_limits<std::uint64_t>::max()}};
    CHECK(formatDiagnosticRecord(invalid) ==
          "utc_ms=-9223372036854775808 severity=unknown event=unknown code=unknown count=18446744073709551615\n");
    CHECK(maxDiagnosticRecordBytes == 256);
}

TEST_CASE("all diagnostic enum values have bounded stable external names", "[diagnostic-log][NFR-PRIV-003]") {
    const std::array severities{std::pair{DiagnosticSeverity::Info, "info"},
                                std::pair{DiagnosticSeverity::Warning, "warning"},
                                std::pair{DiagnosticSeverity::Error, "error"}};
    const std::array events{std::pair{DiagnosticEvent::ApplicationStarted, "application_started"},
                            std::pair{DiagnosticEvent::ApplicationStopping, "application_stopping"},
                            std::pair{DiagnosticEvent::StartupFailed, "startup_failed"},
                            std::pair{DiagnosticEvent::SourceOpenFailed, "source_open_failed"},
                            std::pair{DiagnosticEvent::SourceSkipped, "source_skipped"},
                            std::pair{DiagnosticEvent::SeekRejected, "seek_rejected"},
                            std::pair{DiagnosticEvent::MetadataWarning, "metadata_warning"},
                            std::pair{DiagnosticEvent::PreferencesWarning, "preferences_warning"},
                            std::pair{DiagnosticEvent::SessionWarning, "session_warning"},
                            std::pair{DiagnosticEvent::PlatformWarning, "platform_warning"},
                            std::pair{DiagnosticEvent::EventsDropped, "events_dropped"}};
    const std::array codes{std::pair{DiagnosticCode::None, "none"},
                           std::pair{DiagnosticCode::SourceUnavailable, "source_unavailable"},
                           std::pair{DiagnosticCode::InvalidMedia, "invalid_media"},
                           std::pair{DiagnosticCode::AccessDenied, "access_denied"},
                           std::pair{DiagnosticCode::BackendFailure, "backend_failure"},
                           std::pair{DiagnosticCode::AudioOutputUnavailable, "audio_output_unavailable"},
                           std::pair{DiagnosticCode::AudioOutputChanged, "audio_output_changed"},
                           std::pair{DiagnosticCode::NotSeekable, "not_seekable"},
                           std::pair{DiagnosticCode::Busy, "busy"},
                           std::pair{DiagnosticCode::NotReady, "not_ready"},
                           std::pair{DiagnosticCode::CorruptState, "corrupt_state"},
                           std::pair{DiagnosticCode::WriteFailed, "write_failed"},
                           std::pair{DiagnosticCode::UnsupportedVersion, "unsupported_version"},
                           std::pair{DiagnosticCode::Unavailable, "unavailable"},
                           std::pair{DiagnosticCode::Unknown, "unknown"}};
    for (const auto& [severity, severity_name] : severities) {
        for (const auto& [event, event_name] : events) {
            for (const auto& [code, code_name] : codes) {
                const auto line =
                    formatDiagnosticRecord({std::numeric_limits<std::int64_t>::max(),
                                            {severity, event, code, std::numeric_limits<std::uint64_t>::max()}});
                const auto expected = std::string{"utc_ms=9223372036854775807 severity="} + severity_name +
                                      " event=" + event_name + " code=" + code_name + " count=18446744073709551615\n";
                CHECK(line == expected);
                CHECK(line.size() <= maxDiagnosticRecordBytes);
                CHECK(std::count(line.begin(), line.end(), '\n') == 1);
                CHECK(line.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789= \n") == std::string::npos);
            }
        }
    }
}

TEST_CASE("diagnostic retention settings only allow positive values up to the fixed defaults",
          "[diagnostic-log][NFR-PRIV-003]") {
    const DiagnosticLogLimits defaults;
    CHECK(defaults.max_file_bytes == 262'144);
    CHECK(defaults.max_files == 3);
    CHECK(defaults.max_age_ms == 604'800'000);
    CHECK(validDiagnosticLogLimits(defaults));
    CHECK(validDiagnosticLogLimits({1, 1, 1}));
    CHECK(validDiagnosticLogLimits({defaults.max_file_bytes - 1, defaults.max_files - 1, defaults.max_age_ms - 1}));
    CHECK_FALSE(validDiagnosticLogLimits({0, 1, 1}));
    CHECK_FALSE(validDiagnosticLogLimits({1, 0, 1}));
    CHECK_FALSE(validDiagnosticLogLimits({1, 1, 0}));
    CHECK_FALSE(validDiagnosticLogLimits({1, 1, -1}));
    CHECK_FALSE(validDiagnosticLogLimits({defaults.max_file_bytes + 1, 1, 1}));
    CHECK_FALSE(validDiagnosticLogLimits({1, defaults.max_files + 1, 1}));
    CHECK_FALSE(validDiagnosticLogLimits({1, 1, defaults.max_age_ms + 1}));
}

TEST_CASE("diagnostic append bounds include the exact limit without arithmetic overflow",
          "[diagnostic-log][NFR-PRIV-003]") {
    CHECK(canAppendDiagnosticRecord(7, 2, 10));
    CHECK(canAppendDiagnosticRecord(8, 2, 10));
    CHECK_FALSE(canAppendDiagnosticRecord(9, 2, 10));
    CHECK_FALSE(canAppendDiagnosticRecord(11, 1, 10));
    CHECK_FALSE(canAppendDiagnosticRecord(0, 0, 10));
    CHECK_FALSE(canAppendDiagnosticRecord(0, 1, 0));
    CHECK(canAppendDiagnosticRecord(0, 10, 10));
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    CHECK(canAppendDiagnosticRecord(maximum - 1, 1, maximum));
    CHECK_FALSE(canAppendDiagnosticRecord(maximum, 1, maximum));
    CHECK_FALSE(canAppendDiagnosticRecord(maximum - 1, 2, maximum));
}

TEST_CASE("diagnostic age bounds retain exact age and reject future invalid or overflowing timestamps",
          "[diagnostic-log][NFR-PRIV-003]") {
    CHECK_FALSE(diagnosticLogExpired(100, 109, 10));
    CHECK_FALSE(diagnosticLogExpired(100, 110, 10));
    CHECK(diagnosticLogExpired(100, 111, 10));
    CHECK(diagnosticLogExpired(100, 99, 10));
    CHECK(diagnosticLogExpired(-1, 0, 10));
    CHECK(diagnosticLogExpired(0, -1, 10));
    CHECK(diagnosticLogExpired(0, 0, 0));
    CHECK(diagnosticLogExpired(0, 0, -1));
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    CHECK_FALSE(diagnosticLogExpired(maximum - 10, maximum, 10));
    CHECK(diagnosticLogExpired(0, maximum, 10));
    CHECK(diagnosticLogExpired(std::numeric_limits<std::int64_t>::min(), maximum, maximum));
}

TEST_CASE("diagnostic drop counters saturate at uint64 maximum", "[diagnostic-log][NFR-PRIV-003]") {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    CHECK(saturatingDiagnosticCount(0) == 1);
    CHECK(saturatingDiagnosticCount(1, 0) == 1);
    CHECK(saturatingDiagnosticCount(maximum - 2) == maximum - 1);
    CHECK(saturatingDiagnosticCount(maximum - 1) == maximum);
    CHECK(saturatingDiagnosticCount(maximum) == maximum);
    CHECK(saturatingDiagnosticCount(3, maximum) == maximum);
}

TEST_CASE("diagnostic writer validates capacity before starting work", "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    std::atomic<int> clocks{0};
    const auto now = [&] {
        ++clocks;
        return 100;
    };
    CHECK_THROWS_AS((SingleWorkerDiagnosticWriter{store, {now, 0}}), std::invalid_argument);
    CHECK_THROWS_AS((SingleWorkerDiagnosticWriter{store, {now, 257}}), std::invalid_argument);
    CHECK(clocks.load() == 0);
    CHECK(store.records.empty());
}

TEST_CASE("diagnostic writer preserves accepted order and runs clock and store only on one worker",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    std::vector<std::thread::id> clock_threads;
    std::int64_t now = 100;
    SingleWorkerDiagnosticWriter writer{store,
                                        {[&] {
                                             clock_threads.push_back(std::this_thread::get_id());
                                             return now++;
                                         },
                                         2}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    // Both producer calls complete while the first append is still blocked.
    REQUIRE(writer.submit(entry(2)));
    REQUIRE(writer.submit(entry(3)));
    store.release();
    REQUIRE(writer.shutdown());
    CHECK(writer.shutdown());
    CHECK_FALSE(writer.submit(entry(4)));
    REQUIRE(store.records.size() == 3);
    CHECK(store.records[0] == DiagnosticRecord{100, entry(1)});
    CHECK(store.records[1] == DiagnosticRecord{101, entry(2)});
    CHECK(store.records[2] == DiagnosticRecord{102, entry(3)});
    REQUIRE(clock_threads.size() == 3);
    REQUIRE(store.threads.size() == 3);
    CHECK(store.threads == clock_threads);
    CHECK(std::all_of(clock_threads.begin(), clock_threads.end(),
                      [&](const auto id) { return id == clock_threads.front() && id != std::this_thread::get_id(); }));
}

TEST_CASE("diagnostic writer bounds pending entries and coalesces discarded records not entry counts",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    SingleWorkerDiagnosticWriter writer{store, {[] { return 123; }, 2}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    REQUIRE(writer.submit(entry(2)));
    REQUIRE(writer.submit(entry(3)));
    for (int index = 0; index < 10; ++index)
        CHECK_FALSE(writer.submit(entry(std::numeric_limits<std::uint64_t>::max())));
    store.release();
    REQUIRE(writer.shutdown());
    REQUIRE(store.records.size() == 4);
    CHECK(reportedDrops(store.records) == 10);
    std::vector<std::uint64_t> accepted_counts;
    for (const auto& record : store.records) {
        CHECK(record.timestamp_utc_ms == 123);
        if (record.entry.event == DiagnosticEvent::EventsDropped) {
            CHECK(record.entry.severity == DiagnosticSeverity::Warning);
            CHECK(record.entry.code == DiagnosticCode::None);
        } else {
            accepted_counts.push_back(record.entry.count);
        }
    }
    CHECK(accepted_counts == std::vector<std::uint64_t>{1, 2, 3});
}

TEST_CASE("diagnostic writer accepts exactly 256 pending entries at the maximum capacity",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    SingleWorkerDiagnosticWriter writer{store, {[] { return 1; }, 256}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    for (std::uint64_t count = 2; count <= 257; ++count)
        REQUIRE(writer.submit(entry(count)));
    CHECK_FALSE(writer.submit(entry(258)));
    store.release();
    REQUIRE(writer.shutdown());
    REQUIRE(store.records.size() == 258);
    CHECK(reportedDrops(store.records) == 1);
    std::uint64_t expected = 1;
    for (const auto& record : store.records) {
        if (record.entry.event != DiagnosticEvent::EventsDropped)
            CHECK(record.entry.count == expected++);
    }
    CHECK(expected == 258);
}

TEST_CASE("diagnostic store failure is nonfatal does not recurse and permits later accepted entries",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    store.failures_left = 1;
    SingleWorkerDiagnosticWriter writer{store, {[] { return 1; }, 1}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    REQUIRE(writer.submit(entry(2)));
    store.release();
    CHECK_FALSE(writer.shutdown());
    CHECK_FALSE(writer.shutdown());
    REQUIRE(store.records.size() == 2);
    CHECK(store.records[1].entry == entry(2));
    CHECK(reportedDrops(store.records) == 0);
    CHECK_FALSE(writer.submit(entry(3)));
}

TEST_CASE("diagnostic clock failure is contained and never exposes exception text", "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    std::mutex clock_mutex;
    std::condition_variable clock_condition;
    bool clock_entered = false;
    bool clock_released = false;
    int clocks = 0;
    SingleWorkerDiagnosticWriter writer{store,
                                        {[&]() -> std::int64_t {
                                             if (++clocks == 1) {
                                                 std::unique_lock lock{clock_mutex};
                                                 clock_entered = true;
                                                 clock_condition.notify_all();
                                                 clock_condition.wait(lock, [&] { return clock_released; });
                                                 throw std::runtime_error{"C:/private/audio.mp3?token=secret"};
                                             }
                                             return 456;
                                         },
                                         1}};
    // Unblock both fake dependencies before writer teardown on any assertion.
    struct ReleaseClock final {
        std::mutex& mutex;
        std::condition_variable& condition;
        bool& released;
        ~ReleaseClock() {
            std::lock_guard lock{mutex};
            released = true;
            condition.notify_all();
        }
    } release_clock{clock_mutex, clock_condition, clock_released};
    ReleaseStoreOnExit release_store{store};
    REQUIRE(writer.submit(entry(1)));
    {
        std::unique_lock lock{clock_mutex};
        REQUIRE(clock_condition.wait_for(lock, std::chrono::seconds{2}, [&] { return clock_entered; }));
    }
    REQUIRE(writer.submit(entry(2)));
    {
        std::lock_guard lock{clock_mutex};
        clock_released = true;
        clock_condition.notify_all();
    }
    store.release();
    CHECK_FALSE(writer.shutdown());
    REQUIRE(store.records.size() == 1);
    CHECK(store.records[0] == DiagnosticRecord{456, entry(2)});
    CHECK(formatDiagnosticRecord(store.records[0]).find("private") == std::string::npos);
}

TEST_CASE("diagnostic writer drains accepted entries during concurrent idempotent shutdown",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    SingleWorkerDiagnosticWriter writer{store, {[] { return 1; }, 1}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    REQUIRE(writer.submit(entry(2)));
    bool first = false;
    bool second = false;
    std::thread first_shutdown{[&] { first = writer.shutdown(); }};
    std::thread second_shutdown{[&] { second = writer.shutdown(); }};
    store.release();
    first_shutdown.join();
    second_shutdown.join();
    CHECK(first);
    CHECK(second);
    REQUIRE(store.records.size() == 2);
    CHECK_FALSE(writer.submit(entry(3)));
}

TEST_CASE("diagnostic concurrent producers account for every discarded record without waiting for storage",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    SingleWorkerDiagnosticWriter writer{store, {[] { return 1; }, 1}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    REQUIRE(writer.submit(entry(2)));
    std::atomic<int> accepted{0};
    std::array<std::thread, 8> producers;
    for (auto& producer : producers) {
        producer = std::thread{[&] {
            for (int index = 0; index < 16; ++index) {
                if (writer.submit(entry(9)))
                    ++accepted;
            }
        }};
    }
    for (auto& producer : producers)
        producer.join();
    CHECK(accepted.load() == 0);
    store.release();
    REQUIRE(writer.shutdown());
    CHECK(reportedDrops(store.records) == 128);
    REQUIRE(store.records.size() == 3);
}

TEST_CASE("diagnostic writer destruction drains accepted records without an explicit shutdown",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    {
        SingleWorkerDiagnosticWriter writer{store, {[] { return 1; }, 1}};
        ReleaseStoreOnExit release{store};
        REQUIRE(writer.submit(entry(1)));
        REQUIRE(store.waitUntilEntered());
        REQUIRE(writer.submit(entry(2)));
    }
    REQUIRE(store.records.size() == 2);
}

TEST_CASE("diagnostic shutdown drains every accepted entry while concurrent producers finish",
          "[diagnostic-log][NFR-PRIV-003]") {
    ControlledDiagnosticStore store;
    SingleWorkerDiagnosticWriter writer{store, {[] { return 1; }, 256}};
    ReleaseStoreOnExit release{store};
    REQUIRE(writer.submit(entry(1)));
    REQUIRE(store.waitUntilEntered());
    std::atomic<bool> start{false};
    std::array<std::array<bool, 16>, 8> accepted{};
    std::array<std::thread, 8> producers;
    for (std::size_t producer_index = 0; producer_index < producers.size(); ++producer_index) {
        producers[producer_index] = std::thread{[&, producer_index] {
            start.wait(false);
            for (std::size_t index = 0; index < accepted[producer_index].size(); ++index) {
                const auto count = 2 + producer_index * accepted[producer_index].size() + index;
                accepted[producer_index][index] = writer.submit(entry(count));
            }
        }};
    }
    bool shutdown_result = false;
    std::thread shutdown{[&] {
        start.wait(false);
        shutdown_result = writer.shutdown();
    }};
    start.store(true);
    start.notify_all();
    for (auto& producer : producers)
        producer.join();
    store.release();
    shutdown.join();
    CHECK(shutdown_result);
    CHECK_FALSE(writer.submit(entry(999)));
    std::vector<std::uint64_t> expected{1};
    for (std::size_t producer_index = 0; producer_index < producers.size(); ++producer_index) {
        for (std::size_t index = 0; index < accepted[producer_index].size(); ++index) {
            if (accepted[producer_index][index])
                expected.push_back(2 + producer_index * accepted[producer_index].size() + index);
        }
    }
    std::vector<std::uint64_t> actual;
    for (const auto& record : store.records) {
        if (record.entry.event != DiagnosticEvent::EventsDropped)
            actual.push_back(record.entry.count);
    }
    std::sort(actual.begin(), actual.end());
    CHECK(actual == expected);
}

} // namespace music_player::m0::test
