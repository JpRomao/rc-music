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

std::filesystem::path admissionFinalPath() {
#ifdef _WIN32
    return std::filesystem::path{L"C:/rc-music-synthetic/final.mp3"};
#else
    return std::filesystem::path{"/rc-music-synthetic/final.mp3"};
#endif
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
    bool pending{false};
    DownloadImportState polled{DownloadImportState::Pending};
    std::function<void()> on_poll{};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
    DownloadImportState begin(const std::string& job_id, const std::filesystem::path& path) override {
        if (!pending)
            return DownloadImport::begin(job_id, path);
        static_cast<void>(accept(job_id, path));
        return DownloadImportState::Pending;
    }
    DownloadImportState pollImport(std::string_view) override {
        if (on_poll)
            on_poll();
        return polled;
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
    bool throw_poll{false};
    bool throw_poll_after_take{false};
    bool throw_save_after_accept{false};
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
        if (throw_save_after_accept)
            throw std::runtime_error{"uncertain private save"};
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(const std::uint64_t id) override {
        polls.push_back(id);
        if (on_poll)
            on_poll();
        if (throw_poll)
            throw std::runtime_error{"uncertain private poll"};
        if (!completion || (!deliver_malformed && completion->operation_id != id))
            return std::nullopt;
        auto result = std::move(completion);
        completion.reset();
        active.reset();
        if (throw_poll_after_take)
            throw std::runtime_error{"uncertain consumed poll"};
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
    explicit AdmissionHarness(DownloadServiceLimits limits = {},
                              DownloadFinalSavePolicy policy = DownloadFinalSavePolicy::CloseAfterAttempt)
        : service(execution, importer, "owner", limits, &identity), controller(service, io, policy) {}
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

SessionLoadResult healthyAdmissionSession(const std::string& id = "prior-1") {
    return {.snapshot = {.download_import_receipts = {{.job_id = id, .source_path = admissionFinalPath()}}},
            .status = SessionLoadStatus::Loaded};
}
DownloadCheckpoint recoveryAdmissionCheckpoint(const DownloadJobState state = DownloadJobState::Queued) {
    const auto request = admissionInput();
    DownloadCheckpoint result{
        .receipts = {{.request_id = request.request_id,
                      .fingerprint = std::string(64, 'a'),
                      .job_ids = {"prior-1"},
                      .retained_request_bytes =
                          request.request_id.size() + request.items[0].url.size() + request.items[0].title.size()}},
        .jobs = {{.job_id = "prior-1", .title = request.items[0].title, .state = state}}};
    if (state == DownloadJobState::Completed || state == DownloadJobState::Importing ||
        state == DownloadJobState::ImportFailed) {
        result.jobs[0].completed_mp3 = admissionFinalPath();
        result.jobs[0].error =
            state == DownloadJobState::ImportFailed ? DownloadFailure::Import : DownloadFailure::None;
    } else if (state == DownloadJobState::Failed) {
        result.jobs[0].error = DownloadFailure::Network;
    }
    return result;
}
struct RecoveryAdmissionHarness {
    AdmissionExecution execution{};
    AdmissionImport importer{};
    AdmissionIdentity identity{};
    AdmissionIo io{};
    DownloadService service;
    DownloadAdmissionController controller;
    explicit RecoveryAdmissionHarness(const SessionLoadResult& session, std::string prefix = "owner",
                                      DownloadFinalSavePolicy policy = DownloadFinalSavePolicy::CloseAfterAttempt)
        : service(execution, importer, std::move(prefix), {}, &identity), controller(service, io, session, policy) {}
    void load(DownloadCheckpointLoadResult result) {
        REQUIRE(controller.start());
        io.loaded(std::move(result));
        controller.tick(0);
    }
};

} // namespace

TEST_CASE("Durable admission reconciles a saved session beyond a queued checkpoint without importing",
          "[download][recovery-controller][FR-051]") {
    RecoveryAdmissionHarness h{healthyAdmissionSession()};
    h.load({.checkpoint = recoveryAdmissionCheckpoint(), .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.controller.loadStatus() == DownloadCheckpointLoadStatus::Loaded);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::Ready);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Imported);
    REQUIRE(h.service.snapshot()[0].completed_mp3.empty());
    const auto replay = h.controller.begin(admissionInput());
    REQUIRE(replay.state == DownloadAdmissionState::Completed);
    REQUIRE(replay.result.error == DownloadSubmissionError::None);
    REQUIRE(h.controller.poll(admissionInput().request_id)->job_ids == std::vector<std::string>{"prior-1"});
    h.controller.tick(1000);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.controller.begin(admissionInput(2)).state == DownloadAdmissionState::Pending);
    h.io.saved();
    h.controller.tick(1001);
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Durable admission does not treat completed checkpoint history as an import receipt",
          "[download][recovery-controller][FR-053]") {
    const auto session_status = GENERATE(SessionLoadStatus::Missing, SessionLoadStatus::Loaded);
    RecoveryAdmissionHarness h{{.status = session_status}};
    h.load({.checkpoint = recoveryAdmissionCheckpoint(DownloadJobState::Completed),
            .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    REQUIRE(h.controller.recoveryReport().items[0].disposition == DownloadRecoveryDisposition::Unconfirmed);
    REQUIRE_FALSE(h.service.retryImport("prior-1"));
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Durable admission reserves orphan session IDs even when the download checkpoint is missing",
          "[download][recovery-controller][FR-053]") {
    RecoveryAdmissionHarness h{healthyAdmissionSession("owner-1")};
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.loadStatus() == DownloadCheckpointLoadStatus::Missing);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::NamespaceConflict);
    REQUIRE(h.controller.begin(admissionInput()).result.error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.io.saves.empty());
    h.controller.shutdown();
    h.io.allow_stopped = true;
    h.controller.tick(1);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Durable admission retains session-only receipts without synthesizing jobs or requests",
          "[download][recovery-controller][FR-051]") {
    RecoveryAdmissionHarness h{healthyAdmissionSession("orphan-1")};
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.controller.recoveryReport().session_only_job_ids == std::vector<std::string>{"orphan-1"});
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.service.checkpoint()->receipts.empty());
    REQUIRE(h.controller.poll(admissionInput().request_id) == std::nullopt);
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Durable admission blocks untrusted session history without repairing it by download retry",
          "[download][recovery-controller][FR-053]") {
    const auto status = GENERATE(SessionLoadStatus::RecoveredFromBackup, SessionLoadStatus::InvalidUsingDefaults,
                                 SessionLoadStatus::FutureVersion, static_cast<SessionLoadStatus>(999));
    auto session = healthyAdmissionSession();
    session.status = status;
    RecoveryAdmissionHarness h{session};
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.loadStatus() == DownloadCheckpointLoadStatus::Missing);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::InvalidHistory);
    REQUIRE(h.controller.begin(admissionInput()).result.error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::ReloadSession);
    const auto loads = h.io.loads;
    REQUIRE_FALSE(h.controller.retryStartup());
    REQUIRE(h.io.loads == loads);
    h.controller.tick(1);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.io.saves.empty());
    h.controller.shutdown();
    h.io.allow_stopped = true;
    h.controller.tick(2);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Durable admission rejects contradictory missing and malformed loaded session snapshots",
          "[download][recovery-controller][FR-053]") {
    auto session = healthyAdmissionSession();
    const auto missing = GENERATE(true, false);
    if (missing)
        session.status = SessionLoadStatus::Missing;
    else
        session.snapshot.download_import_receipts.push_back(session.snapshot.download_import_receipts[0]);
    RecoveryAdmissionHarness h{session};
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::InvalidHistory);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::ReloadSession);
    const auto loads = h.io.loads;
    REQUIRE_FALSE(h.controller.retryStartup());
    REQUIRE(h.io.loads == loads);
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.io.saves.empty());
}

TEST_CASE("Startup recovery action follows checkpoint retry availability and physical drain",
          "[download][recovery-controller][FR-053]") {
    RecoveryAdmissionHarness h{healthyAdmissionSession()};
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE(h.controller.start());
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE_FALSE(h.controller.retryStartup());
    REQUIRE(h.io.loads.size() == 1);
    h.io.loaded({.status = DownloadCheckpointLoadStatus::Unreadable});
    h.controller.tick(0);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::RetryCheckpoint);
    h.io.on_load = [&] {
        REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
        REQUIRE_FALSE(h.controller.retryStartup());
    };
    REQUIRE(h.controller.retryStartup());
    REQUIRE(h.io.loads.size() == 2);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    h.io.loaded({.status = DownloadCheckpointLoadStatus::Missing});
    h.controller.tick(1);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE_FALSE(h.controller.retryStartup());
    h.controller.shutdown();
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    h.controller.tick(2);
    h.io.saved();
    h.controller.tick(3);
    REQUIRE_FALSE(h.controller.stopped());
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE_FALSE(h.controller.retryStartup());
    h.io.allow_stopped = true;
    h.controller.tick(4);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Session reload recovery action retires on shutdown without a replacement write",
          "[download][recovery-controller][FR-053]") {
    RecoveryAdmissionHarness h{{.status = SessionLoadStatus::FutureVersion}};
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::ReloadSession);
    h.controller.shutdown();
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE_FALSE(h.controller.retryStartup());
    h.io.allow_stopped = true;
    h.controller.tick(1);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Durable admission preserves path and terminal conflicts instead of saving reconciled defaults",
          "[download][recovery-controller][FR-053]") {
    const auto state = GENERATE(DownloadJobState::Completed, DownloadJobState::Failed, DownloadJobState::Cancelled);
    auto checkpoint = recoveryAdmissionCheckpoint(state);
    if (state == DownloadJobState::Completed)
        checkpoint.jobs[0].completed_mp3 = admissionFinalPath().parent_path() / "conflict.mp3";
    const auto original = checkpoint;
    RecoveryAdmissionHarness h{healthyAdmissionSession()};
    h.load({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.loadStatus() == DownloadCheckpointLoadStatus::Loaded);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::ReceiptConflict);
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(checkpoint == original);
    h.controller.shutdown();
    h.io.allow_stopped = true;
    h.controller.tick(1);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Durable admission owns a bounded detached startup receipt snapshot",
          "[download][recovery-controller][FR-051]") {
    auto session = healthyAdmissionSession("orphan-1");
    RecoveryAdmissionHarness h{session};
    session.snapshot.download_import_receipts[0].job_id = "owner-1";
    session.snapshot.download_import_receipts.clear();
    session.status = SessionLoadStatus::FutureVersion;
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.controller.recoveryReport().session_only_job_ids == std::vector<std::string>{"orphan-1"});
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Shutdown during durable startup never restores or saves either history",
          "[download][recovery-controller][FR-053]") {
    RecoveryAdmissionHarness h{healthyAdmissionSession()};
    h.io.on_poll = [&] { h.controller.shutdown(); };
    h.load({.checkpoint = recoveryAdmissionCheckpoint(), .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::NotRequested);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.importer.calls == 0);
    h.io.allow_stopped = true;
    h.controller.tick(1);
    REQUIRE(h.controller.stopped());
}

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

TEST_CASE("Download owner final checkpoint waits for durable import acknowledgement",
          "[download][admission][import-pending]") {
    AdmissionHarness h;
    h.ready();
    h.importer.pending = true;
    pendingWithoutAck(h.controller.begin(admissionInput(1, 2)));
    h.io.saved();
    h.controller.tick(0);
    h.execution.update = {.job_id = "owner-1",
                          .state = DownloadExecutionState::Published,
                          .phase = DownloadPhase::Publishing,
                          .published_mp3 = admissionFinalPath()};
    h.controller.tick(1);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Importing);
    SECTION("external close") { h.controller.shutdown(); }
    SECTION("close during import observation") {
        h.importer.on_poll = [&] { h.controller.shutdown(); };
    }
    h.controller.tick(2);
    h.importer.on_poll = {};
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE_FALSE(h.service.stopped());
    REQUIRE(h.service.snapshot()[1].state == DownloadJobState::Cancelled);
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.io.saves.size() == 1);
    h.importer.polled = DownloadImportState::Committed;
    h.controller.tick(3);
    REQUIRE(h.service.stopped());
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].checkpoint.jobs[0].state == DownloadJobState::Completed);
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(4);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.importer.calls == 1);
    REQUIRE(h.execution.cancellations.empty());
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

TEST_CASE("Held download exit retains final save failures until an explicit successful retry",
          "[download][final-save-policy]") {
    const auto status =
        GENERATE(DownloadCheckpointSaveStatus::Failed, DownloadCheckpointSaveStatus::BlockedByExistingData,
                 static_cast<DownloadCheckpointSaveStatus>(999));
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    REQUIRE_FALSE(h.controller.retryFinalSave());
    REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
    h.controller.shutdown();
    h.controller.tick(1);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE_FALSE(h.controller.retryFinalSave());
    h.io.saved(status);
    h.controller.tick(2);
    const auto expected =
        status == DownloadCheckpointSaveStatus::BlockedByExistingData ? status : DownloadCheckpointSaveStatus::Failed;
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    REQUIRE(h.controller.saveStatus() == expected);
    REQUIRE(h.service.stopped());
    REQUIRE_FALSE(h.controller.stopped());
    REQUIRE(h.io.shutdowns == 0);
    h.controller.shutdown();
    h.controller.tick(3);
    h.controller.tick(4);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.controller.begin(admissionInput()).result.error == DownloadSubmissionError::NotAvailable);
    REQUIRE(h.controller.retryFinalSave());
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].id > h.io.saves[0].id);
    REQUIRE(h.io.saves[1].checkpoint == h.io.saves[0].checkpoint);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE_FALSE(h.controller.retryFinalSave());
    REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
    h.io.saved();
    h.controller.tick(5);
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Saved);
    REQUIRE(h.io.shutdowns == 1);
    REQUIRE_FALSE(h.controller.stopped());
    h.io.allow_stopped = true;
    h.controller.tick(6);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Held download exit handles refused or uncertain enqueue without automatic persistence retry",
          "[download][final-save-policy]") {
    const auto failure = GENERATE(0, 1, 2, 3, 4);
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    if (failure < 3)
        h.io.save_command = failure == 0   ? DownloadCheckpointIoCommand::Busy
                            : failure == 1 ? DownloadCheckpointIoCommand::Closed
                                           : DownloadCheckpointIoCommand::InvalidRequest;
    else if (failure == 3)
        h.io.throw_save = true;
    else
        h.io.throw_save_after_accept = true;
    h.controller.shutdown();
    h.controller.tick(1);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
    REQUIRE(h.io.shutdowns == 0);
    h.controller.tick(2);
    REQUIRE(h.io.saves.size() == 1);
    h.io.throw_save = false;
    h.io.throw_save_after_accept = false;
    h.io.save_command = DownloadCheckpointIoCommand::Accepted;
    if (failure == 4) {
        const auto uncertain_id = h.io.saves[0].id;
        REQUIRE_FALSE(h.controller.retryFinalSave());
        REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
        h.io.completion = DownloadCheckpointIoResult{.operation_id = uncertain_id,
                                                     .operation = DownloadCheckpointOperation::Save,
                                                     .save_status = DownloadCheckpointSaveStatus::Saved};
        h.controller.tick(3);
        REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
        REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    }
    REQUIRE(h.controller.retryFinalSave());
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(4);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Saved);
}

TEST_CASE("Held final persistence never promotes a malformed or previously uncertain completion",
          "[download][final-save-policy]") {
    const auto failure = GENERATE(0, 1, 2);
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    h.controller.shutdown();
    h.controller.tick(1);
    const auto original_id = h.io.saves[0].id;
    if (failure == 0)
        h.io.throw_poll = true;
    else {
        h.io.saved();
        h.io.deliver_malformed = true;
        if (failure == 1)
            h.io.completion->operation_id += 50;
        else
            h.io.completion->operation = DownloadCheckpointOperation::Load;
    }
    h.controller.tick(2);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
    REQUIRE(h.io.shutdowns == 0);
    h.io.throw_poll = false;
    h.io.deliver_malformed = false;
    if (failure == 0) {
        REQUIRE_FALSE(h.controller.retryFinalSave());
        h.io.completion = DownloadCheckpointIoResult{.operation_id = original_id,
                                                     .operation = DownloadCheckpointOperation::Save,
                                                     .save_status = DownloadCheckpointSaveStatus::Saved};
        h.controller.tick(3);
        REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
        REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    }
    REQUIRE(h.controller.retryFinalSave());
    const auto retry_id = h.io.saves.back().id;
    REQUIRE(retry_id > original_id);
    h.io.completion = DownloadCheckpointIoResult{.operation_id = original_id,
                                                 .operation = DownloadCheckpointOperation::Save,
                                                 .save_status = DownloadCheckpointSaveStatus::Saved};
    h.controller.tick(4);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE(h.io.shutdowns == 0);
    h.io.completion = DownloadCheckpointIoResult{.operation_id = retry_id,
                                                 .operation = DownloadCheckpointOperation::Save,
                                                 .save_status = DownloadCheckpointSaveStatus::Saved};
    h.io.allow_stopped = true;
    h.controller.tick(5);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Saved);
}

TEST_CASE("Held download exit can explicitly close without claiming successful persistence",
          "[download][final-save-policy]") {
    const bool uncertain = GENERATE(false, true);
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    h.controller.shutdown();
    h.controller.tick(1);
    if (uncertain)
        h.io.throw_poll = true;
    else
        h.io.saved(DownloadCheckpointSaveStatus::BlockedByExistingData);
    h.controller.tick(2);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    const auto failure = h.controller.saveStatus();
    REQUIRE(h.controller.closeAfterFinalSaveFailure());
    REQUIRE(h.io.shutdowns == 1);
    REQUIRE_FALSE(h.controller.stopped());
    REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
    REQUIRE_FALSE(h.controller.retryFinalSave());
    h.io.throw_poll = false;
    if (uncertain)
        h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(3);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.saveStatus() == failure);
    REQUIRE(h.io.saves.size() == 1);
}

TEST_CASE("Held shutdown of failed admission saves cancelled history only by final retry without an admission ACK",
          "[download][final-save-policy]") {
    const auto status =
        GENERATE(DownloadCheckpointSaveStatus::Failed, DownloadCheckpointSaveStatus::BlockedByExistingData);
    AdmissionHarness h({.max_jobs = 1, .max_requests = 1}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    const auto request = admissionInput();
    pendingWithoutAck(h.controller.begin(request));
    h.io.saved(status);
    h.controller.tick(1);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::AdmissionFailed);
    h.controller.shutdown();
    h.controller.tick(2);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.io.shutdowns == 0);
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Cancelled);
    notAccepted(h.controller.poll(request.request_id));
    REQUIRE(h.controller.retryFinalSave());
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves[1].checkpoint.jobs[0].job_id == "owner-1");
    REQUIRE(h.io.saves[1].checkpoint.jobs[0].state == DownloadJobState::Cancelled);
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(3);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Saved);
    notAccepted(h.controller.poll(request.request_id));
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Hold policy preserves a healthy matching admission acknowledgement during shutdown",
          "[download][final-save-policy]") {
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    const auto request = admissionInput();
    pendingWithoutAck(h.controller.begin(request));
    h.controller.shutdown();
    h.controller.tick(1);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Stopping);
    REQUIRE(h.io.saves.size() == 1);
    h.io.saved();
    h.controller.tick(2);
    const auto receipt = h.controller.poll(request.request_id);
    REQUIRE(receipt);
    REQUIRE(receipt->error == DownloadSubmissionError::None);
    REQUIRE(h.io.saves.size() == 2);
    REQUIRE(h.io.saves.back().checkpoint.jobs[0].state == DownloadJobState::Cancelled);
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(3);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.execution.starts.empty());
}

TEST_CASE("Held final retry waits for native and durable import drain and never reexecutes a job",
          "[download][final-save-policy]") {
    const bool handoff = GENERATE(false, true);
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    pendingWithoutAck(h.controller.begin(admissionInput()));
    h.io.saved();
    h.controller.tick(0);
    REQUIRE(h.execution.starts.size() == 1);
    if (handoff) {
        h.importer.pending = true;
        h.execution.update = {.job_id = "owner-1",
                              .state = DownloadExecutionState::Published,
                              .phase = DownloadPhase::Publishing,
                              .published_mp3 = admissionFinalPath()};
        h.controller.tick(1);
    }
    h.controller.shutdown();
    h.controller.tick(2);
    REQUIRE_FALSE(h.controller.retryFinalSave());
    REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
    REQUIRE(h.io.saves.size() == 1);
    if (handoff)
        h.importer.polled = DownloadImportState::Committed;
    else
        h.execution.update = {.job_id = "owner-1", .state = DownloadExecutionState::Cancelled};
    h.controller.tick(3);
    REQUIRE(h.io.saves.size() == 2);
    h.io.saved(DownloadCheckpointSaveStatus::Failed);
    h.controller.tick(4);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    const auto final = h.io.saves.back().checkpoint;
    REQUIRE(h.controller.retryFinalSave());
    REQUIRE(h.io.saves.back().checkpoint == final);
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(5);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.execution.starts == std::vector<std::string>{"owner-1"});
    REQUIRE(h.importer.calls == (handoff ? std::size_t{1} : std::size_t{0}));
    REQUIRE(h.execution.cancellations.size() == (handoff ? std::size_t{0} : std::size_t{1}));
}

TEST_CASE("Held final decisions cannot reenter the checkpoint port or admit new jobs",
          "[download][final-save-policy]") {
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    h.controller.shutdown();
    h.controller.tick(1);
    h.io.saved(DownloadCheckpointSaveStatus::Failed);
    h.controller.tick(2);
    h.io.on_save = [&] {
        REQUIRE_FALSE(h.controller.retryFinalSave());
        REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
        REQUIRE(h.controller.begin(admissionInput()).result.error == DownloadSubmissionError::Busy);
        h.controller.shutdown();
        h.controller.tick(99);
        REQUIRE(h.io.shutdowns == 0);
    };
    REQUIRE(h.controller.retryFinalSave());
    REQUIRE(h.io.saves.size() == 2);
    h.io.on_save = {};
    h.io.saved();
    h.io.on_poll = [&] {
        REQUIRE_FALSE(h.controller.retryFinalSave());
        REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
    };
    h.io.allow_stopped = true;
    h.controller.tick(3);
    REQUIRE(h.controller.stopped());
}

TEST_CASE("Held final retry bounds ambiguous offers and drains either uncertain operation without an ACK",
          "[download][final-save-policy][uncertain-retry]") {
    const auto uncertainty = GENERATE(0, 1, 2);
    AdmissionHarness h({}, DownloadFinalSavePolicy::HoldForDecision);
    h.ready();
    h.controller.shutdown();
    h.controller.tick(1);
    const auto first_id = h.io.saves[0].id;
    if (uncertainty == 0) {
        h.io.saved();
        h.io.throw_poll_after_take = true;
    } else
        h.io.throw_poll = true;
    h.controller.tick(2);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    h.io.throw_poll = false;
    h.io.throw_poll_after_take = false;
    if (uncertainty == 0)
        h.io.throw_save_after_accept = true;
    else
        h.io.throw_save = true;
    REQUIRE_FALSE(h.controller.retryFinalSave());
    REQUIRE(h.io.saves.size() == 2);
    const auto second_id = h.io.saves[1].id;
    REQUIRE(second_id > first_id);
    h.io.throw_save = false;
    h.io.throw_save_after_accept = false;
    // No unbounded list of possibly-owned native operations may accumulate.
    REQUIRE_FALSE(h.controller.retryFinalSave());
    REQUIRE(h.io.saves.size() == 2);
    if (uncertainty == 2) {
        REQUIRE(h.controller.closeAfterFinalSaveFailure());
        h.io.allow_stopped = true;
        h.controller.tick(3);
        REQUIRE(h.controller.stopped());
        REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
        return;
    }
    h.io.completion = DownloadCheckpointIoResult{.operation_id = uncertainty == 0 ? second_id : first_id,
                                                 .operation = DownloadCheckpointOperation::Save,
                                                 .save_status = DownloadCheckpointSaveStatus::Saved};
    h.controller.tick(3);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::FinalSaveFailed);
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Failed);
    REQUIRE(h.io.shutdowns == 0);
    REQUIRE(h.controller.retryFinalSave());
    h.io.saved();
    h.io.allow_stopped = true;
    h.controller.tick(4);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.controller.saveStatus() == DownloadCheckpointSaveStatus::Saved);
}

TEST_CASE("Hold policy cannot save defaults after blocked or interrupted startup", "[download][final-save-policy]") {
    const auto status = GENERATE(SessionLoadStatus::RecoveredFromBackup, SessionLoadStatus::InvalidUsingDefaults,
                                 SessionLoadStatus::FutureVersion);
    RecoveryAdmissionHarness h({.status = status}, "owner", DownloadFinalSavePolicy::HoldForDecision);
    h.load({.status = DownloadCheckpointLoadStatus::Missing});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    h.controller.shutdown();
    h.io.allow_stopped = true;
    h.controller.tick(1);
    REQUIRE(h.controller.stopped());
    REQUIRE_FALSE(h.controller.retryFinalSave());
    REQUIRE_FALSE(h.controller.closeAfterFinalSaveFailure());
    REQUIRE(h.io.saves.empty());
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

TEST_CASE("Startup namespace recovery restores loaded history without another IO operation",
          "[download][recovery-controller][namespace-recovery][FR-051]") {
    const bool missing = GENERATE(false, true);
    RecoveryAdmissionHarness h{healthyAdmissionSession("owner-1")};
    h.load(missing ? DownloadCheckpointLoadResult{.status = DownloadCheckpointLoadStatus::Missing}
                   : DownloadCheckpointLoadResult{.checkpoint = recoveryAdmissionCheckpoint(),
                                                  .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::RenewNamespace);
    REQUIRE_FALSE(h.controller.retryStartup());
    REQUIRE(h.controller.renewStartupNamespace());
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.controller.loadStatus() ==
            (missing ? DownloadCheckpointLoadStatus::Missing : DownloadCheckpointLoadStatus::Loaded));
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::Ready);
    REQUIRE(h.controller.recoveryReport().session_only_job_ids == std::vector<std::string>{"owner-1"});
    REQUIRE(h.service.snapshot().size() == (missing ? 0 : 1));
    if (!missing) {
        REQUIRE(h.service.snapshot()[0].job_id == "prior-1");
        REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
        const auto replay = h.controller.begin(admissionInput());
        REQUIRE(replay.state == DownloadAdmissionState::Completed);
        REQUIRE(replay.result.job_ids == std::vector<std::string>{"prior-1"});
    }
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.execution.polls.empty());
    REQUIRE(h.execution.cancellations.empty());
    REQUIRE(h.importer.calls == 0);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    REQUIRE_FALSE(h.controller.retryStartup());
    const auto next = h.controller.begin(admissionInput(2));
    REQUIRE(next.state == DownloadAdmissionState::Pending);
    REQUIRE(h.io.saves.size() == 1);
    REQUIRE(h.io.saves.back().checkpoint.jobs.back().job_id == "owner-r001-1");
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Startup namespace recovery preserves the detached checkpoint and its acknowledgement IDs",
          "[download][recovery-controller][namespace-recovery][FR-051]") {
    auto checkpoint = recoveryAdmissionCheckpoint();
    checkpoint.jobs[0].job_id = "owner-1";
    checkpoint.receipts[0].job_ids[0] = "owner-1";
    RecoveryAdmissionHarness h{{.status = SessionLoadStatus::Missing}};
    h.load({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
    checkpoint.jobs.clear();
    checkpoint.receipts.clear();
    REQUIRE(h.controller.renewStartupNamespace());
    REQUIRE(h.service.snapshot().size() == 1);
    REQUIRE(h.service.snapshot()[0].job_id == "owner-1");
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    const auto acknowledgement = h.controller.poll(admissionInput().request_id);
    REQUIRE(acknowledgement.has_value());
    REQUIRE(acknowledgement->job_ids == std::vector<std::string>{"owner-1"});
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Legacy startup namespace recovery preserves its restore semantics",
          "[download][recovery-controller][namespace-recovery][FR-053]") {
    AdmissionHarness h;
    auto checkpoint = recoveredAdmission();
    checkpoint.jobs[0].job_id = "owner-1";
    checkpoint.receipts[0].job_ids[0] = "owner-1";
    REQUIRE(h.controller.start());
    h.io.loaded({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
    h.controller.tick(0);
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::RenewNamespace);
    REQUIRE(h.controller.renewStartupNamespace());
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Ready);
    REQUIRE(h.service.snapshot()[0].job_id == "owner-1");
    REQUIRE(h.service.snapshot()[0].state == DownloadJobState::Interrupted);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::NotRequested);
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Receipt conflicts require history review instead of ineffectual checkpoint retry",
          "[download][recovery-controller][namespace-recovery][FR-053]") {
    const auto state = GENERATE(DownloadJobState::Completed, DownloadJobState::Failed, DownloadJobState::Cancelled);
    auto checkpoint = recoveryAdmissionCheckpoint(state);
    if (state == DownloadJobState::Completed)
        checkpoint.jobs[0].completed_mp3 = admissionFinalPath().parent_path() / "divergent.mp3";
    RecoveryAdmissionHarness h{healthyAdmissionSession()};
    h.load({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::ReceiptConflict);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::ReviewHistory);
    REQUIRE_FALSE(h.controller.retryStartup());
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    REQUIRE(h.service.snapshot().empty());
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Namespace recovery cannot hide a later receipt conflict or partially restore history",
          "[download][recovery-controller][namespace-recovery][FR-053]") {
    auto checkpoint = recoveryAdmissionCheckpoint(DownloadJobState::Completed);
    checkpoint.jobs[0].completed_mp3 = admissionFinalPath().parent_path() / "divergent.mp3";
    RecoveryAdmissionHarness h{healthyAdmissionSession(), "prior"};
    h.load({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::RenewNamespace);
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    REQUIRE(h.controller.state() == DownloadAdmissionControllerState::Blocked);
    REQUIRE(h.controller.recoveryReport().status == DownloadRecoveryStatus::ReceiptConflict);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::ReviewHistory);
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    REQUIRE_FALSE(h.controller.retryStartup());
    REQUIRE(h.service.snapshot().empty());
    const auto retained = h.service.checkpoint();
    REQUIRE(retained.has_value());
    REQUIRE(retained->receipts.empty());
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.execution.starts.empty());
    REQUIRE(h.importer.calls == 0);
}

TEST_CASE("Namespace recovery is unavailable before blocking while stopping or from IO callbacks",
          "[download][recovery-controller][namespace-recovery][FR-053]") {
    RecoveryAdmissionHarness h{healthyAdmissionSession("owner-1")};
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    REQUIRE(h.controller.start());
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    unsigned attempts = 0;
    h.io.on_poll = [&] {
        ++attempts;
        REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
        REQUIRE_FALSE(h.controller.renewStartupNamespace());
    };
    h.io.loaded({.status = DownloadCheckpointLoadStatus::Missing});
    h.controller.tick(0);
    REQUIRE(attempts == 1);
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::RenewNamespace);
    h.controller.shutdown();
    REQUIRE_FALSE(h.controller.renewStartupNamespace());
    REQUIRE(h.controller.startupRecoveryAction() == DownloadStartupRecoveryAction::None);
    h.io.on_poll = {};
    h.io.allow_stopped = true;
    h.controller.tick(1);
    REQUIRE(h.controller.stopped());
    REQUIRE(h.io.loads.size() == 1);
    REQUIRE(h.io.saves.empty());
    REQUIRE(h.importer.calls == 0);
}

} // namespace music_player::m0::test
