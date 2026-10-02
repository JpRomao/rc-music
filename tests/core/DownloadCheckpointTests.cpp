#include "m0/application/DownloadService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>

namespace music_player::m0::test {
namespace {
struct Execution final : DownloadExecution {
    int starts{0};
    int polls{0};
    int cancels{0};
    DownloadExecutionUpdate update{};
    bool start(const std::string& id, const DownloadItem&) override {
        ++starts;
        update.job_id = id;
        return true;
    }
    void cancel(std::string_view) override { ++cancels; }
    DownloadExecutionUpdate poll(std::string_view) override {
        ++polls;
        return update;
    }
};
struct Import final : DownloadImport {
    int calls{0};
    bool accepted{true};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return accepted;
    }
};
struct Identity final : DownloadRequestIdentity {
    int calls{0};
    bool throws{false};
    std::string result = std::string(64, 'a');
    std::function<void()> callback{};
    std::string fingerprint(const DownloadRequest&) override {
        ++calls;
        if (callback)
            callback();
        if (throws)
            throw std::runtime_error{"private source"};
        return result;
    }
};
DownloadRequest input(unsigned int id = 1) {
    std::string tail = std::to_string(id);
    tail.insert(0, 12 - tail.size(), '0');
    return {.request_id = "abcdefab-cdef-4abc-8def-" + tail,
            .items = {{.url = "https://media.example/audio.mp3?token=private",
                       .title = "Synthetic audio",
                       .kind = DownloadMediaKind::Audio}}};
}
std::filesystem::path candidate() {
#ifdef _WIN32
    return std::filesystem::path{L"C:/rc-music-synthetic/final.mp3"};
#else
    return std::filesystem::path{"/rc-music-synthetic/final.mp3"};
#endif
}
DownloadCheckpoint saved(DownloadJobState state = DownloadJobState::Queued) {
    const auto request = input();
    return {.receipts = {{.request_id = request.request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"old-1"},
                          .retained_request_bytes =
                              request.request_id.size() + request.items[0].url.size() + request.items[0].title.size()}},
            .jobs = {{.job_id = "old-1", .title = request.items[0].title, .state = state}}};
}
struct Harness {
    Execution execution{};
    Import importer{};
    Identity identity{};
    DownloadService service;
    explicit Harness(DownloadServiceLimits limits = {}, std::string prefix = "new")
        : service(execution, importer, std::move(prefix), limits, &identity) {}
};
} // namespace

TEST_CASE("Download checkpoint captures bounded receipts without source URLs", "[download][checkpoint][FR-051]") {
    Harness h;
    auto request = input();
    const auto result = h.service.submit(request);
    REQUIRE(result.error == DownloadSubmissionError::None);
    const auto checkpoint = h.service.checkpoint();
    REQUIRE(checkpoint.has_value());
    REQUIRE(validDownloadCheckpoint(*checkpoint));
    REQUIRE(checkpoint->jobs == h.service.snapshot());
    REQUIRE(checkpoint->receipts.size() == 1);
    REQUIRE(checkpoint->receipts[0].job_ids == result.job_ids);
    REQUIRE(checkpoint->receipts[0].fingerprint == std::string(64, 'a'));
    REQUIRE(checkpoint->receipts[0].retained_request_bytes ==
            request.request_id.size() + request.items[0].url.size() + request.items[0].title.size());
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
    request.items[0].title = "Changed outside owner";
    REQUIRE(h.service.checkpoint() == checkpoint);
}

TEST_CASE("Download checkpoint identity is optional for legacy in-memory ownership", "[download][checkpoint]") {
    Execution execution;
    Import importer;
    DownloadService service(execution, importer, "legacy");
    REQUIRE(service.submit(input()).error == DownloadSubmissionError::None);
    REQUIRE_FALSE(service.checkpoint().has_value());
    REQUIRE_FALSE(service.restore(saved()));
}

TEST_CASE("Download recovery interrupts queued and active work without executing ports",
          "[download][checkpoint][FR-053]") {
    const auto state =
        GENERATE(DownloadJobState::Queued, DownloadJobState::Downloading, DownloadJobState::Converting,
                 DownloadJobState::Publishing, DownloadJobState::Cancelling, DownloadJobState::ImportFailed);
    auto checkpoint = saved(state);
    if (state == DownloadJobState::ImportFailed) {
        checkpoint.jobs[0].completed_mp3 = candidate();
        checkpoint.jobs[0].error = DownloadFailure::Import;
    }
    Harness h;
    REQUIRE(h.service.restore(checkpoint));
    const auto job = h.service.snapshot()[0];
    REQUIRE(job.state == DownloadJobState::Interrupted);
    REQUIRE(job.error == DownloadFailure::Unavailable);
    REQUIRE(job.completed_mp3.empty());
    REQUIRE_FALSE(h.service.retryImport(job.job_id));
    REQUIRE_FALSE(h.service.cancel(job.job_id));
    h.service.tick(0);
    h.service.tick(std::numeric_limits<std::int64_t>::max());
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.execution.polls == 0);
    REQUIRE(h.execution.cancels == 0);
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.service.checkpoint()->jobs[0].completed_mp3 == checkpoint.jobs[0].completed_mp3);
}

TEST_CASE("Download recovery retains completed history but no import authority", "[download][checkpoint][FR-053]") {
    auto checkpoint = saved(DownloadJobState::Completed);
    checkpoint.jobs[0].completed_mp3 = candidate();
    checkpoint.jobs[0].completed_units = std::numeric_limits<std::uint64_t>::max();
    checkpoint.jobs[0].total_units = std::numeric_limits<std::uint64_t>::max();
    Harness h;
    REQUIRE(h.service.restore(checkpoint));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE_FALSE(h.service.retryImport("old-1"));
    h.service.tick(0);
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.service.checkpoint() == checkpoint);
}

TEST_CASE("Download recovery preserves typed failure and cancelled history", "[download][checkpoint]") {
    const auto state = GENERATE(DownloadJobState::Failed, DownloadJobState::Cancelled, DownloadJobState::Interrupted);
    auto checkpoint = saved(state);
    if (state != DownloadJobState::Cancelled)
        checkpoint.jobs[0].error = DownloadFailure::Publication;
    Harness h;
    REQUIRE(h.service.restore(checkpoint));
    REQUIRE(h.service.snapshot()[0].state == state);
    REQUIRE(h.service.snapshot()[0].error == checkpoint.jobs[0].error);
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-1"});
    REQUIRE(h.service.snapshot().size() == 1);
    REQUIRE(h.execution.starts == 0);
}

TEST_CASE("Recovered request identity rejects conflicting batches without losing receipt",
          "[download][checkpoint][FR-051]") {
    Harness h;
    REQUIRE(h.service.restore(saved()));
    auto request = input();
    std::transform(request.request_id.begin(), request.request_id.end(), request.request_id.begin(), [](char value) {
        return value >= 'a' && value <= 'f' ? static_cast<char>(value - 'a' + 'A') : value;
    });
    REQUIRE(h.service.submit(request).job_ids == std::vector<std::string>{"old-1"});
    request.items[0].url += "changed";
    h.identity.result = std::string(64, 'b');
    REQUIRE(h.service.submit(request).error == DownloadSubmissionError::InvalidRequest);
    REQUIRE(h.service.snapshot().size() == 1);
    h.identity.result = std::string(64, 'a');
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-1"});
}

TEST_CASE("Recovery preserves capacity and permits replay at inclusive full limits", "[download][checkpoint]") {
    const auto checkpoint = saved();
    Harness h({.max_jobs = 1,
               .max_requests = 1,
               .max_retained_request_bytes = checkpoint.receipts[0].retained_request_bytes});
    REQUIRE(h.service.restore(checkpoint));
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-1"});
    REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::LimitExceeded);
}

TEST_CASE("Recovery restores atomically into only pristine open owners with fresh namespaces",
          "[download][checkpoint]") {
    SECTION("existing receipt") {
        Harness h;
        REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::None);
        const auto before = h.service.snapshot();
        REQUIRE_FALSE(h.service.restore(saved()));
        REQUIRE(h.service.snapshot() == before);
    }
    SECTION("closed owner") {
        Harness h;
        h.service.shutdown();
        REQUIRE_FALSE(h.service.restore(saved()));
    }
    SECTION("same namespace") {
        Harness h({}, "old");
        REQUIRE_FALSE(h.service.restore(saved()));
        REQUIRE(h.service.snapshot().empty());
    }
    SECTION("smaller configured budget") {
        Harness h({.max_retained_request_bytes = 1});
        REQUIRE_FALSE(h.service.restore(saved()));
        REQUIRE(h.service.snapshot().empty());
    }
    SECTION("new owner IDs") {
        Harness h;
        REQUIRE(h.service.restore(saved()));
        REQUIRE_FALSE(h.service.restore(saved()));
        REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"new-1"});
        h.service.tick(0);
        REQUIRE(h.execution.starts == 1);
        REQUIRE(h.execution.update.job_id == "new-1");
    }
}

TEST_CASE("Identity failures and reentrancy cannot reserve download admission", "[download][checkpoint]") {
    Harness h;
    h.identity.throws = true;
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.snapshot().empty());
    h.identity.throws = false;
    h.identity.result = "not-a-digest";
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.snapshot().empty());
    h.identity.result = std::string(64, 'a');
    h.identity.callback = [&] {
        REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::Busy);
        REQUIRE_FALSE(h.service.restore(saved()));
        REQUIRE_FALSE(h.service.checkpoint().has_value());
        h.service.tick(0);
        h.service.shutdown();
    };
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::None);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.service.snapshot().size() == 1);
    h.identity.callback = {};
    REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::None);
}

TEST_CASE("Download checkpoint rejects malformed identities references states and counters", "[download][checkpoint]") {
    auto checkpoint = saved();
    REQUIRE(validDownloadCheckpoint(checkpoint));
    SECTION("UUID") { checkpoint.receipts[0].request_id = "bad"; }
    SECTION("noncanonical UUID") { checkpoint.receipts[0].request_id[0] = 'A'; }
    SECTION("fingerprint") { checkpoint.receipts[0].fingerprint = "bad"; }
    SECTION("fingerprint case") { checkpoint.receipts[0].fingerprint[0] = 'A'; }
    SECTION("empty receipt") { checkpoint.receipts[0].job_ids.clear(); }
    SECTION("orphan job") { checkpoint.receipts.clear(); }
    SECTION("wrong reference") { checkpoint.receipts[0].job_ids[0] = "unknown"; }
    SECTION("duplicate reference") { checkpoint.receipts[0].job_ids.push_back("old-1"); }
    SECTION("duplicate job") { checkpoint.jobs.push_back(checkpoint.jobs[0]); }
    SECTION("unknown state") { checkpoint.jobs[0].state = static_cast<DownloadJobState>(100); }
    SECTION("unknown error") { checkpoint.jobs[0].error = static_cast<DownloadFailure>(100); }
    SECTION("contradictory counters") {
        checkpoint.jobs[0].completed_units = 2;
        checkpoint.jobs[0].total_units = 1;
    }
    SECTION("queued error") { checkpoint.jobs[0].error = DownloadFailure::Network; }
    SECTION("failed no error") { checkpoint.jobs[0].state = DownloadJobState::Failed; }
    SECTION("completed no path") { checkpoint.jobs[0].state = DownloadJobState::Completed; }
    SECTION("partial path") {
        checkpoint.jobs[0].state = DownloadJobState::ImportFailed;
        checkpoint.jobs[0].error = DownloadFailure::Import;
        checkpoint.jobs[0].completed_mp3 = "partial.mp3";
    }
    SECTION("path on queued") { checkpoint.jobs[0].completed_mp3 = candidate(); }
    SECTION("NUL") { checkpoint.jobs[0].title.push_back('\0'); }
    SECTION("invalid ID") { checkpoint.jobs[0].job_id = "bad/path"; }
    SECTION("accounting undercount") { checkpoint.receipts[0].retained_request_bytes = 1; }
    SECTION("accounting overflow") {
        checkpoint.receipts[0].retained_request_bytes = std::numeric_limits<std::size_t>::max();
    }
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
    Harness h;
    REQUIRE_FALSE(h.service.restore(checkpoint));
    REQUIRE(h.service.snapshot().empty());
}

TEST_CASE("Download checkpoint enforces representation caps including paths without eviction",
          "[download][checkpoint]") {
    REQUIRE(validDownloadCheckpoint({}));
    auto checkpoint = saved();
    SECTION("too many jobs") { checkpoint.jobs.resize(download_checkpoint_max_jobs + 1, checkpoint.jobs[0]); }
    SECTION("too many receipts") {
        checkpoint.receipts.resize(download_checkpoint_max_receipts + 1, checkpoint.receipts[0]);
    }
    SECTION("too much text") { checkpoint.jobs[0].title.assign(download_checkpoint_max_bytes + 1, 'a'); }
    SECTION("too long path") {
        checkpoint.jobs[0].state = DownloadJobState::Interrupted;
        checkpoint.jobs[0].error = DownloadFailure::Unavailable;
        checkpoint.jobs[0].completed_mp3 = candidate().parent_path() / (std::string(30'001, 'x') + ".mp3");
    }
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
}

TEST_CASE("Download checkpoint retains FIFO associations for multiple receipts", "[download][checkpoint]") {
    Harness h;
    auto first = input();
    first.items.push_back(first.items[0]);
    REQUIRE(h.service.submit(first).job_ids == std::vector<std::string>{"new-1", "new-2"});
    REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"new-3"});
    const auto checkpoint = h.service.checkpoint();
    REQUIRE(checkpoint.has_value());
    REQUIRE(validDownloadCheckpoint(*checkpoint));
    Harness restored({}, "next");
    REQUIRE(restored.service.restore(*checkpoint));
    REQUIRE(restored.service.submit(first).job_ids == std::vector<std::string>{"new-1", "new-2"});
    REQUIRE(restored.service.submit(input(2)).job_ids == std::vector<std::string>{"new-3"});
}

TEST_CASE("Download recovery is one shot even for empty checkpoints and exports after shutdown",
          "[download][checkpoint]") {
    Harness h;
    REQUIRE(h.service.restore({}));
    REQUIRE_FALSE(h.service.restore(saved()));
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::None);
    const auto calls = h.identity.calls;
    h.service.shutdown();
    const auto checkpoint = h.service.checkpoint();
    REQUIRE(checkpoint.has_value());
    REQUIRE(checkpoint->jobs[0].state == DownloadJobState::Cancelled);
    REQUIRE(h.identity.calls == calls);
}

TEST_CASE("Download checkpoint validates an inclusive full schema without hidden eviction", "[download][checkpoint]") {
    DownloadCheckpoint checkpoint;
    for (std::size_t i = 0; i < download_checkpoint_max_receipts; ++i) {
        const auto request = input(static_cast<unsigned int>(i + 1));
        const auto first_id = "old-" + std::to_string(2 * i + 1);
        const auto second_id = "old-" + std::to_string(2 * i + 2);
        checkpoint.receipts.push_back({.request_id = request.request_id,
                                       .fingerprint = std::string(64, 'a'),
                                       .job_ids = {first_id, second_id},
                                       .retained_request_bytes = 40});
        checkpoint.jobs.push_back({.job_id = first_id, .title = "a"});
        checkpoint.jobs.push_back({.job_id = second_id, .title = "b"});
    }
    REQUIRE(validDownloadCheckpoint(checkpoint));
    Harness h;
    REQUIRE(h.service.restore(checkpoint));
    REQUIRE(h.service.snapshot().size() == download_checkpoint_max_jobs);
    auto replay = input();
    replay.items = {{.url = "a", .title = "a"}, {.url = "b", .title = "b"}};
    REQUIRE(h.service.submit(replay).job_ids == checkpoint.receipts[0].job_ids);
    REQUIRE(h.service.submit(input(100)).error == DownloadSubmissionError::LimitExceeded);
    std::swap(checkpoint.jobs[0], checkpoint.jobs[1]);
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
}

TEST_CASE("Download checkpoint representation byte quota is inclusive for native candidates",
          "[download][checkpoint]") {
    DownloadCheckpoint checkpoint;
    const auto unit = sizeof(std::filesystem::path::value_type);
    const unsigned int job_count = unit == 1 ? 36 : 18;
    for (unsigned int i = 1; i <= job_count; ++i) {
        if ((i - 1) % 18 == 0)
            checkpoint.receipts.push_back({.request_id = input((i - 1) / 18 + 1).request_id,
                                           .fingerprint = std::string(64, 'a'),
                                           .retained_request_bytes = 36});
        auto id = "old-" + std::to_string(i);
        checkpoint.receipts.back().job_ids.push_back(id);
        checkpoint.receipts.back().retained_request_bytes += 2;
        checkpoint.jobs.push_back({.job_id = std::move(id), .title = "a", .state = DownloadJobState::Completed});
    }
    std::size_t text_bytes = (36 + 64) * checkpoint.receipts.size();
    for (const auto& job : checkpoint.jobs)
        text_bytes += job.job_id.size() * 2 + job.title.size();
    // Align the non-path strings so the whole native budget can be represented.
    while ((download_checkpoint_max_bytes - text_bytes) % unit != 0) {
        checkpoint.jobs[0].title += 'a';
        ++checkpoint.receipts[0].retained_request_bytes;
        ++text_bytes;
    }
    auto remaining_units = (download_checkpoint_max_bytes - text_bytes) / unit;
    const auto parent = candidate().parent_path();
    for (std::size_t i = 0; i < checkpoint.jobs.size(); ++i) {
        const auto count = remaining_units / (checkpoint.jobs.size() - i);
        const auto overhead = (parent / "x.mp3").native().size() - 1;
        REQUIRE(count > overhead);
        REQUIRE(count <= 30'000);
        checkpoint.jobs[i].completed_mp3 = parent / (std::string(count - overhead, 'x') + ".mp3");
        REQUIRE(checkpoint.jobs[i].completed_mp3.native().size() == count);
        remaining_units -= count;
    }
    REQUIRE(remaining_units == 0);
    REQUIRE(validDownloadCheckpoint(checkpoint));
    checkpoint.jobs[0].completed_mp3 = parent / (checkpoint.jobs[0].completed_mp3.stem().string() + "x.mp3");
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
}

TEST_CASE("Recovered replay identity failures and callbacks preserve committed receipts", "[download][checkpoint]") {
    Harness h({.max_jobs = 1, .max_requests = 1});
    REQUIRE(h.service.restore(saved()));
    const auto before = h.service.checkpoint();
    h.identity.throws = true;
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::NotAvailable);
    h.identity.throws = false;
    h.identity.result = "bad";
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.checkpoint() == before);
    h.identity.result = std::string(64, 'a');
    h.identity.callback = [&] {
        REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::Busy);
        REQUIRE_FALSE(h.service.checkpoint());
        REQUIRE_FALSE(h.service.retryImport("old-1"));
        h.service.shutdown();
    };
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-1"});
    h.identity.callback = {};
    REQUIRE(h.service.checkpoint() == before);
    REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::LimitExceeded);
}

TEST_CASE("Two download restarts retain private candidate Unicode and confirmed progress", "[download][checkpoint]") {
    auto checkpoint = saved(DownloadJobState::ImportFailed);
    checkpoint.jobs[0].error = DownloadFailure::Import;
    checkpoint.jobs[0].completed_mp3 = candidate().parent_path() / std::filesystem::path{u8"Música 日本 ☕.mp3"};
    checkpoint.jobs[0].title = "Música 日本 ☕";
    checkpoint.jobs[0].completed_units = 123;
    checkpoint.jobs[0].total_units = 123;
    Harness first;
    REQUIRE(first.service.restore(checkpoint));
    const auto recovered = first.service.checkpoint();
    REQUIRE(recovered);
    REQUIRE(recovered->jobs[0].state == DownloadJobState::Interrupted);
    REQUIRE(recovered->jobs[0].completed_mp3 == checkpoint.jobs[0].completed_mp3);
    Harness second({}, "next");
    REQUIRE(second.service.restore(*recovered));
    REQUIRE(second.service.checkpoint() == recovered);
    REQUIRE(second.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(second.service.snapshot()[0].title == checkpoint.jobs[0].title);
    REQUIRE(second.service.snapshot()[0].completed_units == 123);
    REQUIRE(second.service.snapshot()[0].total_units == 123);
    second.service.tick(0);
    REQUIRE(second.execution.starts == 0);
    REQUIRE(second.importer.calls == 0);
}

TEST_CASE("Live download checkpoints preserve phases without consulting execution or identity",
          "[download][checkpoint]") {
    const auto phase = GENERATE(DownloadPhase::Downloading, DownloadPhase::Converting, DownloadPhase::Publishing);
    Harness h;
    const auto id = h.service.submit(input()).job_ids[0];
    h.service.tick(0);
    h.execution.update = {.job_id = id, .phase = phase, .completed_units = 3, .total_units = 8};
    h.service.tick(1);
    const auto calls = h.identity.calls;
    const auto polls = h.execution.polls;
    const auto checkpoint = h.service.checkpoint();
    REQUIRE(checkpoint);
    REQUIRE(checkpoint->jobs == h.service.snapshot());
    REQUIRE(checkpoint->jobs[0].completed_units == 3);
    REQUIRE(h.identity.calls == calls);
    REQUIRE(h.execution.polls == polls);
    Harness restored({}, "next");
    REQUIRE(restored.service.restore(*checkpoint));
    REQUIRE(restored.service.snapshot()[0].state == DownloadJobState::Interrupted);
    restored.service.tick(2);
    REQUIRE(restored.execution.starts == 0);
}

} // namespace music_player::m0::test
