#include "m0/application/DownloadService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace music_player::m0::test {
namespace {

struct FakeExecution final : DownloadExecution {
    struct ArchivedStart final {
        std::vector<std::string> archived_job_ids;
        std::vector<std::string> retired_import_job_ids;
    };
    std::vector<std::pair<std::string, DownloadItem>> starts;
    std::vector<ArchivedStart> archived_starts;
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
    bool startWithArchivedJobs(const std::string& job_id, const DownloadItem& item,
                               const ArchivedDownloadJobEvidence& evidence) override {
        archived_starts.push_back(
            {.archived_job_ids = {evidence.archived_job_ids.begin(), evidence.archived_job_ids.end()},
             .retired_import_job_ids = {evidence.retired_import_job_ids.begin(),
                                        evidence.retired_import_job_ids.end()}});
        return start(job_id, item);
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
    std::optional<DownloadImportState> begin_state{};
    DownloadImportState poll_state{DownloadImportState::Pending};
    std::vector<std::string> import_polls{};
    bool throw_poll{false};
    std::function<void()> on_import_poll{};
    bool durable_recovery{false};
    bool throw_capability{false};
    mutable int capability_queries{0};
    mutable std::function<void()> on_capability{};
    std::vector<std::string> retired_import_job_ids{};
    std::function<void()> on_retirement_evidence{};

    bool supportsDurableRecovery() const override {
        ++capability_queries;
        if (on_capability)
            on_capability();
        if (throw_capability)
            throw std::runtime_error{"private capability detail"};
        return durable_recovery;
    }

    bool accept(const std::string& id, const std::filesystem::path& path) override {
        imports.emplace_back(id, path);
        if (on_accept)
            on_accept();
        if (throws)
            throw std::runtime_error("private import detail");
        return accepted;
    }
    DownloadImportState begin(const std::string& id, const std::filesystem::path& path) override {
        if (!begin_state)
            return DownloadImport::begin(id, path);
        static_cast<void>(accept(id, path));
        return *begin_state;
    }
    DownloadImportState pollImport(const std::string_view id) override {
        import_polls.emplace_back(id);
        if (on_import_poll)
            on_import_poll();
        if (throw_poll)
            throw std::runtime_error("private persistence detail");
        return poll_state;
    }
    std::span<const std::string> committedRetiredImportJobIds() const override {
        if (on_retirement_evidence)
            on_retirement_evidence();
        return retired_import_job_ids;
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

DownloadRequest youtubeRequest(const unsigned int id = 1) {
    auto result = request(id);
    result.items = {{.url = "https://www.youtube.com/watch?v=AbCdEf_12-3",
                     .title = "Synthetic opening",
                     .kind = DownloadMediaKind::Audio,
                     .youtube = YoutubeAudioItem{.video_id = "AbCdEf_12-3",
                                                 .title = "Synthetic opening",
                                                 .artist = "Synthetic artist",
                                                 .lyrics_page = "https://www.letras.mus.br/synthetic/opening/",
                                                 .fetch_lyrics = true}}};
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

struct RecoveryIdentity final : DownloadRequestIdentity {
    std::size_t calls{0};
    std::string fingerprint(const DownloadRequest& value) override {
        ++calls;
        return std::string(64,
                           !value.items.empty() && value.items.front().title == "Changed archived request" ? 'b' : 'a');
    }
};

void requireCapacity(const DownloadService& service, const DownloadServiceCapacity expected) {
    STATIC_REQUIRE(noexcept(service.capacity()));
    const auto actual = service.capacity();
    REQUIRE(actual.jobs == expected.jobs);
    REQUIRE(actual.max_jobs == expected.max_jobs);
    REQUIRE(actual.requests == expected.requests);
    REQUIRE(actual.max_requests == expected.max_requests);
    REQUIRE(actual.retained_request_bytes == expected.retained_request_bytes);
    REQUIRE(actual.max_retained_request_bytes == expected.max_retained_request_bytes);
}

DownloadCheckpoint interruptedCheckpoint(bool with_candidate = false) {
    return {.receipts = {{.request_id = request().request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"old-1"},
                          .retained_request_bytes = 100}},
            .jobs = {{.job_id = "old-1",
                      .title = "Recovered audio",
                      .state = DownloadJobState::Interrupted,
                      .completed_units = 2,
                      .total_units = 9,
                      .completed_mp3 = with_candidate ? finalPath() : std::filesystem::path{},
                      .error = DownloadFailure::Unavailable}}};
}

struct RecoveryHarness final {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service;
    explicit RecoveryHarness(DownloadServiceLimits limits = {}, DownloadCheckpoint checkpoint = interruptedCheckpoint())
        : service(execution, importer, "fresh", limits, &identity) {
        importer.durable_recovery = true;
        REQUIRE(service.restore(checkpoint, {}));
    }
};

} // namespace

TEST_CASE("Legacy download import begin preserves its synchronous acceptance contract", "[download][import-pending]") {
    FakeImport importer;
    const bool accepted = GENERATE(false, true);
    importer.accepted = accepted;
    REQUIRE(importer.begin("job-1", finalPath()) ==
            (accepted ? DownloadImportState::Committed : DownloadImportState::Failed));
    REQUIRE(importer.imports.size() == 1);
    REQUIRE(importer.DownloadImport::pollImport("job-1") == DownloadImportState::Failed);
}

TEST_CASE("Settling a confirmed recovered discard preserves replay and archives only the complete batch",
          "[download][recovered-discard]") {
    auto original = interruptedCheckpoint(true);
    auto sibling = original.jobs.front();
    sibling.job_id = "old-2";
    original.receipts.front().job_ids.push_back(sibling.job_id);
    original.jobs.push_back(sibling);
    RecoveryHarness h({}, original);
    STATIC_REQUIRE(noexcept(h.service.canSettleRecoveredDiscard("old-1")));
    REQUIRE(h.service.recoveryReport().items.size() == 2);
    REQUIRE_FALSE(h.service.canSettleRecoveredDiscard("unknown"));
    REQUIRE_FALSE(h.service.settleRecoveredDiscard("unknown"));
    REQUIRE(h.service.checkpoint() == original);

    REQUIRE(h.service.canSettleRecoveredDiscard("old-1"));
    REQUIRE(h.service.settleRecoveredDiscard("old-1"));
    REQUIRE_FALSE(h.service.canSettleRecoveredDiscard("old-1"));
    REQUIRE_FALSE(h.service.settleRecoveredDiscard("old-1"));
    auto settled = original;
    auto& resolved = settled.jobs.front();
    resolved.state = DownloadJobState::Cancelled;
    resolved.error = DownloadFailure::None;
    resolved.completed_mp3.clear();
    REQUIRE(h.service.checkpoint() == settled);
    REQUIRE(h.service.snapshot().front() == resolved);
    REQUIRE(validDownloadCheckpoint(settled));
    REQUIRE(h.service.recoveryReport().items ==
            std::vector<DownloadRecoveryItem>{{"old-2", DownloadRecoveryDisposition::Unchanged}});
    REQUIRE_FALSE(h.service.canArchiveSettled());
    REQUIRE_FALSE(h.service.stageSettledArchive());
    REQUIRE(h.service.submit(request(1, 2)).job_ids == original.receipts.front().job_ids);

    RecoveryHarness restarted({}, settled);
    REQUIRE(restarted.service.checkpoint() == settled);
    REQUIRE_FALSE(restarted.service.canSettleRecoveredDiscard("old-1"));
    REQUIRE(restarted.service.settleRecoveredDiscard("old-2"));
    const auto archive = restarted.service.stageSettledArchive();
    REQUIRE(archive);
    REQUIRE(archive->jobs.empty());
    REQUIRE(archive->receipts.empty());
    REQUIRE(archive->archived_receipts.size() == 1);
    REQUIRE(archive->archived_receipts.front().job_ids == original.receipts.front().job_ids);
    REQUIRE(restarted.service.confirmSettledArchive());
    REQUIRE(restarted.service.submit(request(1, 2)).job_ids == original.receipts.front().job_ids);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.importer.capability_queries == 0);
    REQUIRE(restarted.execution.starts.empty());
    REQUIRE(restarted.importer.imports.empty());
}

TEST_CASE("Recovered discard cannot settle an active, guarded or previously imported job",
          "[download][recovered-discard]") {
    RecoveryHarness h({}, interruptedCheckpoint(true));
    std::string id{"old-1"};
    SECTION("live queued job") {
        const auto admitted = h.service.submit(request(2));
        REQUIRE(admitted.error == DownloadSubmissionError::None);
        id = admitted.job_ids.front();
    }
    SECTION("closed owner") { h.service.shutdown(); }
    SECTION("archive staged") {
        const auto admitted = h.service.submit(request(2));
        REQUIRE(admitted.error == DownloadSubmissionError::None);
        REQUIRE(h.service.cancel(admitted.job_ids.front()));
        REQUIRE(h.service.stageSettledArchive());
    }
    SECTION("prior recovery import") {
        const auto result =
            GENERATE(DownloadImportState::Pending, DownloadImportState::Failed, DownloadImportState::Committed);
        h.importer.begin_state = result;
        REQUIRE(h.service.beginRecoveredImport(id, finalPath()) == (result != DownloadImportState::Failed));
        REQUIRE(h.service.recoveredImportState(id) == result);
    }
    const auto before = h.service.checkpoint();
    const auto report = h.service.recoveryReport();
    REQUIRE_FALSE(h.service.canSettleRecoveredDiscard(id));
    REQUIRE_FALSE(h.service.settleRecoveredDiscard(id));
    REQUIRE(h.service.checkpoint() == before);
    REQUIRE(h.service.recoveryReport().items == report.items);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Recovered import preserves the original job and partial telemetry until durable commitment",
          "[download][recovered-import][FR-051]") {
    RecoveryHarness h;
    const auto before = h.service.checkpoint();
    REQUIRE(before);
    REQUIRE(h.service.canImportRecovered("old-1"));
    REQUIRE_FALSE(h.service.recoveredImportState("old-1"));
    h.importer.begin_state = DownloadImportState::Pending;
    REQUIRE(h.service.beginRecoveredImport("old-1", finalPath()));
    REQUIRE(h.service.recoveredImportState("old-1") == DownloadImportState::Pending);
    REQUIRE(h.service.snapshot()[0] == before->jobs[0]);
    REQUIRE_FALSE(h.service.canImportRecovered("old-1"));
    REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
    REQUIRE_FALSE(h.service.retryImport("old-1"));
    REQUIRE_FALSE(h.service.cancel("old-1"));
    const auto pending = h.service.checkpoint();
    REQUIRE(pending);
    REQUIRE(validDownloadCheckpoint(*pending));
    REQUIRE(pending->receipts == before->receipts);
    REQUIRE(pending->jobs[0].completed_mp3 == finalPath());
    REQUIRE(pending->jobs[0].completed_units == 2);
    REQUIRE(pending->jobs[0].total_units == 9);
    h.service.tick(-1);
    REQUIRE(h.service.recoveredImportState("old-1") == DownloadImportState::Pending);
    h.importer.poll_state = DownloadImportState::Committed;
    h.service.tick(1);
    const auto imported = h.service.snapshot()[0];
    REQUIRE(imported.state == DownloadJobState::Imported);
    REQUIRE(imported.error == DownloadFailure::None);
    REQUIRE(imported.completed_mp3.empty());
    REQUIRE(imported.completed_units == 2);
    REQUIRE(imported.total_units == 9);
    REQUIRE(h.service.recoveredImportState("old-1") == DownloadImportState::Committed);
    REQUIRE(h.service.recoveryReport().items ==
            std::vector<DownloadRecoveryItem>{{"old-1", DownloadRecoveryDisposition::Imported}});
    REQUIRE(h.service.checkpoint()->receipts == before->receipts);
    REQUIRE(validDownloadCheckpoint(*h.service.checkpoint()));
    REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
    REQUIRE(h.importer.imports == std::vector<std::pair<std::string, std::filesystem::path>>{{"old-1", finalPath()}});
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.execution.cancellations.empty());
}

TEST_CASE("Recovered import failures need a fresh explicit admission without reexecuting download",
          "[download][recovered-import][FR-053]") {
    RecoveryHarness h({}, interruptedCheckpoint(true));
    h.importer.begin_state = DownloadImportState::Failed;
    SECTION("failed begin") {}
    SECTION("throwing begin") { h.importer.throws = true; }
    SECTION("unknown begin") { h.importer.begin_state = static_cast<DownloadImportState>(99); }
    REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
    REQUIRE(h.service.recoveredImportState("old-1") == DownloadImportState::Failed);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Import);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.service.checkpoint()->jobs[0].completed_mp3 == finalPath());
    REQUIRE_FALSE(h.service.retryImport("old-1"));
    REQUIRE(h.service.canImportRecovered("old-1"));
    h.service.tick(0);
    REQUIRE(h.importer.imports.size() == 1);
    h.importer.throws = false;
    h.importer.begin_state = DownloadImportState::Committed;
    REQUIRE(h.service.beginRecoveredImport("old-1", finalPath()));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Imported);
    REQUIRE(h.service.recoveredImportState("old-1") == DownloadImportState::Committed);
    REQUIRE(h.importer.imports.size() == 2);
    REQUIRE(h.importer.imports[0] == h.importer.imports[1]);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Recovered import admission refuses wrong identity path capability and closed owners atomically",
          "[download][recovered-import][FR-053]") {
    RecoveryHarness h({}, interruptedCheckpoint(true));
    auto id = std::string{"old-1"};
    auto path = finalPath();
    SECTION("unknown job") { id = "unknown"; }
    SECTION("private candidate conflict") { path = path.parent_path() / "other.mp3"; }
    SECTION("relative path") { path = "relative.mp3"; }
    SECTION("unnormalized path") { path = path.parent_path() / "x/../final.mp3"; }
    SECTION("wrong suffix") { path.replace_extension(".mp4"); }
    SECTION("embedded nul") {
        auto native = path.native();
        native.insert(native.begin() + 3, 0);
        path = std::filesystem::path{native};
    }
    SECTION("volatile importer") { h.importer.durable_recovery = false; }
    SECTION("throwing capability") { h.importer.throw_capability = true; }
    SECTION("closed owner") { h.service.shutdown(); }
    const auto before = h.service.checkpoint();
    REQUIRE_FALSE(h.service.beginRecoveredImport(id, path));
    REQUIRE(h.service.checkpoint() == before);
    REQUIRE_FALSE(h.service.recoveredImportState("old-1"));
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Recovered import capability is prompt guarded and never authorizes a live or confirmed job",
          "[download][recovered-import]") {
    RecoveryHarness h;
    h.importer.on_capability = [&] {
        REQUIRE_FALSE(h.service.canImportRecovered("old-1"));
        REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
        REQUIRE_FALSE(h.service.canSettleRecoveredDiscard("old-1"));
        REQUIRE_FALSE(h.service.settleRecoveredDiscard("old-1"));
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
        REQUIRE(h.service.snapshot().size() == 1);
    };
    REQUIRE(h.service.canImportRecovered("old-1"));
    REQUIRE(h.importer.capability_queries == 1);
    REQUIRE(h.importer.imports.empty());
    REQUIRE_FALSE(h.service.canImportRecovered("unknown"));
    Harness live;
    live.importer.durable_recovery = true;
    const auto id = live.admit()[0];
    REQUIRE_FALSE(live.service.canImportRecovered(id));
    REQUIRE_FALSE(live.service.beginRecoveredImport(id, finalPath()));
    FakeImport legacy;
    REQUIRE_FALSE(legacy.DownloadImport::supportsDurableRecovery());
    // A different pristine owner recognizes a healthy receipt without invoking
    // the import port again, including when the old queue was later cleared.
    FakeExecution execution;
    FakeImport importer;
    importer.durable_recovery = true;
    RecoveryIdentity identity;
    DownloadService restored{execution, importer, "later", {}, &identity};
    REQUIRE(restored.restore(interruptedCheckpoint(), std::vector<DownloadImportReceipt>{{"old-1", finalPath()}}));
    REQUIRE_FALSE(restored.canImportRecovered("old-1"));
    REQUIRE_FALSE(restored.beginRecoveredImport("old-1", finalPath()));
    REQUIRE(importer.imports.empty());
}

TEST_CASE("Recovered import keeps quota receipts and fresh namespace while guarding port reentry",
          "[download][recovered-import]") {
    RecoveryHarness h({.max_jobs = 1, .max_requests = 1, .max_retained_request_bytes = 100});
    const auto receipts = h.service.checkpoint()->receipts;
    h.importer.on_accept = [&] {
        REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
        REQUIRE_FALSE(h.service.canImportRecovered("old-1"));
        REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
        REQUIRE_FALSE(h.service.retryImport("old-1"));
        REQUIRE_FALSE(h.service.checkpoint());
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
        h.service.tick(2);
    };
    h.importer.begin_state = DownloadImportState::Pending;
    REQUIRE(h.service.beginRecoveredImport("old-1", finalPath()));
    REQUIRE(h.service.checkpoint()->receipts == receipts);
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Shutdown drains recovered pending import even with no native execution slot",
          "[download][recovered-import][FR-053]") {
    RecoveryHarness h;
    h.importer.begin_state = DownloadImportState::Pending;
    REQUIRE(h.service.beginRecoveredImport("old-1", finalPath()));
    h.service.shutdown();
    REQUIRE_FALSE(h.service.stopped());
    REQUIRE_FALSE(h.service.canImportRecovered("old-1"));
    REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
    h.service.tick(0);
    REQUIRE_FALSE(h.service.stopped());
    SECTION("saved") { h.importer.poll_state = DownloadImportState::Committed; }
    SECTION("failed") { h.importer.poll_state = DownloadImportState::Failed; }
    SECTION("unknown terminal") { h.importer.poll_state = static_cast<DownloadImportState>(99); }
    SECTION("throwing terminal") { h.importer.throw_poll = true; }
    h.service.tick(1);
    REQUIRE(h.service.stopped());
    REQUIRE(h.service.recoveredImportState("old-1") == (h.importer.poll_state == DownloadImportState::Committed
                                                            ? DownloadImportState::Committed
                                                            : DownloadImportState::Failed));
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(validDownloadCheckpoint(*h.service.checkpoint()));
}

TEST_CASE("Recovered import preflights the full checkpoint representation before touching its importer",
          "[download][recovered-import]") {
    auto checkpoint = interruptedCheckpoint();
    auto path = finalPath().parent_path() / (std::string(28'990, 'x') + ".mp3");
    SECTION("per-path representation") { path = finalPath().parent_path() / (std::string(30'001, 'x') + ".mp3"); }
    SECTION("aggregate representation") {
        checkpoint.receipts.front().retained_request_bytes = 1000;
        for (unsigned int index = 2; index <= 19; ++index) {
            auto job = checkpoint.jobs.front();
            job.job_id = "old-" + std::to_string(index);
            job.completed_mp3 = path;
            checkpoint.jobs.push_back(job);
            checkpoint.receipts.front().job_ids.push_back(job.job_id);
        }
        // Number of records adapts only to native path-unit width so both
        // Windows UTF-16 and Unix byte paths approach the same schema cap.
        if constexpr (sizeof(std::filesystem::path::value_type) == 1) {
            for (unsigned int index = 20; index <= 37; ++index) {
                if (index == 21) {
                    auto receipt = checkpoint.receipts.front();
                    receipt.request_id = request(2).request_id;
                    receipt.job_ids.clear();
                    checkpoint.receipts.push_back(receipt);
                }
                auto job = checkpoint.jobs.front();
                job.job_id = "old-" + std::to_string(index);
                job.completed_mp3 = path;
                checkpoint.jobs.push_back(job);
                checkpoint.receipts.back().job_ids.push_back(job.job_id);
            }
        }
    }
    REQUIRE(validDownloadCheckpoint(checkpoint));
    RecoveryHarness h({}, checkpoint);
    const auto before = h.service.checkpoint();
    REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", path));
    REQUIRE(h.service.checkpoint() == before);
    REQUIRE_FALSE(h.service.recoveredImportState("old-1"));
    REQUIRE(h.importer.imports.empty());
}

TEST_CASE("Recovered import rechecks the owner handoff predicate after capability callbacks",
          "[download][recovered-import][FR-053]") {
    RecoveryHarness h;
    bool allowed = true;
    bool throw_predicate = false;
    SECTION("owner refuses") { allowed = false; }
    SECTION("owner predicate throws") { throw_predicate = true; }
    SECTION("capability callback closes the trusted owner") {
        h.importer.on_capability = [&] { allowed = false; };
    }
    const auto before = h.service.checkpoint();
    int predicate_calls = 0;
    REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath(), [&] {
        ++predicate_calls;
        if (throw_predicate)
            throw std::runtime_error{"private owner guard failure"};
        return allowed;
    }));
    REQUIRE(predicate_calls == 1);
    REQUIRE(h.service.checkpoint() == before);
    REQUIRE_FALSE(h.service.recoveredImportState("old-1"));
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Recovered import handoff predicate cannot reenter mutations or duplicate the handoff",
          "[download][recovered-import]") {
    RecoveryHarness h;
    int predicate_calls = 0;
    REQUIRE(h.service.beginRecoveredImport("old-1", finalPath(), [&] {
        ++predicate_calls;
        REQUIRE_FALSE(h.service.canImportRecovered("old-1"));
        REQUIRE_FALSE(h.service.beginRecoveredImport("old-1", finalPath()));
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
        h.service.tick(4);
        return true;
    }));
    REQUIRE(predicate_calls == 1);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Imported);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("A published download remains importing until its durable handoff commits", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit();
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    const auto pending = h.service.snapshot()[0];
    REQUIRE(pending.state == DownloadJobState::Importing);
    REQUIRE(pending.error == DownloadFailure::None);
    REQUIRE(pending.completed_mp3 == finalPath());
    REQUIRE_FALSE(h.service.retryImport(ids[0]));
    REQUIRE_FALSE(h.service.cancel(ids[0]));
    h.service.tick(std::numeric_limits<std::int64_t>::max());
    REQUIRE(h.service.snapshot()[0] == pending);
    REQUIRE(h.execution.polls.size() == 1);
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.importer.import_polls == std::vector<std::string>{ids[0]});
    h.importer.poll_state = DownloadImportState::Committed;
    h.service.tick(0);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.snapshot()[0].completed_units == pending.completed_units);
    REQUIRE(h.service.snapshot()[0].total_units == pending.total_units);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.execution.cancellations.empty());
}

TEST_CASE("Durable import pending does not hold the native download execution slot", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit(2);
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Importing);
    REQUIRE(h.execution.starts.size() == 2);
    REQUIRE(h.execution.starts.back().first == ids[1]);
    h.service.tick(3);
    REQUIRE(h.execution.polls == std::vector<std::string>{ids[0], ids[1]});
    REQUIRE(h.importer.import_polls == std::vector<std::string>{ids[0], ids[0]});
    REQUIRE(h.execution.cancellations.empty());
}

TEST_CASE("A trusted start gate refuses work promptly while durable import drains", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit(2);
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    const bool throws = GENERATE(false, true);
    h.service.tick(2, [&] {
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
        REQUIRE_FALSE(h.service.cancel(ids[1]));
        h.service.tick(3);
        if (throws)
            throw std::runtime_error("private shutdown state");
        return false;
    });
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Importing);
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Queued);
    REQUIRE(h.importer.import_polls == std::vector<std::string>{ids[0]});
    h.service.tick(4, [] { return true; });
    REQUIRE(h.execution.starts.size() == 2);
    REQUIRE(h.execution.starts.back().first == ids[1]);
}

TEST_CASE("All durable handoffs drain independently after the native slot is released", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit(2);
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    h.service.tick(2);
    published(h.execution, ids[1]);
    h.service.tick(3);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Importing);
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Importing);
    h.service.shutdown();
    REQUIRE_FALSE(h.service.stopped());
    h.importer.poll_state = DownloadImportState::Committed;
    h.service.tick(4);
    REQUIRE(h.service.stopped());
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Completed);
    REQUIRE(h.importer.imports.size() == 2);
    REQUIRE(h.importer.import_polls.back() == ids[1]);
    REQUIRE(h.execution.starts.size() == 2);
    REQUIRE(h.execution.polls.size() == 2);
    REQUIRE(h.execution.cancellations.empty());
}

TEST_CASE("Failed durable imports preserve the published file and retry without redownload",
          "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit();
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    h.importer.poll_state = DownloadImportState::Failed;
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::ImportFailed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Import);
    REQUIRE(h.service.snapshot()[0].completed_mp3 == finalPath());
    REQUIRE(h.service.retryImport(ids[0]));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Importing);
    REQUIRE(h.importer.imports.size() == 2);
    REQUIRE(h.importer.imports[0] == h.importer.imports[1]);
    h.importer.poll_state = DownloadImportState::Committed;
    h.service.tick(3);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.polls.size() == 1);
    REQUIRE_FALSE(h.service.retryImport(ids[0]));
}

TEST_CASE("Shutdown drains durable import without cancelling terminal execution", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit(2);
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    h.service.shutdown();
    REQUIRE_FALSE(h.service.stopped());
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Cancelled);
    REQUIRE_FALSE(h.service.retryImport(ids[0]));
    h.service.tick(std::numeric_limits<std::int64_t>::max());
    REQUIRE_FALSE(h.service.stopped());
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.polls.size() == 1);
    REQUIRE(h.execution.cancellations.empty());
    h.importer.poll_state = GENERATE(DownloadImportState::Committed, DownloadImportState::Failed);
    h.service.tick(0);
    REQUIRE(h.service.stopped());
    REQUIRE(h.service.snapshot()[0].state == (h.importer.poll_state == DownloadImportState::Committed
                                                  ? DownloadJobState::Completed
                                                  : DownloadJobState::ImportFailed));
    REQUIRE(h.execution.cancellations.empty());
}

TEST_CASE("Import port callbacks cannot reenter mutations or duplicate pending handoffs",
          "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit();
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    h.importer.on_import_poll = [&] {
        REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Importing);
        REQUIRE_FALSE(h.service.retryImport(ids[0]));
        REQUIRE_FALSE(h.service.cancel(ids[0]));
        REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::Busy);
        h.service.tick(3);
        h.service.shutdown();
    };
    h.service.tick(2);
    REQUIRE(h.importer.import_polls.size() == 1);
    REQUIRE(h.importer.imports.size() == 1);
    REQUIRE(h.execution.polls.size() == 1);
    h.importer.on_import_poll = {};
    h.importer.poll_state = DownloadImportState::Committed;
    h.service.tick(4);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Completed);
    REQUIRE(h.service.submit(request(2)).error == DownloadSubmissionError::None);
}

TEST_CASE("Invalid or throwing begin cannot report a committed import", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = static_cast<DownloadImportState>(99);
    SECTION("invalid state") {}
    SECTION("exception") { h.importer.throws = true; }
    const auto ids = h.admit();
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::ImportFailed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Import);
    REQUIRE(h.service.snapshot()[0].completed_mp3 == finalPath());
    REQUIRE(h.execution.cancellations.empty());
}

TEST_CASE("Invalid or throwing import polls never manufacture durable completion", "[download][import-pending]") {
    Harness h;
    h.importer.begin_state = DownloadImportState::Pending;
    const auto ids = h.admit();
    h.service.tick(0);
    published(h.execution, ids[0]);
    h.service.tick(1);
    SECTION("invalid state") { h.importer.poll_state = static_cast<DownloadImportState>(99); }
    SECTION("exception") { h.importer.throw_poll = true; }
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::ImportFailed);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Import);
    REQUIRE(h.service.snapshot()[0].completed_mp3 == finalPath());
    REQUIRE(h.importer.import_polls.size() == 1);
    REQUIRE(h.execution.cancellations.empty());
}

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

TEST_CASE("Download capacity observes an empty ledger and configured limits without invoking ports",
          "[download][capacity]") {
    const DownloadServiceLimits limits{.max_jobs = 7, .max_requests = 3, .max_retained_request_bytes = 4096};
    Harness h(limits);
    requireCapacity(h.service, {.max_jobs = 7, .max_requests = 3, .max_retained_request_bytes = 4096});
    requireCapacity(h.service, {.max_jobs = 7, .max_requests = 3, .max_retained_request_bytes = 4096});
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.importer.import_polls.empty());
    REQUIRE(h.importer.capability_queries == 0);
}

TEST_CASE("Download capacity retains exact request accounting through replay rejection and cancellation",
          "[download][capacity]") {
    Harness h({.max_jobs = 3, .max_requests = 2, .max_retained_request_bytes = 4096});
    const auto direct = request(1, 2);
    const auto typed = youtubeRequest(2);
    const auto direct_bytes = direct.request_id.size() + direct.items[0].url.size() + direct.items[0].title.size() +
                              direct.items[1].url.size() + direct.items[1].title.size();
    const auto& source = *typed.items[0].youtube;
    const auto typed_bytes = typed.request_id.size() + typed.items[0].url.size() + typed.items[0].title.size() +
                             source.video_id.size() + source.title.size() + source.artist.size() +
                             source.lyrics_page.size() + 1;
    const auto first = h.service.submit(direct);
    REQUIRE(first.error == DownloadSubmissionError::None);
    requireCapacity(h.service, {.jobs = 2,
                                .max_jobs = 3,
                                .requests = 1,
                                .max_requests = 2,
                                .retained_request_bytes = direct_bytes,
                                .max_retained_request_bytes = 4096});
    REQUIRE(h.service.submit(typed).error == DownloadSubmissionError::None);
    const DownloadServiceCapacity retained{.jobs = 3,
                                           .max_jobs = 3,
                                           .requests = 2,
                                           .max_requests = 2,
                                           .retained_request_bytes = direct_bytes + typed_bytes,
                                           .max_retained_request_bytes = 4096};
    requireCapacity(h.service, retained);
    REQUIRE(h.service.submit(direct).job_ids == first.job_ids);
    REQUIRE(h.service.submit(request(3)).error == DownloadSubmissionError::LimitExceeded);
    auto invalid = request(4);
    invalid.request_id = "invalid";
    REQUIRE(h.service.submit(invalid).error == DownloadSubmissionError::InvalidRequest);
    requireCapacity(h.service, retained);
    REQUIRE(h.service.cancel(first.job_ids.front()));
    h.service.shutdown();
    REQUIRE(h.service.stopped());
    requireCapacity(h.service, retained);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.importer.imports.empty());
    REQUIRE(h.importer.import_polls.empty());
    REQUIRE(h.importer.capability_queries == 0);
}

TEST_CASE("Download capacity commits only accepted restored accounting and preserves recovered replay",
          "[download][capacity]") {
    const DownloadServiceLimits limits{.max_jobs = 5, .max_requests = 2, .max_retained_request_bytes = 500};
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "fresh", limits, &identity);
    auto history = interruptedCheckpoint();
    history.receipts[0].retained_request_bytes = 501;
    REQUIRE_FALSE(service.restore(history));
    requireCapacity(service, {.max_jobs = 5, .max_requests = 2, .max_retained_request_bytes = 500});
    history.receipts[0].retained_request_bytes = 100;
    REQUIRE(service.restore(history));
    const DownloadServiceCapacity retained{.jobs = 1,
                                           .max_jobs = 5,
                                           .requests = 1,
                                           .max_requests = 2,
                                           .retained_request_bytes = 100,
                                           .max_retained_request_bytes = 500};
    const auto before = service.snapshot();
    const auto identity_calls = identity.calls;
    requireCapacity(service, retained);
    REQUIRE(identity.calls == identity_calls);
    REQUIRE(service.snapshot() == before);
    REQUIRE(service.submit(request()).job_ids == std::vector<std::string>{"old-1"});
    REQUIRE_FALSE(service.restore(history));
    service.shutdown();
    requireCapacity(service, retained);
    REQUIRE(service.snapshot() == before);
    REQUIRE(execution.starts.empty());
    REQUIRE(execution.polls.empty());
    REQUIRE(execution.cancellations.empty());
    REQUIRE(importer.imports.empty());
    REQUIRE(importer.import_polls.empty());
    REQUIRE(importer.capability_queries == 0);
}

TEST_CASE("A settled archive releases active quota only when confirmed and preserves exact request replay",
          "[download][archive]") {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "test", {.max_jobs = 2, .max_requests = 1}, &identity);
    const auto original = request(1, 2);
    const auto admitted = service.submit(original);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    REQUIRE(service.cancel(admitted.job_ids[0]));
    REQUIRE_FALSE(service.canArchiveSettled());
    REQUIRE_FALSE(service.stageSettledArchive());
    REQUIRE(service.cancel(admitted.job_ids[1]));
    REQUIRE(service.canArchiveSettled());
    const auto before = service.checkpoint();
    REQUIRE(before);
    const auto candidate = service.stageSettledArchive();
    REQUIRE(candidate);
    REQUIRE(validDownloadCheckpoint(*candidate));
    REQUIRE(candidate->jobs.empty());
    REQUIRE(candidate->receipts.empty());
    REQUIRE(candidate->archived_receipts.size() == 1);
    REQUIRE(candidate->archived_receipts.front().request_id == original.request_id);
    REQUIRE(candidate->archived_receipts.front().fingerprint == before->receipts.front().fingerprint);
    REQUIRE(candidate->archived_receipts.front().job_ids == admitted.job_ids);
    REQUIRE(service.checkpoint() == before);
    REQUIRE(service.capacity().jobs == 2);
    REQUIRE(service.capacity().requests == 1);
    REQUIRE(service.capacity().retained_request_bytes == before->receipts.front().retained_request_bytes);
    REQUIRE_FALSE(service.canArchiveSettled());
    REQUIRE_FALSE(service.stageSettledArchive());
    REQUIRE(service.submit(original).job_ids == admitted.job_ids);
    REQUIRE(service.submit(request(2)).error == DownloadSubmissionError::Busy);

    STATIC_REQUIRE(noexcept(service.confirmSettledArchive()));
    REQUIRE(service.confirmSettledArchive());
    REQUIRE_FALSE(service.confirmSettledArchive());
    REQUIRE(service.checkpoint() == candidate);
    REQUIRE(service.capacity().jobs == 0);
    REQUIRE(service.capacity().requests == 0);
    REQUIRE(service.capacity().retained_request_bytes == 0);
    const auto calls = identity.calls;
    REQUIRE(service.submit(original).job_ids == admitted.job_ids);
    REQUIRE(identity.calls == calls + 1);
    auto changed = original;
    changed.items[0].title = "Changed archived request";
    REQUIRE(service.submit(changed).error == DownloadSubmissionError::InvalidRequest);
    REQUIRE(service.submit(request(2)).job_ids == std::vector<std::string>{"test-3"});
    REQUIRE(execution.starts.empty());
    REQUIRE(execution.polls.empty());
    REQUIRE(importer.imports.empty());
}

TEST_CASE("Settled archive waits for active work and keeps an entire batch with an import retry",
          "[download][archive]") {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "test", {}, &identity);
    const auto settled = service.submit(request(1));
    const auto mixed = service.submit(request(2, 2));
    REQUIRE(settled.error == DownloadSubmissionError::None);
    REQUIRE(mixed.error == DownloadSubmissionError::None);
    REQUIRE(service.cancel(settled.job_ids.front()));
    REQUIRE(service.cancel(mixed.job_ids.front()));
    REQUIRE_FALSE(service.canArchiveSettled());
    service.tick(0);
    REQUIRE_FALSE(service.canArchiveSettled());
    importer.begin_state = DownloadImportState::Pending;
    published(execution, mixed.job_ids.back());
    service.tick(1);
    REQUIRE_FALSE(service.canArchiveSettled());
    importer.poll_state = DownloadImportState::Failed;
    service.tick(2);
    const auto candidate = service.stageSettledArchive();
    REQUIRE(candidate);
    REQUIRE(candidate->archived_receipts.size() == 1);
    REQUIRE(candidate->archived_receipts.front().job_ids == settled.job_ids);
    REQUIRE(candidate->receipts.size() == 1);
    REQUIRE(candidate->receipts.front().job_ids == mixed.job_ids);
    REQUIRE(candidate->jobs.size() == 2);
    REQUIRE(candidate->jobs.back().state == DownloadJobState::ImportFailed);
    REQUIRE_FALSE(service.retryImport(mixed.job_ids.back()));
    REQUIRE(importer.imports.size() == 1);
    REQUIRE(service.confirmSettledArchive());
    REQUIRE(service.capacity().jobs == 2);
    REQUIRE_FALSE(service.canArchiveSettled());
    importer.begin_state = DownloadImportState::Committed;
    REQUIRE(service.retryImport(mixed.job_ids.back()));
    const auto second = service.stageSettledArchive();
    REQUIRE(second);
    REQUIRE(second->archived_receipts.size() == 2);
    REQUIRE(second->jobs.empty());
    service.shutdown();
    REQUIRE(service.stopped());
    REQUIRE(service.confirmSettledArchive());
    REQUIRE(service.checkpoint() == second);
}

TEST_CASE("Restored archive preserves namespace and replay without occupying active quota", "[download][archive]") {
    DownloadCheckpoint history;
    history.archived_receipts.push_back(
        {.request_id = request().request_id, .fingerprint = std::string(64, 'a'), .job_ids = {"fresh-1"}});
    const std::vector<DownloadImportReceipt> confirmed{{.job_id = "fresh-1", .source_path = finalPath()}};
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "fresh", {.max_jobs = 1, .max_requests = 1}, &identity);
    REQUIRE_FALSE(service.restore(history));
    REQUIRE(service.recoveryReport().status == DownloadRecoveryStatus::NamespaceConflict);
    REQUIRE(service.restoreWithFreshNamespace(history, confirmed));
    REQUIRE(service.recoveryReport().session_only_job_ids.empty());
    REQUIRE(service.checkpoint() == history);
    REQUIRE(service.capacity().jobs == 0);
    REQUIRE(service.capacity().requests == 0);
    REQUIRE(service.submitStaged(request()).job_ids == std::vector<std::string>{"fresh-1"});
    REQUIRE_FALSE(service.confirmStaged(request().request_id));
    REQUIRE(service.submit(request(2)).job_ids == std::vector<std::string>{"fresh-r001-1"});
    REQUIRE(execution.starts.empty());
    REQUIRE(importer.imports.empty());
}

TEST_CASE("Execution receives only acknowledged archived jobs and current durable retirement evidence",
          "[download][archive][ownership-retirement]") {
    DownloadCheckpoint history;
    const std::vector<std::string> archived{"old-1", "old-2"};
    history.archived_receipts.push_back(
        {.request_id = request().request_id, .fingerprint = std::string(64, 'a'), .job_ids = archived});
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "fresh", {}, &identity);
    REQUIRE(service.restore(history));
    execution.accepted = false;
    int retirement_evidence_queries = 0;
    importer.on_retirement_evidence = [&] {
        ++retirement_evidence_queries;
        REQUIRE(service.submit(request(9)).error == DownloadSubmissionError::Busy);
    };
    REQUIRE(service.submit(request(2)).error == DownloadSubmissionError::None);
    service.tick(0);
    REQUIRE(execution.starts.size() == 1);
    REQUIRE(execution.archived_starts.empty());
    REQUIRE(retirement_evidence_queries == 0);

    service.retireArchivedImportReceipts();
    REQUIRE(service.submit(request(3)).error == DownloadSubmissionError::None);
    service.tick(1);
    REQUIRE(execution.archived_starts.size() == 1);
    REQUIRE(execution.archived_starts.back().archived_job_ids == archived);
    REQUIRE(execution.archived_starts.back().retired_import_job_ids.empty());

    importer.retired_import_job_ids = {"old-2"};
    REQUIRE(service.submit(request(4)).error == DownloadSubmissionError::None);
    service.tick(2);
    REQUIRE(execution.archived_starts.size() == 2);
    REQUIRE(execution.archived_starts.back().archived_job_ids == archived);
    REQUIRE(execution.archived_starts.back().retired_import_job_ids == importer.retired_import_job_ids);
    REQUIRE(retirement_evidence_queries == 2);
}

TEST_CASE("An unfinished live published YouTube lyric offer does not survive restart", "[download][archive]") {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "live", {}, &identity);
    const auto admitted = service.submit(youtubeRequest());
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    const auto& id = admitted.job_ids.front();
    service.tick(0);
    published(execution, id);
    service.tick(1);
    REQUIRE(service.snapshot().front().state == DownloadJobState::Completed);
    REQUIRE(service.publishedYoutubeAudio(id));
    REQUIRE_FALSE(service.canArchiveSettled());
    REQUIRE_FALSE(service.stageSettledArchive());
    const auto history = service.checkpoint();
    REQUIRE(history);
    const std::vector<DownloadImportReceipt> confirmed{{.job_id = id, .source_path = finalPath()}};
    DownloadService restored(execution, importer, "restored", {}, &identity);
    REQUIRE(restored.restore(*history, confirmed));
    REQUIRE_FALSE(restored.publishedYoutubeAudio(id));
    REQUIRE_FALSE(restored.finishPublishedLyricsReview(id));
    REQUIRE(restored.canArchiveSettled());
}

TEST_CASE("Finishing a live YouTube lyric review releases only that job's hold on whole-batch archival",
          "[download][archive][published-lyrics]") {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "live", {}, &identity);
    auto input = youtubeRequest();
    auto sibling = input.items.front();
    sibling.youtube->video_id = "ZbCdEf_12-3";
    sibling.url = "https://www.youtube.com/watch?v=" + sibling.youtube->video_id;
    input.items.push_back(sibling);
    const auto admitted = service.submit(input);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    REQUIRE(admitted.job_ids.size() == 2);
    const auto& first = admitted.job_ids.front();
    const auto& second = admitted.job_ids.back();
    REQUIRE_FALSE(service.finishPublishedLyricsReview(first));
    REQUIRE_FALSE(service.finishPublishedLyricsReview("unknown"));
    service.tick(0);
    published(execution, first);
    service.tick(1);
    REQUIRE(service.publishedYoutubeAudio(first));
    execution.on_start = [&] { REQUIRE_FALSE(service.finishPublishedLyricsReview(first)); };
    service.tick(2);
    published(execution, second);
    service.tick(3);
    REQUIRE(service.publishedYoutubeAudio(second));
    const auto before = service.checkpoint();
    REQUIRE(before);
    REQUIRE_FALSE(service.canArchiveSettled());

    REQUIRE(service.finishPublishedLyricsReview(first));
    REQUIRE_FALSE(service.finishPublishedLyricsReview(first));
    REQUIRE_FALSE(service.publishedYoutubeAudio(first));
    REQUIRE(service.publishedYoutubeAudio(second));
    REQUIRE_FALSE(service.canArchiveSettled());
    REQUIRE(service.finishPublishedLyricsReview(second));
    REQUIRE_FALSE(service.publishedYoutubeAudio(second));
    REQUIRE(service.canArchiveSettled());
    REQUIRE(service.checkpoint() == before);
    REQUIRE(service.submit(input).job_ids == admitted.job_ids);
    auto changed = input;
    changed.items.front().youtube->fetch_lyrics = false;
    REQUIRE(service.submit(changed).error == DownloadSubmissionError::InvalidRequest);
    const auto archive = service.stageSettledArchive();
    REQUIRE(archive);
    REQUIRE(archive->jobs.empty());
    REQUIRE(archive->archived_receipts.size() == 1);
    REQUIRE(archive->archived_receipts.front().job_ids == admitted.job_ids);
    REQUIRE(archive->archived_receipts.front().fingerprint == before->receipts.front().fingerprint);
    REQUIRE(service.capacity().jobs == 2);
    REQUIRE(service.confirmSettledArchive());
    REQUIRE(service.capacity().jobs == 0);
    REQUIRE(service.submit(input).job_ids == admitted.job_ids);
    REQUIRE(execution.starts.size() == 2);
    REQUIRE(importer.imports.size() == 2);
}

TEST_CASE("A lyric review completed while another batch awaits archive confirmation remains complete",
          "[download][archive][published-lyrics]") {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "live", {}, &identity);
    const auto retired = service.submit(request(1));
    REQUIRE(retired.error == DownloadSubmissionError::None);
    REQUIRE(service.cancel(retired.job_ids.front()));
    const auto held = service.submit(youtubeRequest(2));
    REQUIRE(held.error == DownloadSubmissionError::None);
    const auto& id = held.job_ids.front();
    service.tick(0);
    published(execution, id);
    service.tick(1);
    REQUIRE(service.publishedYoutubeAudio(id));
    const auto archive = service.stageSettledArchive();
    REQUIRE(archive);
    REQUIRE(archive->jobs.size() == 1);
    REQUIRE(archive->jobs.front().job_id == id);
    REQUIRE(archive->archived_receipts.front().job_ids == retired.job_ids);

    REQUIRE(service.finishPublishedLyricsReview(id));
    REQUIRE_FALSE(service.publishedYoutubeAudio(id));
    REQUIRE(service.confirmSettledArchive());
    REQUIRE(service.checkpoint() == archive);
    REQUIRE_FALSE(service.publishedYoutubeAudio(id));
    REQUIRE_FALSE(service.finishPublishedLyricsReview(id));
    REQUIRE(service.canArchiveSettled());
    const auto second = service.stageSettledArchive();
    REQUIRE(second);
    REQUIRE(second->jobs.empty());
    REQUIRE(second->archived_receipts.back().job_ids == held.job_ids);
}

TEST_CASE("A full replay archive never evicts receipts or releases unrecorded history", "[download][archive]") {
    DownloadCheckpoint history;
    for (std::size_t index = 0; index < download_checkpoint_max_archived_receipts; ++index) {
        const auto input = request(1000 + static_cast<unsigned int>(index));
        history.archived_receipts.push_back({.request_id = input.request_id,
                                             .fingerprint = std::string(64, 'a'),
                                             .job_ids = {"old-" + std::to_string(index)}});
    }
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService service(execution, importer, "test", {.max_jobs = 1, .max_requests = 1}, &identity);
    REQUIRE(service.restore(history));
    const auto admitted = service.submit(request());
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    REQUIRE(service.cancel(admitted.job_ids.front()));
    REQUIRE_FALSE(service.canArchiveSettled());
    REQUIRE_FALSE(service.stageSettledArchive());
    REQUIRE(service.capacity().jobs == 1);
    REQUIRE(service.checkpoint()->archived_receipts == history.archived_receipts);
    REQUIRE(service.submit(request(1000)).job_ids == std::vector<std::string>{"old-0"});
    REQUIRE(service.submit(request(2)).error == DownloadSubmissionError::LimitExceeded);
}

TEST_CASE("Published YouTube context requires a live publication and survives pending or failed queue import",
          "[download][published-lyrics]") {
    Harness h;
    auto input = youtubeRequest();
    const auto admitted = h.service.submit(input);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    const auto id = admitted.job_ids.front();
    REQUIRE_FALSE(h.service.publishedYoutubeAudio(id));
    h.importer.begin_state = DownloadImportState::Pending;
    h.service.tick(0);
    REQUIRE_FALSE(h.service.publishedYoutubeAudio(id));
    published(h.execution, id);
    h.service.tick(1);
    const auto live = h.service.publishedYoutubeAudio(id);
    REQUIRE(live);
    REQUIRE(live->job_id == id);
    REQUIRE(live->published_mp3 == finalPath());
    REQUIRE(live->source == *input.items[0].youtube);
    input.items[0].youtube->artist = "Changed caller";
    REQUIRE(h.service.publishedYoutubeAudio(id)->source.artist != input.items[0].youtube->artist);
    h.importer.poll_state = DownloadImportState::Failed;
    h.service.tick(2);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::ImportFailed);
    REQUIRE(h.service.publishedYoutubeAudio(id));
    REQUIRE_FALSE(h.service.publishedYoutubeAudio("unknown"));
}
TEST_CASE("Only live jobs requesting lyrics can finish a published YouTube review", "[download][published-lyrics]") {
    Harness h;
    SECTION("direct") {
        const auto ids = h.admit();
        h.service.tick(0);
        published(h.execution, ids.front());
        h.service.tick(1);
        REQUIRE_FALSE(h.service.publishedYoutubeAudio(ids.front()));
        REQUIRE_FALSE(h.service.finishPublishedLyricsReview(ids.front()));
    }
    SECTION("cancelled") {
        const auto result = h.service.submit(youtubeRequest());
        REQUIRE(result.error == DownloadSubmissionError::None);
        REQUIRE(h.service.cancel(result.job_ids.front()));
        REQUIRE_FALSE(h.service.publishedYoutubeAudio(result.job_ids.front()));
        REQUIRE_FALSE(h.service.finishPublishedLyricsReview(result.job_ids.front()));
    }
    SECTION("lyrics not requested") {
        auto input = youtubeRequest();
        input.items.front().youtube->fetch_lyrics = false;
        const auto result = h.service.submit(input);
        REQUIRE(result.error == DownloadSubmissionError::None);
        h.service.tick(0);
        published(h.execution, result.job_ids.front());
        h.service.tick(1);
        REQUIRE(h.service.publishedYoutubeAudio(result.job_ids.front()));
        REQUIRE_FALSE(h.service.finishPublishedLyricsReview(result.job_ids.front()));
    }
}
TEST_CASE("Recovered YouTube receipts do not acquire publication authority even after explicit replay",
          "[download][published-lyrics]") {
    FakeExecution execution;
    FakeImport importer;
    RecoveryIdentity identity;
    DownloadService original(execution, importer, "old", {}, &identity);
    const auto input = youtubeRequest();
    const auto admitted = original.submit(input);
    REQUIRE(admitted.error == DownloadSubmissionError::None);
    original.tick(0);
    published(execution, admitted.job_ids.front());
    original.tick(1);
    const auto checkpoint = original.checkpoint();
    REQUIRE(checkpoint);
    FakeExecution fresh_execution;
    FakeImport fresh_importer;
    DownloadService restored(fresh_execution, fresh_importer, "new", {}, &identity);
    REQUIRE(restored.restore(*checkpoint));
    REQUIRE_FALSE(restored.publishedYoutubeAudio(admitted.job_ids.front()));
    REQUIRE(restored.submit(input).job_ids == admitted.job_ids);
    REQUIRE_FALSE(restored.publishedYoutubeAudio(admitted.job_ids.front()));
    REQUIRE(fresh_execution.starts.empty());
    REQUIRE(fresh_importer.imports.empty());
}
TEST_CASE("Download admission owns every YouTube field and rejects changed request replay",
          "[download][youtube][FR-050]") {
    Harness h;
    auto input = youtubeRequest();
    const auto original = input;
    const auto accepted = h.service.submit(input);
    REQUIRE(accepted.error == DownloadSubmissionError::None);
    SECTION("video identity") { input.items[0].youtube->video_id = "ZbCdEf_12-3"; }
    SECTION("metadata title") { input.items[0].youtube->title += " changed"; }
    SECTION("artist") { input.items[0].youtube->artist += " changed"; }
    SECTION("lyrics page") { input.items[0].youtube->lyrics_page += "other/"; }
    SECTION("lyrics choice") { input.items[0].youtube->fetch_lyrics = false; }
    SECTION("optional presence") { input.items[0].youtube.reset(); }
    REQUIRE(input.items != original.items);
    REQUIRE(h.service.submit(input).error == DownloadSubmissionError::InvalidRequest);
    REQUIRE(h.service.submit(original).job_ids == accepted.job_ids);
    h.service.tick(0);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.starts.front().second == original.items.front());
}

TEST_CASE("YouTube typed metadata is bounded coherently before consuming any receipt or job",
          "[download][youtube][FR-050]") {
    Harness h;
    auto input = youtubeRequest();
    auto& item = input.items.front();
    auto& youtube = *item.youtube;
    SECTION("short ID") { youtube.video_id.pop_back(); }
    SECTION("long ID") { youtube.video_id.push_back('x'); }
    SECTION("nonopaque ID") { youtube.video_id[0] = '/'; }
    SECTION("ID NUL") { youtube.video_id[0] = '\0'; }
    SECTION("empty metadata title") { youtube.title.clear(); }
    SECTION("metadata title bytes") { youtube.title.assign(513, 't'); }
    SECTION("metadata title NUL") { youtube.title.push_back('\0'); }
    SECTION("artist bytes") { youtube.artist.assign(513, 'a'); }
    SECTION("artist NUL") { youtube.artist.push_back('\0'); }
    SECTION("lyrics page bytes") { youtube.lyrics_page.assign(2049, 'p'); }
    SECTION("lyrics page NUL") { youtube.lyrics_page.push_back('\0'); }
    SECTION("mismatched canonical ID") { youtube.video_id = "ZbCdEf_12-3"; }
    SECTION("mismatched title") { youtube.title = "Another title"; }
    SECTION("video kind") { item.kind = DownloadMediaKind::Video; }
    SECTION("direct URL with context") { item.url = "https://media.example/audio.mp3"; }
    SECTION("canonical URL extra query") { item.url += "&arbitrary=1"; }
    const auto result = h.service.submit(input);
    REQUIRE(result.error == DownloadSubmissionError::InvalidRequest);
    REQUIRE(result.job_ids.empty());
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.service.submit(youtubeRequest(2)).job_ids == std::vector<std::string>{"test-1"});
}

TEST_CASE("YouTube metadata inclusive byte boundaries remain usable without artist or lyrics page",
          "[download][youtube][FR-050]") {
    Harness h;
    auto input = youtubeRequest();
    auto& item = input.items.front();
    item.title.assign(512, 't');
    item.youtube->title = item.title;
    SECTION("maximum retained fields") {
        item.youtube->artist.assign(512, 'a');
        constexpr std::string_view prefix{"https://letras.mus.br/"};
        item.youtube->lyrics_page = std::string(prefix) + std::string(2048 - prefix.size(), 'p');
    }
    SECTION("optional empty metadata") {
        item.youtube->artist.clear();
        item.youtube->lyrics_page.clear();
        item.youtube->fetch_lyrics = false;
    }
    REQUIRE(h.service.submit(input).error == DownloadSubmissionError::None);
    h.service.tick(0);
    REQUIRE(h.execution.starts.size() == 1);
    REQUIRE(h.execution.starts.front().second == item);
}

TEST_CASE("YouTube quota counts all retained metadata bytes without changing the direct request budget",
          "[download][youtube][FR-050]") {
    const auto input = youtubeRequest();
    const auto& item = input.items.front();
    const auto& youtube = *item.youtube;
    const auto bytes = input.request_id.size() + item.url.size() + item.title.size() + youtube.video_id.size() +
                       youtube.title.size() + youtube.artist.size() + youtube.lyrics_page.size() + 1;
    SECTION("inclusive limit and replay") {
        RecoveryIdentity identity;
        FakeExecution execution;
        FakeImport importer;
        DownloadService service{execution, importer, "youtube", {.max_retained_request_bytes = bytes}, &identity};
        REQUIRE(service.submit(input).error == DownloadSubmissionError::None);
        REQUIRE(service.submit(input).error == DownloadSubmissionError::None);
        REQUIRE(service.checkpoint()->receipts.front().retained_request_bytes == bytes);
        REQUIRE(service.submit(youtubeRequest(2)).error == DownloadSubmissionError::LimitExceeded);
    }
    SECTION("one byte too small rejects atomically") {
        Harness h({.max_retained_request_bytes = bytes - 1});
        REQUIRE(h.service.submit(input).error == DownloadSubmissionError::LimitExceeded);
        REQUIRE(h.service.snapshot().empty());
        REQUIRE(h.execution.starts.empty());
    }
    SECTION("direct request still counts only its original strings") {
        const auto direct = request();
        const auto direct_bytes = direct.request_id.size() + direct.items[0].url.size() + direct.items[0].title.size();
        Harness h({.max_retained_request_bytes = direct_bytes});
        REQUIRE(h.service.submit(direct).error == DownloadSubmissionError::None);
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
