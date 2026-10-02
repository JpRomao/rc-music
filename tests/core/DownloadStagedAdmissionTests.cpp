#include "m0/application/DownloadService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0::test {
namespace {

struct StagedExecution final : DownloadExecution {
    std::vector<std::string> starts{};
    std::vector<std::string> polls{};
    std::vector<std::string> cancellations{};
    DownloadExecutionUpdate update{};
    bool start(const std::string& id, const DownloadItem&) override {
        starts.push_back(id);
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view id) override { cancellations.emplace_back(id); }
    DownloadExecutionUpdate poll(std::string_view id) override {
        polls.emplace_back(id);
        return update;
    }
};

struct StagedImport final : DownloadImport {
    unsigned int calls{0};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};

struct StagedIdentity final : DownloadRequestIdentity {
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};

DownloadRequest stagedInput(const unsigned int number = 1, const std::size_t count = 1) {
    auto tail = std::to_string(number);
    tail.insert(0, 12 - tail.size(), '0');
    DownloadRequest request{.request_id = "abcdefab-cdef-4abc-8def-" + tail};
    for (std::size_t i = 0; i < count; ++i)
        request.items.push_back({.url = "https://synthetic.example/audio.mp3?token=private",
                                 .title = "Synthetic " + std::to_string(i),
                                 .kind = DownloadMediaKind::Audio});
    return request;
}

std::filesystem::path stagedCandidate() {
#ifdef _WIN32
    return std::filesystem::path{L"C:/synthetic checkpoint/audio.mp3"};
#else
    return std::filesystem::path{"/synthetic checkpoint/audio.mp3"};
#endif
}

struct StagedHarness {
    StagedExecution execution{};
    StagedImport importer{};
    StagedIdentity identity{};
    DownloadService service;
    explicit StagedHarness(DownloadServiceLimits limits = {})
        : service(execution, importer, "new", limits, &identity) {}
};

DownloadCheckpoint stagedHistory(const std::size_t job_count, const std::size_t batch_count) {
    DownloadCheckpoint checkpoint;
    for (std::size_t i = 0; i < job_count; ++i) {
        if (i % batch_count == 0)
            checkpoint.receipts.push_back(
                {.request_id = stagedInput(static_cast<unsigned int>(i / batch_count + 1)).request_id,
                 .fingerprint = std::string(64, 'a'),
                 .retained_request_bytes = 36});
        auto id = "old-" + std::to_string(i + 1);
        checkpoint.receipts.back().job_ids.push_back(id);
        checkpoint.receipts.back().retained_request_bytes += 2;
        checkpoint.jobs.push_back({.job_id = std::move(id), .title = "a"});
    }
    return checkpoint;
}

DownloadCheckpoint stagedRepresentation(const std::size_t target_bytes) {
    const auto unit = sizeof(std::filesystem::path::value_type);
    auto checkpoint = stagedHistory(unit == 1 ? 36 : 18, 18);
    std::size_t strings = (36 + 64) * checkpoint.receipts.size();
    for (const auto& job : checkpoint.jobs)
        strings += job.job_id.size() * 2 + job.title.size();
    while ((target_bytes - strings) % unit != 0) {
        checkpoint.jobs[0].title += 'a';
        ++checkpoint.receipts[0].retained_request_bytes;
        ++strings;
    }
    auto remaining = (target_bytes - strings) / unit;
    const auto parent = stagedCandidate().parent_path();
    for (std::size_t i = 0; i < checkpoint.jobs.size(); ++i) {
        const auto count = remaining / (checkpoint.jobs.size() - i);
        const auto overhead = (parent / "x.mp3").native().size() - 1;
        REQUIRE(count > overhead);
        REQUIRE(count <= 30'000);
        auto& job = checkpoint.jobs[i];
        job.state = DownloadJobState::Completed;
        job.completed_mp3 = parent / (std::string(count - overhead, 'x') + ".mp3");
        REQUIRE(job.completed_mp3.native().size() == count);
        remaining -= count;
    }
    REQUIRE(remaining == 0);
    REQUIRE(validDownloadCheckpoint(checkpoint));
    return checkpoint;
}

} // namespace

TEST_CASE("Staged download admission has a receipt but cannot execute before confirmation",
          "[download][staged][FR-051]") {
    StagedHarness h;
    const auto request = stagedInput(1, 2);
    const auto admitted = h.service.submitStaged(request);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    REQUIRE(admitted.job_ids == std::vector<std::string>{"new-1", "new-2"});
    REQUIRE(h.service.checkpoint().has_value());
    REQUIRE(h.service.submitStaged(request).job_ids == admitted.job_ids);
    REQUIRE(h.service.submit(request).error == DownloadSubmissionError::Busy);
    h.service.tick(0);
    h.service.tick(std::numeric_limits<std::int64_t>::max());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.importer.calls == 0);
    REQUIRE_FALSE(h.service.confirmStaged(stagedInput(2).request_id));
    auto uppercase = request.request_id;
    std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(),
                   [](const char c) { return c >= 'a' && c <= 'f' ? static_cast<char>(c - 'a' + 'A') : c; });
    REQUIRE(h.service.confirmStaged(uppercase));
    REQUIRE(h.service.submit(request).job_ids == admitted.job_ids);
    h.service.tick(0);
    REQUIRE(h.execution.starts == std::vector<std::string>{"new-1"});
}

TEST_CASE("Staged download blocks FIFO successors while an existing active job still drains",
          "[download][staged][FR-053]") {
    StagedHarness h;
    const auto first = h.service.submit(stagedInput(1));
    REQUIRE(first.error == DownloadSubmissionError::None);
    h.service.tick(0);
    REQUIRE(h.execution.starts == first.job_ids);
    const auto staged = h.service.submitStaged(stagedInput(2));
    REQUIRE(staged.error == DownloadSubmissionError::None);
    const auto following = h.service.submit(stagedInput(3));
    REQUIRE(following.error == DownloadSubmissionError::None);
    h.execution.update = {.job_id = first.job_ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(1);
    REQUIRE(h.execution.polls == first.job_ids);
    h.service.tick(2);
    REQUIRE(h.execution.starts == first.job_ids);
    REQUIRE(h.service.confirmStaged(stagedInput(2).request_id));
    h.service.tick(3);
    REQUIRE(h.execution.starts == std::vector<std::string>{"new-1", "new-2"});
    h.execution.update = {.job_id = staged.job_ids[0], .state = DownloadExecutionState::Cancelled};
    h.service.tick(4);
    h.service.tick(5);
    REQUIRE(h.execution.starts == std::vector<std::string>{"new-1", "new-2", "new-3"});
}

TEST_CASE("Cancellation and confirmation of a staged receipt never resurrect its jobs", "[download][staged]") {
    StagedHarness h;
    const auto request = stagedInput();
    const auto admitted = h.service.submitStaged(request);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    REQUIRE(h.service.cancel(admitted.job_ids[0]));
    REQUIRE(h.service.confirmStaged(request.request_id));
    h.service.tick(0);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelled);
    REQUIRE(h.service.submit(request).job_ids == admitted.job_ids);
}

TEST_CASE("Staged admission requires identity and owns no quota when it refuses", "[download][staged]") {
    StagedExecution execution;
    StagedImport importer;
    DownloadService service(execution, importer, "volatile", {.max_jobs = 1, .max_requests = 1});
    REQUIRE(service.submitStaged(stagedInput()).error == DownloadSubmissionError::NotAvailable);
    REQUIRE(service.snapshot().empty());
    REQUIRE(service.submit(stagedInput()).job_ids == std::vector<std::string>{"volatile-1"});
}

TEST_CASE("Restored staged history never becomes executable through confirmation", "[download][staged]") {
    StagedHarness first;
    const auto request = stagedInput();
    const auto admitted = first.service.submitStaged(request);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    const auto checkpoint = first.service.checkpoint();
    REQUIRE(checkpoint);
    StagedExecution execution;
    StagedImport importer;
    StagedIdentity identity;
    DownloadService restored(execution, importer, "fresh", {}, &identity);
    REQUIRE(restored.restore(*checkpoint));
    static_cast<void>(restored.confirmStaged(request.request_id));
    restored.tick(0);
    REQUIRE(execution.starts.empty());
    REQUIRE(restored.snapshot()[0].state == DownloadJobState::Interrupted);
    REQUIRE(restored.submitStaged(request).job_ids == admitted.job_ids);
}

TEST_CASE("Staged admission enforces fixed checkpoint job and receipt limits before commit", "[download][staged]") {
    const bool receipts_full = GENERATE(false, true);
    const auto checkpoint = stagedHistory(receipts_full ? 64 : 128, receipts_full ? 1 : 20);
    REQUIRE(validDownloadCheckpoint(checkpoint));
    StagedHarness h({.max_jobs = 129, .max_requests = 65});
    REQUIRE(h.service.restore(checkpoint));
    const auto before = h.service.checkpoint();
    REQUIRE(h.service.submitStaged(stagedInput(100)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.service.checkpoint() == before);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Staged admission counts private restored candidates in its prospective checkpoint", "[download][staged]") {
    StagedHarness h;
    REQUIRE(h.service.restore(stagedRepresentation(download_checkpoint_max_bytes - 160)));
    const auto before = h.service.checkpoint();
    auto large = stagedInput(100);
    large.items[0].title.assign(800, 'a');
    REQUIRE(h.service.submitStaged(large).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.service.checkpoint() == before);
    auto small = stagedInput(101);
    small.items[0].url = "a";
    small.items[0].title = "a";
    const auto accepted = h.service.submitStaged(small);
    REQUIRE(accepted.error == DownloadSubmissionError::None);
    REQUIRE(accepted.job_ids == std::vector<std::string>{"new-1"});
    REQUIRE(h.service.checkpoint());
    h.service.tick(0);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Staged prospective checkpoint representation limit is inclusive", "[download][staged]") {
    // One request ID, fingerprint, two copies of new-1 and the one-byte title.
    constexpr std::size_t new_strings = 36 + 64 + 2 * 5 + 1;
    StagedHarness h;
    REQUIRE(h.service.restore(stagedRepresentation(download_checkpoint_max_bytes - new_strings)));
    auto request = stagedInput(100);
    request.items[0].url = "a";
    request.items[0].title = "a";
    REQUIRE(h.service.submitStaged(request).error == DownloadSubmissionError::None);
    REQUIRE(h.service.checkpoint());
    const auto before = h.service.checkpoint();
    REQUIRE(h.service.submitStaged(stagedInput(101)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.service.checkpoint() == before);
}

} // namespace music_player::m0::test
