#include "m0/application/DownloadService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>

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

TEST_CASE("Archived download receipts retain unique request and job identities", "[download][checkpoint][archive]") {
    auto checkpoint = saved();
    checkpoint.archived_receipts.push_back({input(2).request_id, std::string(64, 'b'), {"archived-1", "archived-2"}});
    REQUIRE(validDownloadCheckpoint(checkpoint));
    auto& archive = checkpoint.archived_receipts.front();
    SECTION("live request ID reused") { archive.request_id = checkpoint.receipts.front().request_id; }
    SECTION("live job ID reused") { archive.job_ids.front() = checkpoint.jobs.front().job_id; }
    SECTION("archived job ID repeated") { archive.job_ids.back() = archive.job_ids.front(); }
    SECTION("malformed job ID") { archive.job_ids.front() = "archive/path"; }
    SECTION("malformed request ID") { archive.request_id = "not-a-uuid"; }
    SECTION("malformed fingerprint") { archive.fingerprint = std::string(64, 'B'); }
    SECTION("empty group") { archive.job_ids.clear(); }
    SECTION("oversized group") {
        for (unsigned int index = 2; index <= 20; ++index)
            archive.job_ids.push_back("extra-" + std::to_string(index));
    }
    SECTION("request ID repeated across archived groups") {
        checkpoint.archived_receipts.push_back({archive.request_id, std::string(64, 'c'), {"other-1"}});
    }
    SECTION("job ID repeated across archived groups") {
        checkpoint.archived_receipts.push_back({input(3).request_id, std::string(64, 'c'), {archive.job_ids.front()}});
    }
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
}

TEST_CASE("Archived download receipt count is bounded independently of live groups",
          "[download][checkpoint][archive]") {
    auto checkpoint = saved();
    for (unsigned int index = 0; index < download_checkpoint_max_archived_receipts; ++index) {
        checkpoint.archived_receipts.push_back(
            {input(index + 2).request_id, std::string(64, 'a'), {"archived-" + std::to_string(index)}});
    }
    REQUIRE(validDownloadCheckpoint(checkpoint));
    checkpoint.archived_receipts.push_back({input(1000).request_id, std::string(64, 'a'), {"overflow"}});
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
}

TEST_CASE("Archived receipt identities share the checkpoint byte budget with live rows",
          "[download][checkpoint][archive]") {
    DownloadCheckpoint checkpoint;
    const auto path =
        (candidate().parent_path() / (std::string(20'000 / sizeof(std::filesystem::path::value_type), 'x') + ".mp3"))
            .lexically_normal();
    for (unsigned int group = 0; group < 2; ++group) {
        auto& live = checkpoint.receipts.emplace_back(input(group + 1).request_id, std::string(64, 'a'));
        for (unsigned int index = 0; index < 20; ++index) {
            const auto id = "live-" + std::to_string(group * 20 + index);
            live.job_ids.push_back(id);
            checkpoint.jobs.push_back(
                {.job_id = id, .title = "Audio", .state = DownloadJobState::Completed, .completed_mp3 = path});
        }
        live.retained_request_bytes = 36 + live.job_ids.size() * 6;
    }
    REQUIRE(validDownloadCheckpoint(checkpoint));

    DownloadCheckpoint archive;
    for (unsigned int group = 0; group < 200; ++group) {
        auto& receipt = archive.archived_receipts.emplace_back(input(group + 3).request_id, std::string(64, 'b'));
        for (unsigned int index = 0; index < 20; ++index) {
            auto id = "archived-" + std::to_string(group) + "-" + std::to_string(index) + "-";
            id.resize(64, 'x');
            receipt.job_ids.push_back(std::move(id));
        }
    }
    REQUIRE(validDownloadCheckpoint(archive));
    checkpoint.archived_receipts = std::move(archive.archived_receipts);
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
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
    REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{});
}

TEST_CASE("Strict recovery recognises confirmed imports even before a candidate was checkpointed",
          "[download][checkpoint][reconciliation]") {
    const auto previous =
        GENERATE(DownloadJobState::Queued, DownloadJobState::Downloading, DownloadJobState::Converting,
                 DownloadJobState::Publishing, DownloadJobState::ImportFailed, DownloadJobState::Importing,
                 DownloadJobState::Completed, DownloadJobState::Interrupted, DownloadJobState::Imported);
    auto checkpoint = saved(previous);
    auto& original = checkpoint.jobs[0];
    original.completed_units = 2;
    original.total_units = 7;
    if (previous == DownloadJobState::Completed || previous == DownloadJobState::Importing ||
        previous == DownloadJobState::ImportFailed || previous == DownloadJobState::Imported) {
        original.completed_mp3 = candidate();
        if (previous != DownloadJobState::Imported)
            original.completed_units = 7;
    }
    if (previous == DownloadJobState::ImportFailed)
        original.error = DownloadFailure::Import;
    if (previous == DownloadJobState::Interrupted)
        original.error = DownloadFailure::Unavailable;
    const std::vector<DownloadImportReceipt> receipts{{"old-1", candidate()}};
    Harness h;
    REQUIRE(h.service.restore(checkpoint, receipts));
    const auto restored = h.service.snapshot()[0];
    REQUIRE(restored.state == DownloadJobState::Imported);
    REQUIRE(restored.error == DownloadFailure::None);
    REQUIRE(restored.completed_mp3.empty());
    REQUIRE(restored.completed_units == original.completed_units);
    REQUIRE(restored.total_units == original.total_units);
    REQUIRE(h.service.recoveryReport() ==
            DownloadRecoveryReport{.status = DownloadRecoveryStatus::Ready,
                                   .items = {{"old-1", DownloadRecoveryDisposition::Imported}}});
    const auto recovered = h.service.checkpoint();
    REQUIRE(recovered);
    REQUIRE(validDownloadCheckpoint(*recovered));
    REQUIRE(recovered->jobs[0].completed_mp3 == candidate());
    REQUIRE(recovered->receipts == checkpoint.receipts);
    REQUIRE_FALSE(h.service.retryImport("old-1"));
    REQUIRE_FALSE(h.service.cancel("old-1"));
    h.service.tick(0);
    h.service.shutdown();
    h.service.tick(std::numeric_limits<std::int64_t>::max());
    REQUIRE(h.service.stopped());
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.execution.polls == 0);
    REQUIRE(h.execution.cancels == 0);
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.identity.calls == 0);
}

TEST_CASE("Strict recovery leaves unconfirmed import history interrupted without losing private evidence",
          "[download][checkpoint][reconciliation]") {
    const auto previous = GENERATE(DownloadJobState::Completed, DownloadJobState::Importing,
                                   DownloadJobState::ImportFailed, DownloadJobState::Imported);
    auto checkpoint = saved(previous);
    checkpoint.jobs[0].completed_mp3 = candidate();
    checkpoint.jobs[0].completed_units = 4;
    checkpoint.jobs[0].total_units = previous == DownloadJobState::Imported ? 9 : 4;
    if (previous == DownloadJobState::ImportFailed)
        checkpoint.jobs[0].error = DownloadFailure::Import;
    Harness h;
    REQUIRE(h.service.restore(checkpoint, {}));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Unavailable);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.service.recoveryReport() ==
            DownloadRecoveryReport{.status = DownloadRecoveryStatus::Ready,
                                   .items = {{"old-1", DownloadRecoveryDisposition::Unconfirmed}}});
    REQUIRE(h.service.checkpoint()->jobs[0].completed_mp3 == candidate());
    REQUIRE(h.service.snapshot()[0].completed_units == 4);
    REQUIRE(h.service.snapshot()[0].total_units == checkpoint.jobs[0].total_units);
    h.service.tick(0);
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.execution.starts == 0);
}

TEST_CASE("Strict recovery reports inert history and keeps terminal diagnostics",
          "[download][checkpoint][reconciliation]") {
    const auto previous =
        GENERATE(DownloadJobState::Queued, DownloadJobState::Downloading, DownloadJobState::Converting,
                 DownloadJobState::Publishing, DownloadJobState::Cancelling, DownloadJobState::Failed,
                 DownloadJobState::Cancelled, DownloadJobState::Interrupted);
    auto checkpoint = saved(previous);
    const bool unchanged = previous == DownloadJobState::Failed || previous == DownloadJobState::Cancelled ||
                           previous == DownloadJobState::Interrupted;
    if (previous == DownloadJobState::Failed || previous == DownloadJobState::Interrupted)
        checkpoint.jobs[0].error = DownloadFailure::Publication;
    Harness h;
    REQUIRE(h.service.restore(checkpoint, {}));
    REQUIRE(h.service.snapshot()[0].state == (unchanged ? previous : DownloadJobState::Interrupted));
    REQUIRE(h.service.snapshot()[0].error == (unchanged ? checkpoint.jobs[0].error : DownloadFailure::Unavailable));
    REQUIRE(h.service.recoveryReport().items ==
            std::vector<DownloadRecoveryItem>{{"old-1", unchanged ? DownloadRecoveryDisposition::Unchanged
                                                                  : DownloadRecoveryDisposition::Interrupted}});
}

TEST_CASE("Strict recovery refuses receipt contradictions atomically and permits a pristine retry",
          "[download][checkpoint][reconciliation]") {
    auto checkpoint = saved(DownloadJobState::Completed);
    checkpoint.jobs[0].completed_mp3 = candidate();
    std::vector<DownloadImportReceipt> receipts{{"old-1", candidate()}};
    SECTION("different path") { receipts[0].source_path = candidate().parent_path() / "other.mp3"; }
    SECTION("lexical case mismatch") { receipts[0].source_path = candidate().parent_path() / "Final.mp3"; }
    SECTION("cancelled with receipt") {
        checkpoint.jobs[0].state = DownloadJobState::Cancelled;
        checkpoint.jobs[0].completed_mp3.clear();
    }
    SECTION("failed with receipt") {
        checkpoint.jobs[0].state = DownloadJobState::Failed;
        checkpoint.jobs[0].completed_mp3.clear();
        checkpoint.jobs[0].error = DownloadFailure::Publication;
    }
    SECTION("cancelling with receipt") {
        checkpoint.jobs[0].state = DownloadJobState::Cancelling;
        checkpoint.jobs[0].completed_mp3.clear();
    }
    Harness h;
    REQUIRE_FALSE(h.service.restore(checkpoint, receipts));
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{DownloadCheckpoint{}});
    REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{.status = DownloadRecoveryStatus::ReceiptConflict});
    REQUIRE(h.service.restore(saved(), std::vector<DownloadImportReceipt>{{"old-1", candidate()}}));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Imported);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Strict recovery validates the entire bounded history before taking ownership",
          "[download][checkpoint][reconciliation]") {
    auto checkpoint = saved();
    std::vector<DownloadImportReceipt> receipts{{"old-1", candidate()}};
    SECTION("invalid checkpoint") { checkpoint.receipts[0].fingerprint = "invalid"; }
    SECTION("duplicate receipt IDs") { receipts.push_back(receipts[0]); }
    SECTION("invalid receipt ID") { receipts[0].job_id = "bad/id"; }
    SECTION("relative receipt path") { receipts[0].source_path = "relative.mp3"; }
    SECTION("non-normal receipt path") { receipts[0].source_path = candidate().parent_path() / "x/../final.mp3"; }
    SECTION("wrong receipt suffix") { receipts[0].source_path = candidate().parent_path() / "final.mp4"; }
    SECTION("too many receipts") { receipts.resize(maximum_download_import_receipts + 1, receipts[0]); }
    SECTION("invalid native receipt text") {
        auto native = candidate().native();
        native.insert(native.begin() + 3, 0);
        receipts[0].source_path = std::filesystem::path{native};
    }
    SECTION("oversized receipt representation") {
        receipts[0].source_path =
            candidate().parent_path() / (std::string(maximum_download_import_receipt_bytes, 'x') + ".mp3");
    }
    SECTION("invalid prospective checkpoint candidate") {
        receipts[0].source_path = candidate().parent_path() / (std::string(30'001, 'x') + ".mp3");
    }
    Harness h;
    REQUIRE_FALSE(h.service.restore(checkpoint, receipts));
    REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{.status = DownloadRecoveryStatus::InvalidHistory});
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.restore(saved(), {}));
    REQUIRE(h.service.recoveryReport().status == DownloadRecoveryStatus::Ready);
}

TEST_CASE("Strict recovery reserves the fresh namespace against session-only IDs",
          "[download][checkpoint][reconciliation]") {
    const auto id = GENERATE(std::string{"new-1"}, std::string{"new-999"}, std::string{"new-extra"});
    Harness h;
    REQUIRE_FALSE(h.service.restore({}, std::vector<DownloadImportReceipt>{{id, candidate()}}));
    REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{.status = DownloadRecoveryStatus::NamespaceConflict});
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.restore({}, std::vector<DownloadImportReceipt>{{"older-1", candidate()}}));
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"new-1"});
}

TEST_CASE("Strict recovery reports session-only receipts without creating jobs or consuming download quotas",
          "[download][checkpoint][reconciliation]") {
    std::vector<DownloadImportReceipt> receipts;
    std::vector<std::string> ids;
    for (std::size_t index = 0; index < maximum_download_import_receipts; ++index) {
        ids.push_back("orphan-" + std::to_string(index));
        receipts.push_back({ids.back(), candidate()});
    }
    Harness h({.max_jobs = 1, .max_requests = 1});
    REQUIRE(h.service.restore({}, receipts));
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{DownloadCheckpoint{}});
    REQUIRE(h.service.recoveryReport() ==
            DownloadRecoveryReport{.status = DownloadRecoveryStatus::Ready, .session_only_job_ids = ids});
    auto detached_report = h.service.recoveryReport();
    detached_report.session_only_job_ids.clear();
    REQUIRE(h.service.recoveryReport().session_only_job_ids == ids);
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"new-1"});
    REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::LimitExceeded);
    const auto before = h.service.recoveryReport();
    REQUIRE_FALSE(h.service.restore(saved(), {}));
    REQUIRE(h.service.recoveryReport() == before);
}

TEST_CASE("Imported historical checkpoints preserve partial progress and require private final evidence",
          "[download][checkpoint][reconciliation]") {
    auto checkpoint = saved(DownloadJobState::Imported);
    checkpoint.jobs[0].completed_mp3 = candidate();
    checkpoint.jobs[0].completed_units = 2;
    checkpoint.jobs[0].total_units = 7;
    REQUIRE(validDownloadCheckpoint(checkpoint));
    SECTION("no path") { checkpoint.jobs[0].completed_mp3.clear(); }
    SECTION("error") { checkpoint.jobs[0].error = DownloadFailure::Import; }
    SECTION("contradictory progress") { checkpoint.jobs[0].completed_units = 8; }
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
}

TEST_CASE("Pending download imports restore only as private interrupted candidates",
          "[download][checkpoint][import-pending]") {
    auto checkpoint = saved(DownloadJobState::Importing);
    checkpoint.jobs[0].completed_mp3 = candidate();
    checkpoint.jobs[0].completed_units = 1;
    checkpoint.jobs[0].total_units = 1;
    REQUIRE(validDownloadCheckpoint(checkpoint));
    Harness h;
    REQUIRE(h.service.restore(checkpoint));
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    REQUIRE(h.service.snapshot()[0].error == DownloadFailure::Unavailable);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE_FALSE(h.service.retryImport("old-1"));
    h.service.tick(0);
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.service.checkpoint()->jobs[0].completed_mp3 == candidate());
    checkpoint.jobs[0].completed_mp3.clear();
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
    checkpoint.jobs[0].completed_mp3 = candidate();
    checkpoint.jobs[0].error = DownloadFailure::Import;
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
    checkpoint.jobs[0].error = DownloadFailure::None;
    checkpoint.jobs[0].completed_units = 0;
    REQUIRE_FALSE(validDownloadCheckpoint(checkpoint));
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

namespace {
std::string recoveryPrefix(const std::string_view original, const unsigned int ordinal) {
    auto digits = std::to_string(ordinal);
    digits.insert(0, 3 - digits.size(), '0');
    return std::string{original.substr(0, 35)} + "-r" + digits;
}
DownloadCheckpoint namespaceHistory(const std::vector<std::string>& ids) {
    DownloadCheckpoint result;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto request = input(static_cast<unsigned int>(index / 20 + 1));
        if (index % 20 == 0)
            result.receipts.push_back({.request_id = request.request_id,
                                       .fingerprint = std::string(64, 'a'),
                                       .retained_request_bytes = request.request_id.size()});
        result.receipts.back().job_ids.push_back(ids[index]);
        result.receipts.back().retained_request_bytes += request.items[0].url.size() + request.items[0].title.size();
        result.jobs.push_back({.job_id = ids[index],
                               .title = request.items[0].title,
                               .state = DownloadJobState::Interrupted,
                               .error = DownloadFailure::Unavailable});
    }
    return result;
}
} // namespace

TEST_CASE("Fresh namespace recovery changes only future IDs and preserves historical replay",
          "[download][checkpoint][namespace-recovery]") {
    const bool reconcile = GENERATE(false, true);
    auto checkpoint = saved(DownloadJobState::Completed);
    checkpoint.jobs[0].completed_mp3 = candidate();
    checkpoint.jobs[0].completed_units = 7;
    checkpoint.jobs[0].total_units = 7;
    const std::vector<DownloadImportReceipt> receipts{{"old-1", candidate()}};
    Harness h({}, "old");
    REQUIRE_FALSE((reconcile ? h.service.restore(checkpoint, receipts) : h.service.restore(checkpoint)));
    REQUIRE(h.service.recoveryReport().status == DownloadRecoveryStatus::NamespaceConflict);
    REQUIRE((reconcile ? h.service.restoreWithFreshNamespace(checkpoint, receipts)
                       : h.service.restoreWithFreshNamespace(checkpoint)));
    REQUIRE(h.identity.calls == 0);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.execution.polls == 0);
    REQUIRE(h.execution.cancels == 0);
    REQUIRE(h.importer.calls == 0);
    const auto restored = h.service.checkpoint();
    REQUIRE(restored);
    REQUIRE(restored->receipts == checkpoint.receipts);
    REQUIRE(restored->jobs[0].job_id == "old-1");
    REQUIRE(restored->jobs[0].completed_mp3 == candidate());
    REQUIRE(restored->jobs[0].completed_units == 7);
    REQUIRE(restored->jobs[0].total_units == 7);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    REQUIRE(h.service.snapshot()[0].state == (reconcile ? DownloadJobState::Imported : DownloadJobState::Completed));
    REQUIRE(h.service.recoveryReport().status ==
            (reconcile ? DownloadRecoveryStatus::Ready : DownloadRecoveryStatus::NotRequested));
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-1"});
    h.identity.result = std::string(64, 'b');
    REQUIRE(h.service.submit(input()).error == DownloadSubmissionError::InvalidRequest);
    h.identity.result = std::string(64, 'a');
    REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-r001-1"});
    REQUIRE(h.service.checkpoint()->receipts[0] == checkpoint.receipts[0]);
    REQUIRE(h.service.snapshot()[0].job_id == "old-1");
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery keeps an already compatible namespace unchanged",
          "[download][checkpoint][namespace-recovery]") {
    const bool reconcile = GENERATE(false, true);
    Harness h;
    const auto checkpoint = saved();
    REQUIRE((reconcile ? h.service.restoreWithFreshNamespace(checkpoint, {})
                       : h.service.restoreWithFreshNamespace(checkpoint)));
    REQUIRE(h.service.checkpoint()->receipts == checkpoint.receipts);
    REQUIRE(h.service.snapshot()[0].job_id == "old-1");
    REQUIRE(h.identity.calls == 0);
    REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"new-1"});
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery skips colliding history and session-only namespace families",
          "[download][checkpoint][namespace-recovery]") {
    const auto checkpoint = namespaceHistory({"old-1", "old-r001-other", "old-r002-999"});
    const std::vector<DownloadImportReceipt> receipts{{"old-r003-7", candidate()}, {"unrelated-1", candidate()}};
    REQUIRE(validDownloadCheckpoint(checkpoint));
    Harness h({}, "old");
    REQUIRE(h.service.restoreWithFreshNamespace(checkpoint, receipts));
    REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{checkpoint});
    REQUIRE(h.service.recoveryReport().session_only_job_ids == std::vector<std::string>{"old-r003-7", "unrelated-1"});
    REQUIRE(h.identity.calls == 0);
    REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-r004-1"});
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery reserves orphan receipts without consuming download capacity",
          "[download][checkpoint][namespace-recovery]") {
    const std::vector<DownloadImportReceipt> receipts{
        {"old-99", candidate()}, {"old-r001-1", candidate()}, {"old-r002-extra", candidate()}};
    Harness h({.max_jobs = 1, .max_requests = 1}, "old");
    REQUIRE(h.service.restoreWithFreshNamespace({}, receipts));
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{DownloadCheckpoint{}});
    REQUIRE(h.service.recoveryReport().session_only_job_ids ==
            std::vector<std::string>{"old-99", "old-r001-1", "old-r002-extra"});
    REQUIRE(h.identity.calls == 0);
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-r003-1"});
    REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery preserves inclusive restored admission budgets",
          "[download][checkpoint][namespace-recovery]") {
    const auto budget = GENERATE(0, 1, 2);
    const auto checkpoint = saved();
    DownloadServiceLimits limits;
    if (budget == 0)
        limits.max_jobs = 1;
    else if (budget == 1)
        limits.max_requests = 1;
    else
        limits.max_retained_request_bytes = checkpoint.receipts[0].retained_request_bytes;
    Harness h(limits, "old");
    REQUIRE(h.service.restoreWithFreshNamespace(checkpoint));
    REQUIRE(h.service.checkpoint()->receipts == checkpoint.receipts);
    REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.service.submit(input()).job_ids == std::vector<std::string>{"old-1"});
    REQUIRE(h.service.snapshot().size() == 1);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery handles a forty-character prefix without renaming historical IDs",
          "[download][checkpoint][namespace-recovery]") {
    const std::string prefix = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN";
    REQUIRE(prefix.size() == 40);
    auto checkpoint = saved();
    checkpoint.jobs[0].job_id = prefix + "-1";
    checkpoint.receipts[0].job_ids = {checkpoint.jobs[0].job_id};
    REQUIRE(validDownloadCheckpoint(checkpoint));
    Harness h({}, prefix);
    REQUIRE(h.service.restoreWithFreshNamespace(checkpoint));
    REQUIRE(h.service.snapshot()[0].job_id == prefix + "-1");
    REQUIRE(h.service.checkpoint()->receipts == checkpoint.receipts);
    const auto result = h.service.submit(input(2));
    REQUIRE(result.error == DownloadSubmissionError::None);
    REQUIRE(result.job_ids == std::vector<std::string>{prefix.substr(0, 35) + "-r001-1"});
    REQUIRE(result.job_ids[0].size() <= 64);
}

TEST_CASE("Fresh namespace recovery searches the bounded full history and ledger without eviction",
          "[download][checkpoint][namespace-recovery]") {
    std::vector<std::string> ids;
    std::vector<DownloadImportReceipt> receipts;
    for (unsigned int index = 1; index <= download_checkpoint_max_jobs; ++index)
        ids.push_back(recoveryPrefix("old", index) + "-1");
    for (unsigned int index = 1; index <= maximum_download_import_receipts; ++index)
        receipts.push_back(
            {recoveryPrefix("old", static_cast<unsigned int>(download_checkpoint_max_jobs) + index) + "-1",
             candidate()});
    const auto checkpoint = namespaceHistory(ids);
    REQUIRE(validDownloadCheckpoint(checkpoint));
    REQUIRE(isValidDownloadImportReceipts(receipts));
    Harness h({}, "old");
    REQUIRE(h.service.restoreWithFreshNamespace(checkpoint, receipts));
    REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{checkpoint});
    REQUIRE(h.service.snapshot().size() == download_checkpoint_max_jobs);
    REQUIRE(h.service.recoveryReport().session_only_job_ids.size() == maximum_download_import_receipts);
    REQUIRE(h.service.submit(input(8)).error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.identity.calls == 0);
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.execution.polls == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery rolls back the prefix when later reconciliation fails",
          "[download][checkpoint][namespace-recovery]") {
    const auto failure = GENERATE(0, 1, 2);
    auto checkpoint = saved(DownloadJobState::Completed);
    checkpoint.jobs[0].completed_mp3 = candidate();
    std::vector<DownloadImportReceipt> receipts{{"old-1", candidate()}};
    DownloadServiceLimits limits;
    DownloadRecoveryStatus expected = DownloadRecoveryStatus::InvalidHistory;
    if (failure == 0) {
        receipts[0].source_path = candidate().parent_path() / "different.mp3";
        expected = DownloadRecoveryStatus::ReceiptConflict;
    } else if (failure == 1) {
        checkpoint = saved();
        receipts[0].source_path = candidate().parent_path() / (std::string(30'001, 'x') + ".mp3");
    } else {
        limits.max_retained_request_bytes = checkpoint.receipts[0].retained_request_bytes;
        ++checkpoint.receipts[0].retained_request_bytes;
    }
    REQUIRE(validDownloadCheckpoint(checkpoint));
    REQUIRE(isValidDownloadImportReceipts(receipts));
    Harness h(limits, "old");
    REQUIRE_FALSE(h.service.restoreWithFreshNamespace(checkpoint, receipts));
    REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{.status = expected});
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{DownloadCheckpoint{}});
    REQUIRE(h.identity.calls == 0);
    // A failed transaction neither reserves the proposed namespace nor IDs.
    REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-1"});
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Fresh namespace recovery refuses owners that are no longer pristine",
          "[download][checkpoint][namespace-recovery]") {
    const bool reconcile = GENERATE(false, true);
    Harness h({}, "old");
    const auto restore = [&] {
        return reconcile ? h.service.restoreWithFreshNamespace(saved(), {})
                         : h.service.restoreWithFreshNamespace(saved());
    };
    SECTION("already admitted") {
        REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-1"});
        const auto before = h.service.checkpoint();
        REQUIRE_FALSE(restore());
        REQUIRE(h.service.checkpoint() == before);
        REQUIRE(h.service.submit(input(3)).job_ids == std::vector<std::string>{"old-2"});
    }
    SECTION("already restored empty history") {
        REQUIRE(h.service.restore({}));
        REQUIRE_FALSE(restore());
        REQUIRE(h.service.checkpoint() == std::optional<DownloadCheckpoint>{DownloadCheckpoint{}});
        REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-1"});
    }
    SECTION("already restored a renewed namespace") {
        REQUIRE(restore());
        const auto before = h.service.checkpoint();
        REQUIRE_FALSE(restore());
        REQUIRE(h.service.checkpoint() == before);
        REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-r001-1"});
    }
    SECTION("closed") {
        h.service.shutdown();
        REQUIRE_FALSE(restore());
        REQUIRE(h.service.stopped());
        REQUIRE(h.service.snapshot().empty());
        REQUIRE(h.service.submit(input(2)).error == DownloadSubmissionError::NotAvailable);
    }
    SECTION("identity callback reentrancy") {
        h.identity.callback = [&] {
            REQUIRE_FALSE(restore());
            REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{});
            REQUIRE(h.service.snapshot().empty());
        };
        REQUIRE(h.service.submit(input(2)).job_ids == std::vector<std::string>{"old-1"});
        h.identity.callback = {};
        REQUIRE(h.service.submit(input(3)).job_ids == std::vector<std::string>{"old-2"});
    }
    REQUIRE(h.execution.starts == 0);
    REQUIRE(h.execution.polls == 0);
    REQUIRE(h.execution.cancels == 0);
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Legacy restore exposes typed refusal without changing its refusal or success semantics",
          "[download][checkpoint][namespace-recovery]") {
    auto checkpoint = saved();
    SECTION("namespace refusal") {
        Harness h({}, "old");
        REQUIRE_FALSE(h.service.restore(checkpoint));
        REQUIRE(h.service.recoveryReport() ==
                DownloadRecoveryReport{.status = DownloadRecoveryStatus::NamespaceConflict});
        REQUIRE(h.service.snapshot().empty());
    }
    SECTION("invalid history refusal") {
        Harness h;
        checkpoint.receipts[0].fingerprint = "invalid";
        REQUIRE_FALSE(h.service.restore(checkpoint));
        REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{.status = DownloadRecoveryStatus::InvalidHistory});
        REQUIRE(h.service.restore(saved()));
        REQUIRE(h.service.recoveryReport() == DownloadRecoveryReport{});
    }
}

} // namespace music_player::m0::test
