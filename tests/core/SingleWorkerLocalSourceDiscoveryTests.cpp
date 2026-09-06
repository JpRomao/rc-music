#include "m0/adapters/SingleWorkerLocalSourceDiscovery.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

using namespace std::chrono_literals;

[[nodiscard]] std::filesystem::path createExclusiveTemporaryDirectory() {
    static std::atomic_uint64_t sequence{0};
    const auto parent = std::filesystem::temp_directory_path();
    for (std::uint32_t attempt = 0; attempt < 100; ++attempt) {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto suffix = sequence.fetch_add(1, std::memory_order_relaxed);
        const auto candidate =
            parent / ("music-player-m0-discovery-" + std::to_string(timestamp) + "-" + std::to_string(suffix));
        std::error_code create_error;
        if (std::filesystem::create_directory(candidate, create_error))
            return candidate;
        if (create_error && create_error != std::errc::file_exists) {
            throw std::filesystem::filesystem_error{"could not create discovery test directory", candidate,
                                                    create_error};
        }
    }
    throw std::runtime_error{"could not allocate an exclusive discovery test directory"};
}

class ScopedTemporaryDirectory final {
  public:
    ScopedTemporaryDirectory() : path_{createExclusiveTemporaryDirectory()} {}

    ~ScopedTemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

void createFile(const std::filesystem::path& path) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    REQUIRE(stream);
}

struct CollectedDiscovery final {
    std::vector<std::filesystem::path> accepted_paths;
    std::vector<LocalSourceDiscoveryIssue> issues;
    std::size_t batch_count{0};
    std::optional<LocalSourceDiscoverySummary> summary;
};

[[nodiscard]] CollectedDiscovery collectDiscovery(LocalSourceDiscovery& discovery, const DiscoveryGeneration generation,
                                                  const std::chrono::milliseconds timeout = 2s) {
    CollectedDiscovery collected;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = discovery.poll(generation);
        if (result.status == DiscoveryPollStatus::Pending) {
            std::this_thread::yield();
            continue;
        }
        if (result.status == DiscoveryPollStatus::BatchReady) {
            REQUIRE(result.batch.has_value());
            ++collected.batch_count;
            for (auto& path : result.batch->accepted_paths)
                collected.accepted_paths.push_back(std::move(path));
            for (auto& issue : result.batch->issues)
                collected.issues.push_back(std::move(issue));
            continue;
        }
        REQUIRE(result.status == DiscoveryPollStatus::Completed);
        REQUIRE(result.summary.has_value());
        collected.summary = std::move(result.summary);
        return collected;
    }
    FAIL("local source discovery did not complete before its test deadline");
    return collected;
}

} // namespace

TEST_CASE("local source discovery validates requests and options", "[m0][qt][core][source-discovery]") {
    CHECK_THROWS_AS(SingleWorkerLocalSourceDiscovery({.batch_size = 0}), std::invalid_argument);
    CHECK_THROWS_AS(SingleWorkerLocalSourceDiscovery({.progress_interval = 0}), std::invalid_argument);
    CHECK_THROWS_AS(SingleWorkerLocalSourceDiscovery({.maximum_depth = 0}), std::invalid_argument);

    SingleWorkerLocalSourceDiscovery discovery;
    CHECK(discovery.start({DiscoveryGeneration{0}, {std::filesystem::path{"zero.mp3"}}}) ==
          DiscoveryCommandResult::InvalidArgument);
    CHECK(discovery.start({DiscoveryGeneration{1}, {}}) == DiscoveryCommandResult::InvalidArgument);
    CHECK(discovery.start({DiscoveryGeneration{1}, {std::filesystem::path{"valid.mp3"}, {}}}) ==
          DiscoveryCommandResult::InvalidArgument);
    CHECK(discovery.poll(DiscoveryGeneration{0}).status == DiscoveryPollStatus::InvalidArgument);
}

TEST_CASE("explicit files preserve root order duplicates and unknown extensions", "[m0][qt][core][source-discovery]") {
    ScopedTemporaryDirectory temporary;
    const auto known = temporary.path() / L"conhecida.MP3";
    const auto unknown = temporary.path() / L"diagnostico.bin";
    createFile(known);
    createFile(unknown);

    SingleWorkerLocalSourceDiscovery discovery;
    REQUIRE(discovery.start({DiscoveryGeneration{1}, {unknown, known, unknown}}) == DiscoveryCommandResult::Accepted);
    const auto result = collectDiscovery(discovery, DiscoveryGeneration{1});

    CHECK(result.accepted_paths == std::vector<std::filesystem::path>{unknown, known, unknown});
    REQUIRE(result.summary.has_value());
    CHECK(result.summary->inspected_count == 3);
    CHECK(result.summary->accepted_count == 3);
    CHECK(result.summary->issue_count == 0);
}

TEST_CASE("directory discovery keeps valid Unicode files while reporting invalid roots and entries",
          "[m0][qt][core][source-discovery]") {
    ScopedTemporaryDirectory temporary;
    const auto nested = temporary.path() / L"m\u00fasica com espa\u00e7os";
    REQUIRE(std::filesystem::create_directory(nested));
    const auto first = temporary.path() / L"primeira.flac";
    const auto second = nested / L"segunda.OGA";
    const auto unsupported = nested / L"notas.txt";
    const auto missing = temporary.path() / L"ausente";
    createFile(first);
    createFile(second);
    createFile(unsupported);

    SingleWorkerLocalSourceDiscovery discovery;
    REQUIRE(discovery.start({DiscoveryGeneration{7}, {missing, temporary.path()}}) == DiscoveryCommandResult::Accepted);
    const auto result = collectDiscovery(discovery, DiscoveryGeneration{7});

    CHECK(std::set<std::filesystem::path>{result.accepted_paths.begin(), result.accepted_paths.end()} ==
          std::set<std::filesystem::path>{first, second});
    CHECK(std::ranges::any_of(result.issues, [](const LocalSourceDiscoveryIssue& issue) {
        return issue.code == LocalSourceDiscoveryIssueCode::SourceNotFound;
    }));
    CHECK(std::ranges::any_of(result.issues, [](const LocalSourceDiscoveryIssue& issue) {
        return issue.code == LocalSourceDiscoveryIssueCode::UnsupportedFile;
    }));
    REQUIRE(result.summary.has_value());
    CHECK(result.summary->accepted_count == 2);
    CHECK(result.summary->issue_count == 2);
}

TEST_CASE("a source removed during discovery is isolated from later valid roots",
          "[m0][qt][core][source-discovery][security]") {
    ScopedTemporaryDirectory temporary;
    const auto removed = temporary.path() / L"removida.mp3";
    const auto valid = temporary.path() / L"valida.flac";
    createFile(removed);
    createFile(valid);

    bool removed_once = false;
    SingleWorkerLocalSourceDiscovery discovery{{
        .before_inspect =
            [&] {
                if (removed_once)
                    return;
                removed_once = true;
                std::error_code remove_error;
                const bool did_remove = std::filesystem::remove(removed, remove_error);
                if (!did_remove || remove_error)
                    throw std::runtime_error{"could not remove the injected discovery source"};
            },
    }};

    REQUIRE(discovery.start({DiscoveryGeneration{1}, {removed, valid}}) == DiscoveryCommandResult::Accepted);
    const auto result = collectDiscovery(discovery, DiscoveryGeneration{1});

    CHECK(result.accepted_paths == std::vector<std::filesystem::path>{valid});
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == LocalSourceDiscoveryIssueCode::SourceNotFound);
}

TEST_CASE("discovery publishes bounded batches with one-slot backpressure",
          "[m0][qt][core][source-discovery][backpressure]") {
    ScopedTemporaryDirectory temporary;
    std::vector<std::filesystem::path> roots;
    for (int index = 0; index < 5; ++index) {
        auto path = temporary.path() / ("track-" + std::to_string(index) + ".wav");
        createFile(path);
        roots.push_back(std::move(path));
    }

    std::mutex probe_mutex;
    std::condition_variable probe_changed;
    std::size_t probe_count = 0;
    SingleWorkerLocalSourceDiscovery discovery{{
        .batch_size = 1,
        .progress_interval = 64,
        .maximum_depth = 64,
        .before_inspect =
            [&] {
                {
                    std::lock_guard lock{probe_mutex};
                    ++probe_count;
                }
                probe_changed.notify_all();
            },
    }};

    REQUIRE(discovery.start({DiscoveryGeneration{1}, roots}) == DiscoveryCommandResult::Accepted);
    {
        std::unique_lock lock{probe_mutex};
        REQUIRE(probe_changed.wait_for(lock, 2s, [&] { return probe_count >= 2; }));
        CHECK(probe_count == 2);
    }

    const auto result = collectDiscovery(discovery, DiscoveryGeneration{1});
    CHECK(result.accepted_paths == roots);
    CHECK(result.batch_count == roots.size());
}

TEST_CASE("busy cancellation and generations prevent stale discovery results",
          "[m0][qt][core][source-discovery][cancel]") {
    ScopedTemporaryDirectory temporary;
    const auto first = temporary.path() / L"primeira.wav";
    const auto second = temporary.path() / L"segunda.wav";
    createFile(first);
    createFile(second);

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool entered = false;
    bool released = false;
    SingleWorkerLocalSourceDiscovery discovery{{
        .before_inspect =
            [&] {
                std::unique_lock lock{gate_mutex};
                if (entered)
                    return;
                entered = true;
                gate_changed.notify_all();
                gate_changed.wait(lock, [&] { return released; });
            },
        .cancel_synchronous_io =
            [&](std::thread::native_handle_type) noexcept {
                {
                    std::lock_guard lock{gate_mutex};
                    released = true;
                }
                gate_changed.notify_all();
            },
    }};

    REQUIRE(discovery.start({DiscoveryGeneration{1}, {first}}) == DiscoveryCommandResult::Accepted);
    {
        std::unique_lock lock{gate_mutex};
        REQUIRE(gate_changed.wait_for(lock, 2s, [&] { return entered; }));
    }
    CHECK(discovery.start({DiscoveryGeneration{2}, {second}}) == DiscoveryCommandResult::Busy);
    CHECK(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::Accepted);
    CHECK(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::Accepted);
    bool cancellation_acknowledged = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = discovery.poll(DiscoveryGeneration{1});
        if (result.status == DiscoveryPollStatus::Cancelled) {
            cancellation_acknowledged = true;
            break;
        }
        if (result.status != DiscoveryPollStatus::Pending)
            break;
        std::this_thread::yield();
    }
    CHECK(cancellation_acknowledged);
    if (!cancellation_acknowledged)
        return;
    REQUIRE(discovery.start({DiscoveryGeneration{2}, {second}}) == DiscoveryCommandResult::Accepted);
    CHECK(discovery.poll(DiscoveryGeneration{1}).status == DiscoveryPollStatus::StaleGeneration);

    const auto result = collectDiscovery(discovery, DiscoveryGeneration{2});
    CHECK(result.accepted_paths == std::vector<std::filesystem::path>{second});
    CHECK(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::StaleGeneration);
}

TEST_CASE("cancel and shutdown interrupt a worker before waiting for it",
          "[m0][qt][core][source-discovery][cancel][windows]") {
    ScopedTemporaryDirectory temporary;
    const auto source = temporary.path() / L"pendente.wav";
    createFile(source);

    const auto runScenario = [&](const bool cancel_first) {
        std::mutex gate_mutex;
        std::condition_variable gate_changed;
        bool entered = false;
        bool released = false;
        std::atomic_int interruption_count{0};
        SingleWorkerLocalSourceDiscovery discovery{{
            .before_inspect =
                [&] {
                    std::unique_lock lock{gate_mutex};
                    entered = true;
                    gate_changed.notify_all();
                    gate_changed.wait(lock, [&] { return released; });
                },
            .cancel_synchronous_io =
                [&](std::thread::native_handle_type) noexcept {
                    interruption_count.fetch_add(1, std::memory_order_relaxed);
                    {
                        std::lock_guard lock{gate_mutex};
                        released = true;
                    }
                    gate_changed.notify_all();
                },
        }};

        REQUIRE(discovery.start({DiscoveryGeneration{1}, {source}}) == DiscoveryCommandResult::Accepted);
        {
            std::unique_lock lock{gate_mutex};
            REQUIRE(gate_changed.wait_for(lock, 2s, [&] { return entered; }));
        }

        if (cancel_first) {
            CHECK(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::Accepted);
            CHECK(interruption_count.load(std::memory_order_relaxed) >= 1);
        } else {
            CHECK(discovery.shutdown() == DiscoveryCommandResult::Accepted);
            CHECK(interruption_count.load(std::memory_order_relaxed) >= 1);
        }
    };

    SECTION("cancel") { runScenario(true); }
    SECTION("shutdown") { runScenario(false); }
}

TEST_CASE("shutdown retries interruption until a worker blocked after ERROR_NOT_FOUND exits",
          "[m0][qt][core][source-discovery][cancel][shutdown][windows]") {
    ScopedTemporaryDirectory temporary;
    const auto source = temporary.path() / L"pendente.wav";
    createFile(source);

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool entered = false;
    bool later_io_started = false;
    bool force_release = false;
    std::atomic_int interruption_count{0};
    SingleWorkerLocalSourceDiscovery discovery{{
        .before_inspect =
            [&] {
                std::unique_lock lock{gate_mutex};
                entered = true;
                gate_changed.notify_all();
                // Attempt 1 is the logical cancel. Attempt 2 is shutdown observing no pending I/O.
                gate_changed.wait(
                    lock, [&] { return force_release || interruption_count.load(std::memory_order_relaxed) >= 2; });
                if (force_release)
                    return;

                // Model a synchronous I/O that begins just after shutdown received ERROR_NOT_FOUND.
                later_io_started = true;
                gate_changed.notify_all();
                gate_changed.wait(
                    lock, [&] { return force_release || interruption_count.load(std::memory_order_relaxed) >= 3; });
            },
        .cancel_synchronous_io =
            [&](std::thread::native_handle_type) noexcept {
                interruption_count.fetch_add(1, std::memory_order_relaxed);
                gate_changed.notify_all();
            },
    }};

    REQUIRE(discovery.start({DiscoveryGeneration{1}, {source}}) == DiscoveryCommandResult::Accepted);
    {
        std::unique_lock lock{gate_mutex};
        REQUIRE(gate_changed.wait_for(lock, 2s, [&] { return entered; }));
    }
    REQUIRE(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::Accepted);
    REQUIRE(interruption_count.load(std::memory_order_relaxed) == 1);

    DiscoveryCommandResult shutdown_result = DiscoveryCommandResult::Shutdown;
    std::jthread shutdown_thread{[&] { shutdown_result = discovery.shutdown(); }};
    {
        std::unique_lock lock{gate_mutex};
        const bool started_after_shutdown_interrupt = gate_changed.wait_for(lock, 2s, [&] { return later_io_started; });
        CHECK(started_after_shutdown_interrupt);
        const bool retried = started_after_shutdown_interrupt && gate_changed.wait_for(lock, 2s, [&] {
            return interruption_count.load(std::memory_order_relaxed) >= 3;
        });
        CHECK(retried);
        force_release = true;
    }

    // Always release the deterministic gate so a failing implementation can still unwind cleanly.
    gate_changed.notify_all();
    shutdown_thread.join();

    CHECK(shutdown_result == DiscoveryCommandResult::Accepted);
    CHECK(interruption_count.load(std::memory_order_relaxed) >= 3);
}

TEST_CASE("cancel retries until its active generation is acknowledged before accepting the next one",
          "[m0][qt][core][source-discovery][cancel][windows]") {
    ScopedTemporaryDirectory temporary;
    const auto first = temporary.path() / L"primeira.wav";
    const auto second = temporary.path() / L"segunda.wav";
    createFile(first);
    createFile(second);

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool entered = false;
    bool later_io_started = false;
    bool force_release = false;
    std::atomic_int interruption_count{0};
    SingleWorkerLocalSourceDiscovery discovery{{
        .before_inspect =
            [&] {
                std::unique_lock lock{gate_mutex};
                if (entered)
                    return;
                entered = true;
                gate_changed.notify_all();
                gate_changed.wait(
                    lock, [&] { return force_release || interruption_count.load(std::memory_order_relaxed) >= 1; });
                if (force_release)
                    return;

                // Model an I/O that begins immediately after the first cancel attempt observed no pending request.
                later_io_started = true;
                gate_changed.notify_all();
                gate_changed.wait(
                    lock, [&] { return force_release || interruption_count.load(std::memory_order_relaxed) >= 2; });
            },
        .cancel_synchronous_io =
            [&](std::thread::native_handle_type) noexcept {
                interruption_count.fetch_add(1, std::memory_order_relaxed);
                gate_changed.notify_all();
            },
    }};

    REQUIRE(discovery.start({DiscoveryGeneration{1}, {first}}) == DiscoveryCommandResult::Accepted);
    {
        std::unique_lock lock{gate_mutex};
        REQUIRE(gate_changed.wait_for(lock, 2s, [&] { return entered; }));
    }
    REQUIRE(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::Accepted);

    {
        std::unique_lock lock{gate_mutex};
        const bool started_after_cancel_interrupt = gate_changed.wait_for(lock, 2s, [&] { return later_io_started; });
        CHECK(started_after_cancel_interrupt);
    }
    const auto early_start_result = discovery.start({DiscoveryGeneration{2}, {second}});
    CHECK(early_start_result == DiscoveryCommandResult::Busy);
    const auto cancelling_poll = discovery.poll(DiscoveryGeneration{1});
    CHECK(cancelling_poll.status == DiscoveryPollStatus::Pending);
    CHECK(interruption_count.load(std::memory_order_relaxed) >= 2);

    {
        std::lock_guard lock{gate_mutex};
        force_release = true;
    }
    gate_changed.notify_all();

    if (early_start_result == DiscoveryCommandResult::Busy) {
        bool cancellation_acknowledged = false;
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = discovery.poll(DiscoveryGeneration{1});
            if (result.status == DiscoveryPollStatus::Cancelled) {
                cancellation_acknowledged = true;
                break;
            }
            if (result.status != DiscoveryPollStatus::Pending)
                break;
            std::this_thread::yield();
        }
        CHECK(cancellation_acknowledged);
        if (!cancellation_acknowledged)
            return;
        REQUIRE(discovery.start({DiscoveryGeneration{2}, {second}}) == DiscoveryCommandResult::Accepted);
    }
    const auto result = collectDiscovery(discovery, DiscoveryGeneration{2});
    CHECK(result.accepted_paths == std::vector<std::filesystem::path>{second});
}

#ifdef _WIN32
TEST_CASE("directory replacement after handle validation is never enumerated",
          "[m0][qt][core][source-discovery][security][windows]") {
    ScopedTemporaryDirectory temporary;
    const auto source = temporary.path() / L"origem";
    const auto retained = temporary.path() / L"origem-retida";
    REQUIRE(std::filesystem::create_directory(source));
    const auto original_track = source / L"original.mp3";
    createFile(original_track);

    bool replaced = false;
    bool replacement_failed = false;
    SingleWorkerLocalSourceDiscovery discovery{{
        .after_directory_handle_validated =
            [&](const std::filesystem::path& validated_path) {
                if (replaced || validated_path != source)
                    return;
                replaced = true;
                std::error_code replacement_error;
                std::filesystem::rename(source, retained, replacement_error);
                if (replacement_error || !std::filesystem::create_directory(source, replacement_error) ||
                    replacement_error) {
                    replacement_failed = true;
                    return;
                }
                std::ofstream replacement{source / L"substituta.mp3", std::ios::binary | std::ios::trunc};
                replacement_failed = !replacement;
            },
    }};

    REQUIRE(discovery.start({DiscoveryGeneration{1}, {source}}) == DiscoveryCommandResult::Accepted);
    const auto result = collectDiscovery(discovery, DiscoveryGeneration{1});

    REQUIRE(replaced);
    REQUIRE(!replacement_failed);
    CHECK(result.accepted_paths.empty());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == LocalSourceDiscoveryIssueCode::FilesystemError);
}
#endif

TEST_CASE("symlink roots are diagnosed and never traversed", "[m0][qt][core][source-discovery][security]") {
    ScopedTemporaryDirectory temporary;
    const auto target = temporary.path() / L"target";
    const auto link = temporary.path() / L"link";
    REQUIRE(std::filesystem::create_directory(target));
    createFile(target / L"hidden.mp3");
    std::error_code link_error;
    std::filesystem::create_directory_symlink(target, link, link_error);
    if (link_error) {
        WARN("the current Windows policy cannot create a test symlink: " << link_error.message());
        return;
    }

    SingleWorkerLocalSourceDiscovery discovery;
    REQUIRE(discovery.start({DiscoveryGeneration{1}, {link}}) == DiscoveryCommandResult::Accepted);
    const auto result = collectDiscovery(discovery, DiscoveryGeneration{1});

    CHECK(result.accepted_paths.empty());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == LocalSourceDiscoveryIssueCode::ReparsePoint);
}

TEST_CASE("discovery shutdown is idempotent and rejects new work", "[m0][qt][core][source-discovery][shutdown]") {
    SingleWorkerLocalSourceDiscovery discovery;
    CHECK(discovery.shutdown() == DiscoveryCommandResult::Accepted);
    CHECK(discovery.shutdown() == DiscoveryCommandResult::Accepted);
    CHECK(discovery.start({DiscoveryGeneration{1}, {std::filesystem::path{"after.mp3"}}}) ==
          DiscoveryCommandResult::Shutdown);
    CHECK(discovery.cancel(DiscoveryGeneration{1}) == DiscoveryCommandResult::Shutdown);
    CHECK(discovery.poll(DiscoveryGeneration{1}).status == DiscoveryPollStatus::Shutdown);
}

} // namespace music_player::m0::test
