#include "m0/application/DownloadAdmissionController.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

namespace music_player::m0::test {
namespace {

DownloadRequest admissionInput(const unsigned int number = 1, const std::size_t count = 1) {
    auto tail = std::to_string(number);
    tail.insert(0, 12 - tail.size(), '0');
    DownloadRequest request{.request_id = "abcdefab-cdef-4abc-8def-" + tail};
    for (std::size_t i = 0; i < count; ++i)
        request.items.push_back({.url = "https://synthetic.example/" + std::to_string(i) + ".mp4?token=private",
                                 .title = "Selected synthetic media " + std::to_string(i),
                                 .kind = DownloadMediaKind::Video});
    return request;
}

struct AdmissionExecution final : DownloadExecution {
    std::vector<std::string> starts{};
    std::vector<std::string> polls{};
    std::vector<std::string> cancellations{};
    DownloadExecutionUpdate update{};
    std::function<void()> on_cancel{};
    bool start(const std::string& id, const DownloadItem&) override {
        starts.push_back(id);
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view id) override {
        cancellations.emplace_back(id);
        if (on_cancel)
            on_cancel();
    }
    DownloadExecutionUpdate poll(std::string_view id) override {
        polls.emplace_back(id);
        return update;
    }
};

struct AdmissionImport final : DownloadImport {
    unsigned int calls{0};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};

struct AdmissionIdentity final : DownloadRequestIdentity {
    std::string value = std::string(64, 'a');
    std::string fingerprint(const DownloadRequest&) override { return value; }
};

struct AdmissionIo final : DownloadCheckpointIo {
    struct Save final {
        std::uint64_t id;
        DownloadCheckpoint checkpoint;
    };
    std::vector<std::uint64_t> loads{};
    std::vector<Save> saves{};
    std::vector<std::uint64_t> polls{};
    std::optional<std::uint64_t> active{};
    std::optional<DownloadCheckpointIoResult> completion{};
    DownloadCheckpointIoCommand load_command{DownloadCheckpointIoCommand::Accepted};
    DownloadCheckpointIoCommand save_command{DownloadCheckpointIoCommand::Accepted};
    bool throw_load{false};
    bool throw_save{false};
    bool allow_stopped{false};
    bool deliver_malformed{false};
    unsigned int shutdowns{0};
    std::function<void()> on_load{};
    std::function<void()> on_save{};
    std::function<void()> on_poll{};

    DownloadCheckpointIoCommand startLoad(const std::uint64_t id) override {
        loads.push_back(id);
        if (on_load)
            on_load();
        if (throw_load)
            throw std::runtime_error{"private load failure"};
        if (load_command != DownloadCheckpointIoCommand::Accepted)
            return load_command;
        if (active)
            return DownloadCheckpointIoCommand::Busy;
        active = id;
        return DownloadCheckpointIoCommand::Accepted;
    }
    DownloadCheckpointIoCommand startSave(const std::uint64_t id, const DownloadCheckpoint& checkpoint) override {
        saves.push_back({id, checkpoint});
        if (on_save)
            on_save();
        if (throw_save)
            throw std::runtime_error{"private save failure"};
        if (save_command != DownloadCheckpointIoCommand::Accepted)
            return save_command;
        if (active)
            return DownloadCheckpointIoCommand::Busy;
        active = id;
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(const std::uint64_t id) override {
        polls.push_back(id);
        if (on_poll)
            on_poll();
        if (!completion || (!deliver_malformed && completion->operation_id != id))
            return std::nullopt;
        auto result = std::move(completion);
        completion.reset();
        active.reset();
        return result;
    }
    void shutdown() override { ++shutdowns; }
    bool stopped() const override { return shutdowns != 0 && allow_stopped; }
    void loaded(DownloadCheckpointLoadResult result) {
        REQUIRE(!loads.empty());
        completion = DownloadCheckpointIoResult{
            .operation_id = loads.back(), .operation = DownloadCheckpointOperation::Load, .load = std::move(result)};
    }
    void saved(const DownloadCheckpointSaveStatus status = DownloadCheckpointSaveStatus::Saved) {
        REQUIRE(!saves.empty());
        completion = DownloadCheckpointIoResult{
            .operation_id = saves.back().id, .operation = DownloadCheckpointOperation::Save, .save_status = status};
    }
};

struct AdmissionHarness {
    AdmissionExecution execution{};
    AdmissionImport importer{};
    AdmissionIdentity identity{};
    AdmissionIo io{};
    DownloadService service;
    DownloadAdmissionController controller;
    explicit AdmissionHarness(DownloadServiceLimits limits = {})
        : service(execution, importer, "owner", limits, &identity), controller(service, io) {}
    void ready() {
        REQUIRE(controller.start());
        REQUIRE(io.loads.size() == 1);
        io.loaded({.status = DownloadCheckpointLoadStatus::Missing});
        controller.tick(0);
        REQUIRE(controller.state() == DownloadAdmissionControllerState::Ready);
    }
};

void pendingWithoutAck(const DownloadAdmissionResult& result) {
    REQUIRE(result.state == DownloadAdmissionState::Pending);
    REQUIRE(result.result.error == DownloadSubmissionError::NotAvailable);
    REQUIRE(result.result.job_ids.empty());
}

void notAccepted(const std::optional<DownloadSubmissionResult>& result) {
    REQUIRE_FALSE((result && result->error == DownloadSubmissionError::None));
    if (result)
        REQUIRE(result->job_ids.empty());
}

DownloadCheckpoint recoveredAdmission() {
    const auto request = admissionInput();
    return {.receipts = {{.request_id = request.request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"old-1"},
                          .retained_request_bytes =
                              request.request_id.size() + request.items[0].url.size() + request.items[0].title.size()}},
            .jobs = {{.job_id = "old-1", .title = request.items[0].title}}};
}

} // namespace

TEST_CASE("Download owner starts with an asynchronous load and never writes missing defaults",
          "[download][admission][FR-051]") {
    AdmissionHarness h;
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::NotStarted);
    REQUIRE_FALSE(h.controller.loadStatus());
    REQUIRE_FALSE(h.controller.saveStatus());
    const auto before = h.controller.begin(admissionInput());
    REQUIRE(before.state == DownloadAdmissionState::Completed);
    REQUIRE(before.result.error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.controller.start());
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Loading);
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.loads[0] != 0);
    REQUIRE_FALSE(h.controller.start());
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.execution.starts.empty());
    h.io.loaded({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Loading);
    h.controller.tick(0);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.controller.loadStatus() == DownloadCheckpointLoadStatus::Missing);
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Download owner blocks invalid future or unreadable startup without overwriting evidence",
          "[download][admission]") {
    const auto status = GENERATE(DownloadCheckpointLoadStatus::Invalid, DownloadCheckpointLoadStatus::Unreadable,
                                 DownloadCheckpointLoadStatus::FutureVersion);
    AdmissionHarness h;
    REQUIRE(h.controller.start());
    h.io.loaded({.status = status});
    h.controller.tick(0);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.loadStatus() == status);
    const auto rejected = h.controller.begin(admissionInput());
    REQUIRE(rejected.state == DownloadAdmissionState::Completed);
    REQUIRE(rejected.result.error == DownloadSubmissionError::NotAvailable);
    REQUIRE(rejected.result.job_ids.empty());
    h.controller.tick(1);
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.controller.retryStartup());
    REQUIRE(h.io.loads.size() == 2);
    REQUIRE(h.io.loads[1] > h.io.loads[0]);
    h.io.loaded({.status = DownloadCheckpointLoadStatus::Missing});
    h.controller.tick(2);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Download owner refuses a contradictory or invalid loaded checkpoint atomically", "[download][admission]") {
    AdmissionHarness h;
    REQUIRE(h.controller.start());
    DownloadCheckpointLoadResult result{.status = DownloadCheckpointLoadStatus::Loaded};
    SECTION("missing checkpoint") {}
    SECTION("invalid checkpoint") {
        result.checkpoint = recoveredAdmission();
        result.checkpoint->receipts[0].job_ids[0] = "missing";
    }
    h.io.loaded(std::move(result));
    h.controller.tick(0);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Download owner restores durable receipts without restarting their old work",
          "[download][admission][FR-053]") {
    AdmissionHarness h;
    const auto checkpoint = recoveredAdmission();
    REQUIRE(h.controller.start());
    h.io.loaded({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
    h.controller.tick(0);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    const auto replay = h.controller.begin(admissionInput());
    REQUIRE(replay.state == DownloadAdmissionState::Completed);
    REQUIRE(replay.result.error == DownloadSubmissionError::None);
    REQUIRE(replay.result.job_ids == std::vector<std::string>{"old-1"});
    h.controller.tick(1);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Download owner publishes no admission receipt before its matching Saved completion",
          "[download][admission][FR-051]") {
    AdmissionHarness h;
    h.ready();
    const auto request = admissionInput(1, 2);
    pendingWithoutAck(h.controller.begin(request));
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::AdmissionPending);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.io.saves[0].id > h.io.loads[0]);
    REQUIRE(h.io.saves[0].checkpoint.receipts[0].job_ids == std::vector<std::string>{"owner-1", "owner-2"});
    const auto polls = h.io.polls.size();
    REQUIRE_FALSE(h.controller.poll(request.request_id));
    REQUIRE_FALSE(h.controller.poll(admissionInput(2).request_id));
    REQUIRE(h.io.polls.size() == polls);
    h.controller.tick(1);
    REQUIRE(h.execution.starts.empty());
    h.io.saved();
    REQUIRE_FALSE(h.controller.poll(request.request_id));
    h.controller.tick(2);
    const auto receipt = h.controller.poll(request.request_id);
    REQUIRE(receipt);
    REQUIRE(receipt->error == DownloadSubmissionError::None);
    REQUIRE(receipt->job_ids == std::vector<std::string>{"owner-1", "owner-2"});
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Saved);
    const auto replay = h.controller.begin(request);
    REQUIRE(replay.state == DownloadAdmissionState::Completed);
    REQUIRE(replay.result.job_ids == receipt->job_ids);
    REQUIRE(h.service.snapshot().size() == 2);
    REQUIRE(h.io.saves.size() == 1);
}

TEST_CASE("Download owner deduplicates pending UUID casing and refuses conflicts or another batch",
          "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    const auto request = admissionInput();
    pendingWithoutAck(h.controller.begin(request));
    auto uppercase = request;
    std::transform(uppercase.request_id.begin(), uppercase.request_id.end(), uppercase.request_id.begin(),
                   [](const char c) { return c >= 'a' && c <= 'f' ? static_cast<char>(c - 'a' + 'A') : c; });
    pendingWithoutAck(h.controller.begin(uppercase));
    auto conflict = request;
    conflict.items[0].url += "changed";
    const auto conflicting = h.controller.begin(conflict);
    REQUIRE(conflicting.state == DownloadAdmissionState::Completed);
    REQUIRE(conflicting.result.error == DownloadSubmissionError::InvalidRequest);
    const auto other = h.controller.begin(admissionInput(2));
    REQUIRE(other.state == DownloadAdmissionState::Completed);
    REQUIRE(other.result.error == DownloadSubmissionError::Busy);
    REQUIRE(other.result.job_ids.empty());
    REQUIRE(h.service.snapshot().size() == 1);
    REQUIRE(h.io.saves.size() == 1);
    h.io.saved();
    h.controller.tick(1);
    const auto receipt = h.controller.poll(uppercase.request_id);
    REQUIRE(receipt);
    REQUIRE(receipt->job_ids == std::vector<std::string>{"owner-1"});
}

TEST_CASE("Download owner ignores stale operation IDs and contradictory completion kinds", "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    const auto request = admissionInput();
    pendingWithoutAck(h.controller.begin(request));
    h.io.saved();
    SECTION("wrong operation ID") { h.io.completion->operation_id += 100; }
    SECTION("wrong operation kind") { h.io.completion->operation = DownloadCheckpointOperation::Load; }
    h.io.deliver_malformed = true;
    h.controller.tick(1);
    notAccepted(h.controller.poll(request.request_id));
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.service.snapshot().size() == 1);
    REQUIRE(h.io.saves.size() == 1);
}

TEST_CASE("Failed admission persistence retains its stage and only explicit retry can confirm it",
          "[download][admission]") {
    const auto status =
        GENERATE(DownloadCheckpointSaveStatus::Failed, DownloadCheckpointSaveStatus::BlockedByExistingData);
    AdmissionHarness h({.max_jobs = 1, .max_requests = 1});
    h.ready();
    const auto request = admissionInput();
    pendingWithoutAck(h.controller.begin(request));
    const auto original = h.io.saves[0];
    REQUIRE_FALSE(h.controller.retryAdmission());
    h.io.saved(status);
    h.controller.tick(1);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::AdmissionFailed);
    REQUIRE(h.controller.saveStatus() == status);
    notAccepted(h.controller.poll(request.request_id));
    h.controller.tick(2);
    h.controller.tick(3);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.service.snapshot().size() == 1);
    REQUIRE(h.service.submit(request).error == DownloadSubmissionError::Busy);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.controller.retryAdmission());
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].id > original.id);
    REQUIRE(h.io.saves[1].checkpoint == original.checkpoint);
    h.io.saved();
    h.controller.tick(4);
    const auto receipt = h.controller.poll(request.request_id);
    REQUIRE(receipt);
    REQUIRE(receipt->error == DownloadSubmissionError::None);
    REQUIRE(receipt->job_ids == original.checkpoint.receipts[0].job_ids);
    const auto full = h.controller.begin(admissionInput(2));
    REQUIRE(full.result.error == DownloadSubmissionError::LimitExceeded);
    REQUIRE(h.service.snapshot().size() == 1);
}

TEST_CASE("Refused or throwing checkpoint enqueue cannot manufacture an admission ACK", "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    SECTION("busy") { h.io.save_command = DownloadCheckpointIoCommand::Busy; }
    SECTION("closed") { h.io.save_command = DownloadCheckpointIoCommand::Closed; }
    SECTION("invalid request") { h.io.save_command = DownloadCheckpointIoCommand::InvalidRequest; }
    SECTION("exception") { h.io.throw_save = true; }
    const auto request = admissionInput();
    const auto attempted = h.controller.begin(request);
    REQUIRE_FALSE((attempted.state == DownloadAdmissionState::Completed &&
                   attempted.result.error == DownloadSubmissionError::None));
    REQUIRE(attempted.result.job_ids.empty());
    notAccepted(h.controller.poll(request.request_id));
    REQUIRE(h.service.snapshot().size() == 1);
    h.controller.tick(1);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.execution.starts.empty());
    h.io.throw_save = false;
    h.io.save_command = DownloadCheckpointIoCommand::Accepted;
    REQUIRE(h.controller.retryAdmission());
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].checkpoint.receipts[0].job_ids == std::vector<std::string>{"owner-1"});
    h.io.saved();
    h.controller.tick(2);
    const auto receipt = h.controller.poll(request.request_id);
    REQUIRE(receipt);
    REQUIRE(receipt->job_ids == std::vector<std::string>{"owner-1"});
}

TEST_CASE("Active download polling and cancellation continue while a new admission waits for disk",
          "[download][admission][FR-053]") {
    const bool timeout = GENERATE(false, true);
    AdmissionHarness h({.job_timeout_ms = 10});
    h.ready();
    const auto first = admissionInput();
    pendingWithoutAck(h.controller.begin(first));
    h.io.saved();
    h.controller.tick(0);
    h.controller.tick(0);
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    pendingWithoutAck(h.controller.begin(admissionInput(2)));
    if (!timeout)
        REQUIRE(h.service.cancel("owner-1")); // Same-thread trusted job control, not new admission.
    h.controller.tick(timeout ? 10 : 1);
    REQUIRE(h.execution.cancellations == std::vector<std::string>{"owner-1"});
    REQUIRE_FALSE(h.execution.polls.empty());
    h.execution.update = {.job_id = "owner-1", .state = DownloadExecutionState::Cancelled};
    h.controller.tick(timeout ? 11 : 2);
    h.controller.tick(timeout ? 12 : 3);
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    REQUIRE(h.service.snapshot()[0].state == (timeout ? DownloadJobState::Failed : DownloadJobState::Cancelled));
    REQUIRE(h.service.snapshot()[0].error == (timeout ? DownloadFailure::Timeout : DownloadFailure::None));
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Queued);
    REQUIRE_FALSE(h.controller.poll(admissionInput(2).request_id));
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Admission port reentrancy cannot submit poll or tick inside checkpoint enqueue", "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    const auto request = admissionInput();
    bool observed = false;
    h.io.on_save = [&] {
        observed = true;
        REQUIRE(h.controller.begin(admissionInput(2)).result.error == DownloadSubmissionError::Busy);
        const auto before = h.io.polls.size();
        REQUIRE_FALSE(h.controller.poll(request.request_id));
        h.controller.tick(1);
        REQUIRE(h.io.polls.size() == before);
        REQUIRE(h.execution.starts.empty());
    };
    pendingWithoutAck(h.controller.begin(request));
    REQUIRE(observed);
    REQUIRE(h.service.snapshot().size() == 1);
    h.io.on_save = {};
    h.io.saved();
    h.controller.tick(2);
    const auto receipt = h.controller.poll(request.request_id);
    REQUIRE(receipt);
    REQUIRE(receipt->error == DownloadSubmissionError::None);
}

TEST_CASE("Reentrant shutdown is deferred until checkpoint enqueue leaves its port call", "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    h.io.on_save = [&] {
        h.controller.shutdown();
        REQUIRE(h.io.shutdowns == 0);
        REQUIRE(h.io.saves.size() == 1);
    };
    const auto result = h.controller.begin(admissionInput());
    REQUIRE_FALSE(
        (result.state == DownloadAdmissionState::Completed && result.result.error == DownloadSubmissionError::None));
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE_FALSE(h.controller.stopped());
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.io.shutdowns == 0);
}

TEST_CASE("Download shutdown drains pending persistence and saves its cancelled final state before stopping IO",
          "[download][admission][FR-053]") {
    AdmissionHarness h;
    h.ready();
    pendingWithoutAck(h.controller.begin(admissionInput()));
    h.controller.shutdown();
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE(h.service.stopped());
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.io.shutdowns == 0);
    REQUIRE_FALSE(h.controller.stopped());
    REQUIRE(h.controller.begin(admissionInput(2)).result.error == DownloadSubmissionError::NotAvailable);
    h.io.saved();
    h.controller.tick(1);
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].id > h.io.saves[0].id);
    REQUIRE(h.io.saves[1].checkpoint.jobs[0].state == DownloadJobState::Cancelled);
    REQUIRE(h.io.shutdowns == 0);
    h.io.saved();
    h.controller.tick(2);
    REQUIRE(h.io.shutdowns == 1);
    REQUIRE_FALSE(h.controller.stopped());
    h.io.allow_stopped = true;
    h.controller.tick(3);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopped);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Download shutdown waits for the actual execution acknowledgement before its final checkpoint",
          "[download][admission][FR-053]") {
    AdmissionHarness h;
    h.ready();
    pendingWithoutAck(h.controller.begin(admissionInput()));
    h.io.saved();
    h.controller.tick(0);
    h.controller.tick(0);
    REQUIRE(h.execution.starts.size() == 1);
    h.controller.shutdown();
    REQUIRE(h.execution.cancellations == std::vector<std::string>{"owner-1"});
    REQUIRE_FALSE(h.service.stopped());
    h.controller.tick(1);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.io.shutdowns == 0);
    h.execution.update = {.job_id = "owner-1", .state = DownloadExecutionState::Cancelled};
    h.controller.tick(2);
    REQUIRE(h.service.stopped());
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].checkpoint.jobs[0].state == DownloadJobState::Cancelled);
    h.io.saved();
    h.controller.tick(3);
    REQUIRE(h.io.shutdowns == 1);
    REQUIRE_FALSE(h.controller.stopped());
}

TEST_CASE("Shutdown during trusted cancellation is applied after the service port call returns",
          "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    pendingWithoutAck(h.controller.begin(admissionInput(1, 2)));
    h.io.saved();
    h.controller.tick(0);
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    h.execution.on_cancel = [&] { h.controller.shutdown(); };
    REQUIRE(h.service.cancel("owner-1"));
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    h.execution.update = {.job_id = "owner-1", .state = DownloadExecutionState::Cancelled};
    h.controller.tick(1);
    REQUIRE(h.service.stopped());
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Cancelled);
    REQUIRE(h.io.saves.size() == 2);
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(2);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    REQUIRE(h.execution.cancellations == std::vector<std::string>{"owner-1"});
}

TEST_CASE("Shutdown during Saved observation prevents staged execution before final drain", "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    pendingWithoutAck(h.controller.begin(admissionInput()));
    h.io.saved();
    h.io.on_poll = [&] { h.controller.shutdown(); };
    h.controller.tick(1);
    h.io.on_poll = {};
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.service.stopped());
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelled);
    REQUIRE(h.io.saves.size() == 2);
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(2);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("A failed final checkpoint remains visible after physical download-owner shutdown", "[download][admission]") {
    AdmissionHarness h;
    h.ready();
    h.controller.shutdown();
    h.controller.tick(0);
    REQUIRE(h.io.saves.size() == 1);
    h.io.saved(DownloadCheckpointSaveStatus::Failed);
    h.controller.tick(1);
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
    REQUIRE(h.io.shutdowns == 1);
    REQUIRE_FALSE(h.controller.stopped());
    h.io.allow_stopped = true;
    h.controller.tick(2);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
    REQUIRE(h.io.saves.size() == 1);
}

TEST_CASE("Download owner destructor does not invoke borrowed IO or execution ports", "[download][admission]") {
    AdmissionExecution execution;
    AdmissionImport importer;
    AdmissionIdentity identity;
    AdmissionIo io;
    DownloadService service(execution, importer, "owner", {}, &identity);
    {
        DownloadAdmissionController controller(service, io);
        REQUIRE(controller.start());
    }
    REQUIRE(io.shutdowns == 0);
    REQUIRE(io.saves.empty());
    REQUIRE(execution.cancellations.empty());
}

} // namespace music_player::m0::test
