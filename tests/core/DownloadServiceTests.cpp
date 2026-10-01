#include "m0/application/DownloadService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace music_player::m0::test {
namespace {

struct FakeExecution final : DownloadExecution {
    std::vector<std::pair<std::string, DownloadItem>> starts;
    std::vector<std::string> cancellations;
    std::vector<std::string> polls;
    DownloadExecutionUpdate update;
    bool accepted{true};
    bool throw_start{false};
    bool throw_poll{false};
    bool throw_cancel{false};
    std::function<void()> on_start;
    std::function<void()> on_poll;
    std::function<void()> on_cancel;

    bool start(const std::string& job_id, const DownloadItem& item) override {
        starts.emplace_back(job_id, item);
        update = {.job_id = job_id};
        if (on_start)
            on_start();
        if (throw_start)
            throw std::runtime_error("private adapter detail");
        return accepted;
    }
    void cancel(std::string_view job_id) override {
        cancellations.emplace_back(job_id);
        if (on_cancel)
            on_cancel();
        if (throw_cancel)
            throw std::runtime_error("private adapter detail");
    }
    DownloadExecutionUpdate poll(std::string_view job_id) override {
        polls.emplace_back(job_id);
        if (on_poll)
            on_poll();
        if (throw_poll)
            throw std::runtime_error("private adapter detail");
        return update;
    }
};

struct FakeImport final : DownloadImport {
    std::vector<std::pair<std::string, std::filesystem::path>> imports;
    bool accepted{true};
    bool throws{false};
    std::function<void()> on_accept;

    bool accept(const std::string& id, const std::filesystem::path& path) override {
        imports.emplace_back(id, path);
        if (on_accept)
            on_accept();
        if (throws)
            throw std::runtime_error("private import detail");
        return accepted;
    }
};

DownloadRequest request(const unsigned int id = 1, const std::size_t count = 1) {
    auto tail = std::to_string(id);
    tail.insert(0, 12 - tail.size(), '0');
    DownloadRequest result{.request_id = "abcdefab-cdef-4abc-8def-" + tail};
    for (std::size_t index = 0; index < count; ++index)
        result.items.push_back({.url = "https://media.example/track" + std::to_string(index) + ".mp4?token=private",
                                .title = "Track " + std::to_string(index),
                                .kind = DownloadMediaKind::Video});
    return result;
}

std::filesystem::path finalPath() {
#ifdef _WIN32
    return std::filesystem::path{L"C:/rc-music-synthetic/final.mp3"};
#else
    return std::filesystem::path{"/rc-music-synthetic/final.mp3"};
#endif
}

void published(FakeExecution& execution, const std::string& id) {
    execution.update = {.job_id = id,
                        .state = DownloadExecutionState::Published,
                        .phase = DownloadPhase::Publishing,
                        .completed_units = 1,
                        .total_units = 1,
                        .published_mp3 = finalPath()};
}

struct Harness {
    FakeExecution execution;
    FakeImport importer;
    DownloadService service;
    explicit Harness(DownloadServiceLimits limits = {}, std::string prefix = "test")
        : service(execution, importer, std::move(prefix), limits) {}
    std::vector<std::string> admit(const std::size_t count = 1) {
        auto result = service.submit(request(1, count));
        REQUIRE(result.error == DownloadSubmissionError::None);
        REQUIRE(result.job_ids.size() == count);
        return result.job_ids;
    }
};

} // namespace

TEST_CASE("Download admission owns the entire ordered batch without execution", "[download][FR-051]") {
    Harness h;
    auto input = request(1, 2);
    const auto expected = input;
    auto result = h.service.submit(input);
    REQUIRE(result.error == DownloadSubmissionError::None);
    REQUIRE(result.job_ids == std::vector<std::string>{"test-1", "test-2"});
    REQUIRE(h.execution.starts.empty());
    input.items[0].title = "Changed";
    input.items[0].url = "https://different.example/";
    input.items.clear();
    const auto jobs = h.service.snapshot();
    REQUIRE(jobs.size() == 2);
    REQUIRE(jobs[0].title == expected.items[0].title);
    REQUIRE(jobs[0].state == DownloadJobState::Queued);
    REQUIRE_FALSE(jobs[0].total_units.has_value());
    h.service.tick(0);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.starts[0].second == expected.items[0]);
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("Download receipts deduplicate UUID casing and survive completion at capacity", "[download][FR-051]") {
    Harness h({.max_jobs = 1, .max_requests = 1});
    auto input = request();
    const auto first = h.service.submit(input);
    REQUIRE(first.error == DownloadSubmissionError::None);
    std::transform(input.request_id.begin(), input.request_id.end(), input.request_id.begin(), [](char value) {
        return value >= 'a' && value <= 'f' ? static_cast<char>(value - 'a' + 'A') : value;
    });
    REQUIRE(h.service.submit(input).job_ids == first.job_ids);
    h.service.tick(0);
    published(h.execution, first.job_ids[0]);
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.submit(input).job_ids == first.job_ids);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::LimitExceeded);
}

TEST_CASE("A conflicting request ID refuses the whole batch without changing receipts", "[download][FR-051]") {
    Harness h;
    auto input = request(1, 2);
    const auto first = h.service.submit(input);
    REQUIRE(first.error == DownloadSubmissionError::None);
    SECTION("changed title") { input.items[0].title += " changed"; }
    SECTION("changed URL") { input.items[0].url += "new"; }
    SECTION("changed kind") { input.items[0].kind = DownloadMediaKind::Audio; }
    SECTION("changed order") { std::swap(input.items[0], input.items[1]); }
    SECTION("changed size") { input.items.pop_back(); }
    REQUIRE(h.service.submit(input).error == DownloadSubmissionError::InvalidRequest);
    REQUIRE(h.service.snapshot().size() == 2);
    REQUIRE(h.service.submit(request(1, 2)).job_ids == first.job_ids);
}

TEST_CASE("Typed admission validates UUID items enum and byte boundaries atomically", "[download][FR-051]") {
    Harness h;
    auto input = request();
    SECTION("empty UUID") { input.request_id.clear(); }
    SECTION("UUID version") { input.request_id[14] = '3'; }
    SECTION("UUID variant") { input.request_id[19] = '7'; }
    SECTION("UUID length") { input.request_id += 'a'; }
    SECTION("UUID separator") { input.request_id[8] = 'a'; }
    SECTION("UUID hex") { input.request_id[0] = 'g'; }
    SECTION("empty batch") { input.items.clear(); }
    SECTION("oversize batch") { input = request(1, 21); }
    SECTION("empty URL") { input.items[0].url.clear(); }
    SECTION("URL bytes") { input.items[0].url.assign(8193, 'x'); }
    SECTION("empty title") { input.items[0].title.clear(); }
    SECTION("title bytes") { input.items[0].title.assign(801, 'x'); }
    SECTION("URL embedded NUL") { input.items[0].url.push_back('\0'); }
    SECTION("title embedded NUL") { input.items[0].title.push_back('\0'); }
    SECTION("unknown kind") { input.items[0].kind = static_cast<DownloadMediaKind>(99); }
    SECTION("one bad item rejects all") { input.items.push_back({}); }
    const auto result = h.service.submit(input);
    REQUIRE(result.error == DownloadSubmissionError::InvalidRequest);
    REQUIRE(result.job_ids.empty());
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.service.submit(request()).job_ids == std::vector<std::string>{"test-1"});
}

TEST_CASE("Typed admission retains valid maximum batches and byte lengths", "[download][FR-051]") {
    Harness h;
    auto input = request(1, 20);
    input.items[0].url.assign(8192, 'x');
    input.items[0].title.assign(800, 't');
    REQUIRE(h.service.submit(input).error == DownloadSubmissionError::None);
    REQUIRE(h.service.snapshot().size() == 20);
}

TEST_CASE("Download retention caps reject atomically and never evict receipts", "[download][FR-051]") {
    DownloadServiceLimits limits;
    SECTION("jobs") { limits.max_jobs = 2; }
    SECTION("requests") { limits.max_requests = 1; }
    Harness h(limits);
    const auto accepted = h.service.submit(request(1, 2));
    REQUIRE(accepted.error == DownloadSubmissionError::None);
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.service.snapshot().size() == 2);
    REQUIRE(h.service.submit(request(1, 2)).job_ids == accepted.job_ids);
    REQUIRE(h.service.cancel(accepted.job_ids[0]));
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::LimitExceeded);
}

TEST_CASE("Download retained request byte budget is exact and overflow safe", "[download][FR-051]") {
    const auto input = request();
    const auto bytes = input.request_id.size() + input.items[0].url.size() + input.items[0].title.size();
    SECTION("exact") {
        Harness h({.max_retained_request_bytes = bytes});
        REQUIRE(h.service.submit(input).error == DownloadSubmissionError::None);
        REQUIRE(h.service.submit(input).error == DownloadSubmissionError::None);
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::LimitExceeded);
    }
    SECTION("one below") {
        Harness h({.max_retained_request_bytes = bytes - 1});
        REQUIRE(h.service.submit(input).error == DownloadSubmissionError::LimitExceeded);
        REQUIRE(h.service.snapshot().empty());
    }
    SECTION("large configuration") {
        Harness h({.max_retained_request_bytes = std::numeric_limits<std::size_t>::max()});
        REQUIRE(h.service.submit(input).error == DownloadSubmissionError::None);
    }
}

TEST_CASE("Invalid service identity and disabled limits never admit jobs", "[download][FR-051]") {
    DownloadServiceLimits limits;
    std::string prefix = "test";
    SECTION("empty") { prefix.clear(); }
    SECTION("too long") { prefix.assign(41, 'x'); }
    SECTION("path") { prefix = "../test"; }
    SECTION("space") { prefix = "test space"; }
    SECTION("non ASCII") { prefix = "t\xC3\xA9st"; }
    SECTION("zero jobs") { limits.max_jobs = 0; }
    SECTION("zero requests") { limits.max_requests = 0; }
    SECTION("zero bytes") { limits.max_retained_request_bytes = 0; }
    SECTION("zero timeout") { limits.job_timeout_ms = 0; }
    SECTION("negative timeout") { limits.job_timeout_ms = -1; }
    Harness h(limits, prefix);
    REQUIRE(h.service.submit(request()).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.snapshot().empty());
}

TEST_CASE("Safe maximum identity prefixes produce distinct receipt-safe IDs", "[download][FR-051]") {
    Harness h({}, std::string(37, 'A') + "_0-");
    const auto ids = h.admit(20);
    REQUIRE(ids.front() != ids.back());
    REQUIRE(ids.back().size() <= 64);
}

TEST_CASE("The worker runs one FIFO job and never imports running files", "[download][FR-051]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(10);
    h.service.tick(11);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.starts[0].first == ids[0]);
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Queued);
    REQUIRE(h.importer.imports.empty());
    published(h.execution, ids[0]);
    h.service.tick(12);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    h.service.tick(13);
    REQUIRE(h.execution.starts.size() == 2);
    REQUIRE(h.execution.starts[1].first == ids[1]);
    REQUIRE(h.importer.imports == std::vector<std::pair<std::string, std::filesystem::path>>{{ids[0], finalPath()}});
}

TEST_CASE("Progress preserves unknown totals and resets only when advancing phase", "[download][FR-051]") {
    Harness h;
    const auto ids = h.admit();
    h.service.tick(0);
    h.execution.update.completed_units = 5;
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].completed_units == 5);
    REQUIRE_FALSE(h.service.snapshot()[0].total_units.has_value());
    h.execution.update.total_units = 10;
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].total_units == 10);
    h.execution.update.phase = DownloadPhase::Converting;
    h.execution.update.completed_units = 0;
    h.execution.update.total_units.reset();
    h.service.tick(3);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Converting);
    REQUIRE(h.service.snapshot()[0].completed_units == 0);
    h.execution.update.phase = DownloadPhase::Publishing;
    h.execution.update.total_units = 1;
    h.service.tick(4);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Publishing);
    published(h.execution, ids[0]);
    h.service.tick(5);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
}

TEST_CASE("Stale worker snapshots cannot change or import the active job", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    published(h.execution, ids[1]);
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Downloading);
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations.empty());
    published(h.execution, ids[0]);
    h.service.tick(2);
    REQUIRE(h.importer.imports.size() == 1);
}

TEST_CASE("Invalid running snapshots cancel safely until acknowledged", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    h.execution.update.completed_units = 5;
    h.execution.update.total_units = 10;
    h.service.tick(1);
    SECTION("decreasing progress") { h.execution.update.completed_units = 4; }
    SECTION("exceeds total") { h.execution.update.completed_units = 11; }
    SECTION("unknown phase") { h.execution.update.phase = static_cast<DownloadPhase>(42); }
    SECTION("unknown state") { h.execution.update.state = static_cast<DownloadExecutionState>(42); }
    SECTION("running error") { h.execution.update.error = DownloadFailure::Network; }
    SECTION("running file") { h.execution.update.published_mp3 = finalPath(); }
    SECTION("phase regression") {
        h.execution.update.phase = DownloadPhase::Converting;
        h.service.tick(2);
        h.execution.update.phase = DownloadPhase::Downloading;
    }
    h.service.tick(3);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    REQUIRE(h.importer.imports.empty());
    h.service.tick(4);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations.size() == 1);
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(5);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
}

TEST_CASE("Only well formed successful publication reaches local import", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit();
    h.service.tick(0);
    published(h.execution, ids[0]);
    SECTION("empty path") { h.execution.update.published_mp3.clear(); }
    SECTION("relative path") { h.execution.update.published_mp3 = "relative.mp3"; }
    SECTION("wrong extension") { h.execution.update.published_mp3.replace_extension(".mp4"); }
    SECTION("non normalized path") { h.execution.update.published_mp3 = finalPath().parent_path() / "x/../final.mp3"; }
    SECTION("publication error") { h.execution.update.error = DownloadFailure::Publication; }
    SECTION("wrong phase") { h.execution.update.phase = DownloadPhase::Downloading; }
    SECTION("invalid progress") { h.execution.update.completed_units = 2; }
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE_FALSE(h.service.retryImport(ids[0]));
}

TEST_CASE("Typed failures remain actionable and invalid failures are sanitized", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit();
    h.service.tick(0);
    auto error = DownloadFailure::Network;
    auto expected = error;
    SECTION("network") {}
    SECTION("expired URL") { expected = error = DownloadFailure::Expired; }
    SECTION("restricted") { expected = error = DownloadFailure::Restricted; }
    SECTION("no audio") { expected = error = DownloadFailure::NoAudio; }
    SECTION("conversion") { expected = error = DownloadFailure::Conversion; }
    SECTION("failure without error") {
        error = DownloadFailure::None;
        expected = DownloadFailure::InvalidResult;
    }
    SECTION("invalid error enum") {
        error = static_cast<DownloadFailure>(99);
        expected = DownloadFailure::InvalidResult;
    }
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Failed, .error = error};
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == expected);
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("Import failure retains the final MP3 for explicit idempotent retry", "[download][FR-051]") {
    Harness h;
    const auto ids = h.admit();
    SECTION("refused import") { h.importer.accepted = false; }
    SECTION("throwing import") { h.importer.throws = true; }
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::ImportFailed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Import);
    REQUIRE(h.service.snapshot()[0].completed_mp3 == finalPath());
    h.service.tick(2);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.execution.starts.size() == 1);
    h.importer.accepted = true;
    h.importer.throws = false;
    REQUIRE(h.service.retryImport(ids[0]));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::None);
    REQUIRE(h.importer.imports.size() == 2);
    REQUIRE(h.importer.imports[0] == h.importer.imports[1]);
    REQUIRE_FALSE(h.service.retryImport(ids[0]));
    REQUIRE(h.execution.starts.size() == 1);
}

TEST_CASE("Queued cancellation is immediate and preserves the receipt", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    REQUIRE(h.service.cancel(ids[0]));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelled);
    REQUIRE_FALSE(h.service.cancel(ids[0]));
    REQUIRE_FALSE(h.service.cancel("unknown"));
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.service.submit(request(1, 2)).job_ids == ids);
    h.service.tick(0);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.starts[0].first == ids[1]);
}

TEST_CASE("Active cancellation waits for an acknowledgement and never imports a late file", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    REQUIRE(h.service.cancel(ids[0]));
    REQUIRE_FALSE(h.service.cancel(ids[0]));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    h.service.tick(1);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    auto expected_state = DownloadJobState::Cancelled;
    auto expected_error = DownloadFailure::None;
    SECTION("cancelled") { h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled}; }
    SECTION("late published") { published(h.execution, ids[0]); }
    SECTION("late failed") {
        h.execution.update = {
            .job_id = ids[0], .state = DownloadExecutionState::Failed, .error = DownloadFailure::Network};
        expected_state = DownloadJobState::Failed;
        expected_error = DownloadFailure::Network;
    }
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == expected_state);
    REQUIRE(h.service.snapshot()[0].error == expected_error);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    h.service.tick(3);
    REQUIRE(h.execution.starts.size() == 2);
}

TEST_CASE("Cancellation and shutdown retain typed cleanup failures after acknowledgement", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    bool shutdown = false;
    SECTION("explicit cancellation") { REQUIRE(h.service.cancel(ids[0])); }
    SECTION("shutdown") {
        shutdown = true;
        h.service.shutdown();
    }
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE_FALSE(h.service.stopped());
    h.execution.update = {
        .job_id = ids[0], .state = DownloadExecutionState::Failed, .error = DownloadFailure::Publication};
    h.service.tick(2);
    const auto job = h.service.snapshot()[0];
    REQUIRE(job.state == DownloadJobState::Failed);
    REQUIRE(job.error == DownloadFailure::Publication);
    REQUIRE(job.completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE_FALSE(h.service.retryImport(ids[0]));
    REQUIRE(h.service.stopped() == shutdown);
    h.service.tick(3);
    REQUIRE(h.execution.starts.size() == (shutdown ? 1 : 2));
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Publication);
}

TEST_CASE("Cancellation acknowledgements do not need the last progress counters", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit();
    h.service.tick(0);
    h.execution.update.phase = DownloadPhase::Converting;
    h.execution.update.completed_units = 10;
    h.execution.update.total_units = 20;
    h.service.tick(1);
    REQUIRE(h.service.cancel(ids[0]));
    auto expected_state = DownloadJobState::Cancelled;
    auto expected_error = DownloadFailure::None;
    SECTION("cancelled resets progress") {
        h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    }
    SECTION("cleanup failure resets progress") {
        h.execution.update = {
            .job_id = ids[0], .state = DownloadExecutionState::Failed, .error = DownloadFailure::Publication};
        expected_state = DownloadJobState::Failed;
        expected_error = DownloadFailure::Publication;
    }
    SECTION("cancelled drops an obsolete total") {
        h.execution.update.state = DownloadExecutionState::Cancelled;
        h.execution.update.completed_units = 5;
        h.execution.update.total_units = 1;
    }
    h.service.tick(2);
    const auto job = h.service.snapshot()[0];
    REQUIRE(job.state == expected_state);
    REQUIRE(job.error == expected_error);
    REQUIRE(job.completed_units == 10);
    REQUIRE(job.total_units == 20);
    REQUIRE(job.completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("Malformed cancellation terminals fail safely without exposing a file", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    REQUIRE(h.service.cancel(ids[0]));
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    SECTION("cancelled with error") { h.execution.update.error = DownloadFailure::Publication; }
    SECTION("cancelled with file") { h.execution.update.published_mp3 = finalPath(); }
    SECTION("unknown phase") { h.execution.update.phase = static_cast<DownloadPhase>(42); }
    SECTION("unknown failure") { h.execution.update.error = static_cast<DownloadFailure>(42); }
    SECTION("failed without error") { h.execution.update.state = DownloadExecutionState::Failed; }
    SECTION("failed with file") {
        h.execution.update.state = DownloadExecutionState::Failed;
        h.execution.update.error = DownloadFailure::Publication;
        h.execution.update.published_mp3 = finalPath();
    }
    SECTION("published without file") {
        h.execution.update.state = DownloadExecutionState::Published;
        h.execution.update.phase = DownloadPhase::Publishing;
    }
    SECTION("published relative file") {
        published(h.execution, ids[0]);
        h.execution.update.published_mp3 = "relative.mp3";
    }
    SECTION("published non MP3") {
        published(h.execution, ids[0]);
        h.execution.update.published_mp3.replace_extension(".mp4");
    }
    SECTION("published with error") {
        published(h.execution, ids[0]);
        h.execution.update.error = DownloadFailure::Publication;
    }
    SECTION("published wrong phase") {
        published(h.execution, ids[0]);
        h.execution.update.phase = DownloadPhase::Downloading;
    }
    h.service.tick(1);
    const auto job = h.service.snapshot()[0];
    REQUIRE(job.state == DownloadJobState::Failed);
    REQUIRE(job.error == DownloadFailure::InvalidResult);
    REQUIRE(job.completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    h.service.tick(2);
    REQUIRE(h.execution.starts.size() == 2);
}

TEST_CASE("An invalid cancellation update does not acknowledge stopped work", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    REQUIRE(h.service.cancel(ids[0]));
    SECTION("unknown state") { h.execution.update.state = static_cast<DownloadExecutionState>(42); }
    SECTION("running with error") { h.execution.update.error = DownloadFailure::Publication; }
    SECTION("running with file") { h.execution.update.published_mp3 = finalPath(); }
    SECTION("running unknown phase") { h.execution.update.phase = static_cast<DownloadPhase>(42); }
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
    h.service.tick(2);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(3);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
    REQUIRE(h.importer.imports.empty());
    h.service.tick(4);
    REQUIRE(h.execution.starts.size() == 2);
}

TEST_CASE("Cancellation ignores stale cleanup failures and keeps the active drain", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.service.tick(0);
    REQUIRE(h.service.cancel(ids[0]));
    h.execution.update = {
        .job_id = ids[1], .state = DownloadExecutionState::Failed, .error = DownloadFailure::Publication};
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::None);
    REQUIRE(h.execution.starts.size() == 1);
    h.execution.update.job_id = ids[0];
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Publication);
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("A primary service failure survives every cancellation terminal", "[download][FR-053]") {
    const auto primary = GENERATE(DownloadFailure::Timeout, DownloadFailure::InvalidResult);
    Harness h({.job_timeout_ms = 10});
    const auto ids = h.admit(2);
    h.service.tick(0);
    if (primary == DownloadFailure::Timeout) {
        h.service.tick(10);
    } else {
        h.execution.update.error = DownloadFailure::Network;
        h.service.tick(1);
    }
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == primary);
    SECTION("cleanup failure") {
        h.execution.update = {
            .job_id = ids[0], .state = DownloadExecutionState::Failed, .error = DownloadFailure::Publication};
    }
    SECTION("cancelled") { h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled}; }
    SECTION("late published") { published(h.execution, ids[0]); }
    SECTION("malformed terminal") { h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Failed}; }
    h.service.tick(11);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == primary);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    h.service.tick(12);
    REQUIRE(h.execution.starts.size() == 2);
}

TEST_CASE("Polling exceptions during cancellation record an error but do not acknowledge stopping",
          "[download][FR-053]") {
    const auto initial = GENERATE(DownloadFailure::None, DownloadFailure::Timeout, DownloadFailure::InvalidResult);
    const auto shutdown = GENERATE(false, true);
    Harness h({.job_timeout_ms = 10});
    const auto ids = h.admit(2);
    h.service.tick(100);
    std::int64_t next_tick = 101;
    if (initial == DownloadFailure::Timeout) {
        h.service.tick(110);
        next_tick = 111;
    } else if (initial == DownloadFailure::InvalidResult) {
        h.execution.update.error = DownloadFailure::Network;
        h.service.tick(101);
        next_tick = 102;
    } else if (!shutdown) {
        REQUIRE(h.service.cancel(ids[0]));
    }
    if (shutdown)
        h.service.shutdown();
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == initial);
    h.execution.throw_poll = true;
    h.service.tick(next_tick);
    const auto expected = initial == DownloadFailure::None ? DownloadFailure::InvalidResult : initial;
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == expected);
    REQUIRE_FALSE(h.service.stopped());
    h.service.tick(next_tick + 1);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    h.execution.throw_poll = false;
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(next_tick + 2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == expected);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.service.stopped() == shutdown);
    h.service.tick(next_tick + 3);
    REQUIRE(h.execution.starts.size() == (shutdown ? 1 : 2));
}

TEST_CASE("Cancellation preserves the first deadline or clock failure until stopped work is acknowledged",
          "[download][FR-053]") {
    const auto shutdown = GENERATE(false, true);
    Harness h({.job_timeout_ms = 10});
    const auto ids = h.admit(2);
    h.service.tick(100);
    if (shutdown)
        h.service.shutdown();
    else
        REQUIRE(h.service.cancel(ids[0]));
    h.service.tick(109);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::None);
    auto expected = DownloadFailure::Timeout;
    SECTION("inclusive deadline before backwards clock") {
        h.service.tick(110);
        REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Timeout);
        h.service.tick(109);
    }
    SECTION("backwards clock before inclusive deadline") {
        expected = DownloadFailure::InvalidResult;
        h.service.tick(108);
        REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
        h.service.tick(110);
    }
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == expected);
    REQUIRE_FALSE(h.service.stopped());
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(111);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == expected);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.service.stopped() == shutdown);
    h.service.tick(112);
    REQUIRE(h.execution.starts.size() == (shutdown ? 1 : 2));
}

TEST_CASE("Total timeout is inclusive does not reset on progress and waits for cancellation", "[download][FR-053]") {
    Harness h({.job_timeout_ms = 10});
    const auto ids = h.admit(2);
    h.service.tick(100);
    h.execution.update.completed_units = 1;
    h.service.tick(109);
    REQUIRE(h.execution.cancellations.empty());
    h.service.tick(110);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Timeout);
    h.service.tick(111);
    REQUIRE(h.execution.starts.size() == 1);
    published(h.execution, ids[0]);
    h.service.tick(112);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Timeout);
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("Clock boundaries never overflow or extend an active deadline", "[download][FR-053]") {
    Harness h({.job_timeout_ms = 10});
    const auto ids = h.admit();
    SECTION("negative clock") {
        h.service.tick(-20);
        h.service.tick(-11);
        REQUIRE(h.execution.cancellations.empty());
        h.service.tick(-10);
    }
    SECTION("signed span") {
        h.service.tick(std::numeric_limits<std::int64_t>::min());
        h.service.tick(std::numeric_limits<std::int64_t>::max());
    }
    SECTION("near maximum") {
        h.service.tick(std::numeric_limits<std::int64_t>::max() - 10);
        h.service.tick(std::numeric_limits<std::int64_t>::max() - 1);
        REQUIRE(h.execution.cancellations.empty());
        h.service.tick(std::numeric_limits<std::int64_t>::max());
    }
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Timeout);
}

TEST_CASE("A backwards active clock fails closed rather than renewing the timeout", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit();
    h.service.tick(100);
    h.service.tick(101);
    h.service.tick(100);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
}

TEST_CASE("Shutdown rejects admission cancels queued jobs and drains only its active worker", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    REQUIRE_FALSE(h.service.stopped());
    h.service.tick(0);
    h.service.shutdown();
    h.service.shutdown();
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.submit(request(1, 2)).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Cancelled);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    REQUIRE_FALSE(h.service.stopped());
    h.service.tick(1);
    REQUIRE_FALSE(h.service.stopped());
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(2);
    REQUIRE(h.service.stopped());
    h.service.tick(3);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("An unused service closes immediately and cannot retry imports after shutdown", "[download][FR-053]") {
    Harness h;
    SECTION("empty") {
        h.service.shutdown();
        REQUIRE(h.service.stopped());
        REQUIRE(h.execution.starts.empty());
    }
    SECTION("failed import") {
        const auto ids = h.admit();
        h.importer.accepted = false;
        h.service.tick(0);
        published(h.execution, ids[0]);
        h.service.tick(1);
        h.service.shutdown();
        REQUIRE(h.service.stopped());
        REQUIRE_FALSE(h.service.retryImport(ids[0]));
        REQUIRE(h.importer.imports.size() == 1);
    }
}

TEST_CASE("A refused worker start fails once and leaves later jobs runnable", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    h.execution.accepted = false;
    h.service.tick(0);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Unavailable);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.cancellations.empty());
    h.execution.accepted = true;
    h.service.tick(1);
    REQUIRE(h.execution.starts.size() == 2);
    REQUIRE(h.execution.starts[1].first == ids[1]);
}

TEST_CASE("Adapter exceptions never imply completion or launch overlapping jobs", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit(2);
    SECTION("start threw after possible launch") { h.execution.throw_start = true; }
    SECTION("poll threw") { h.execution.throw_poll = true; }
    h.execution.throw_cancel = true;
    h.service.tick(0);
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelling);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::InvalidResult);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{ids[0]});
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.importer.imports.empty());
    h.execution.throw_poll = false;
    h.execution.update = {.job_id = ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Failed);
    h.execution.throw_start = false;
    h.service.tick(3);
    REQUIRE(h.execution.starts.size() == 2);
}

TEST_CASE("Reentrant execution callbacks cannot duplicate mutate or close admission", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit();
    auto callback = [&] {
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
        REQUIRE_FALSE(h.service.cancel(ids[0]));
        REQUIRE_FALSE(h.service.retryImport(ids[0]));
        h.service.tick(0);
        h.service.shutdown();
        REQUIRE_FALSE(h.service.stopped());
        REQUIRE(h.service.snapshot().size() == 1);
    };
    h.execution.on_start = callback;
    h.execution.on_poll = callback;
    h.execution.on_cancel = callback;
    h.service.tick(0);
    h.service.tick(1);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.service.cancel(ids[0]));
    REQUIRE(h.execution.cancellations.size() == 1);
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::None);
}

TEST_CASE("Reentrant import callbacks cannot hand off a job twice", "[download][FR-053]") {
    Harness h;
    const auto ids = h.admit();
    h.importer.on_accept = [&] {
        REQUIRE_FALSE(h.service.retryImport(ids[0]));
        REQUIRE_FALSE(h.service.cancel(ids[0]));
        h.service.tick(1);
        h.service.shutdown();
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
    };
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::None);
}

TEST_CASE("A handoff observes publishing before accepting the final local file", "[download][FR-051]") {
    Harness h;
    const auto ids = h.admit();
    std::optional<DownloadJobSnapshot> during_handoff;
    h.importer.on_accept = [&] { during_handoff = h.service.snapshot()[0]; };
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    REQUIRE(during_handoff.has_value());
    REQUIRE(during_handoff->state == DownloadJobState::Publishing);
    REQUIRE(during_handoff->completed_mp3 == finalPath());
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
}

} // namespace music_player::m0::test
