#include "adapters/DownloadJobsModel.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/ports/DownloadRecoveryValidation.hpp"
#include "presentation/DownloadApplicationBinding.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"

#include <QAbstractItemModel>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";
DownloadRequest input() {
    return {.request_id = request_id,
            .items = {{.url = "https://synthetic.example.test/audio.mp3?secret=never-display",
                       .title = "<b>Literal synthetic title</b>",
                       .kind = DownloadMediaKind::Audio}}};
}
std::filesystem::path path() { return std::filesystem::path{L"C:/rc-music-synthetic/known.mp3"}; }
class Platform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
};
class Execution final : public DownloadExecution {
  public:
    bool start(const std::string& id, const DownloadItem&) override {
        ++starts;
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view) override { ++cancellations; }
    DownloadExecutionUpdate poll(std::string_view) override { return update; }
    void shutdown() { ++shutdowns; }
    bool stopped() const {
        if (throw_probe)
            throw std::runtime_error{"private worker error"};
        return shutdowns != 0 && physical_stopped;
    }
    int starts{0};
    int cancellations{0};
    int shutdowns{0};
    bool physical_stopped{false};
    bool throw_probe{false};
    DownloadExecutionUpdate update{};
};
class Importer final : public DownloadImport {
  public:
    bool supportsDurableRecovery() const override {
        if (on_supports)
            on_supports();
        return durable;
    }
    bool accept(const std::string& id, const std::filesystem::path& source) override {
        ++calls;
        inputs.emplace_back(id, source);
        if (on_accept)
            on_accept();
        return true;
    }
    int calls{0};
    std::function<void()> on_accept{};
    std::function<void()> on_poll{};
    std::function<void()> on_supports{};
    bool pending{false};
    bool durable{true};
    std::optional<DownloadImportState> begin_result{};
    std::vector<std::pair<std::string, std::filesystem::path>> inputs{};
    DownloadImportState polled{DownloadImportState::Pending};
    DownloadImportState begin(const std::string& id, const std::filesystem::path& file) override {
        const bool accepted = accept(id, file);
        if (begin_result)
            return *begin_result;
        return pending ? DownloadImportState::Pending
                       : (accepted ? DownloadImportState::Committed : DownloadImportState::Failed);
    }
    DownloadImportState pollImport(std::string_view) override {
        if (on_poll)
            on_poll();
        return polled;
    }
};
class Identity final : public DownloadRequestIdentity {
  public:
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};
class Io final : public DownloadCheckpointIo {
  public:
    DownloadCheckpointIoCommand startLoad(const std::uint64_t id) override {
        loads.push_back(id);
        return DownloadCheckpointIoCommand::Accepted;
    }
    DownloadCheckpointIoCommand startSave(const std::uint64_t id, const DownloadCheckpoint& snapshot) override {
        saves.push_back({id, snapshot});
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(std::uint64_t) override {
        ++polls;
        if (on_poll)
            on_poll();
        return std::exchange(completion, std::nullopt);
    }
    void shutdown() override { ++shutdowns; }
    bool stopped() const override { return shutdowns != 0 && physical_stopped; }
    void loaded(DownloadCheckpointLoadResult loaded = {}) {
        completion = DownloadCheckpointIoResult{
            .operation_id = loads.back(), .operation = DownloadCheckpointOperation::Load, .load = std::move(loaded)};
    }
    void saved(DownloadCheckpointSaveStatus status = DownloadCheckpointSaveStatus::Saved) {
        completion = DownloadCheckpointIoResult{
            .operation_id = saves.back().first, .operation = DownloadCheckpointOperation::Save, .save_status = status};
    }
    std::vector<std::uint64_t> loads{};
    std::vector<std::pair<std::uint64_t, DownloadCheckpoint>> saves{};
    std::optional<DownloadCheckpointIoResult> completion{};
    std::function<void()> on_poll{};
    int polls{0};
    int shutdowns{0};
    bool physical_stopped{false};
};
class Review final : public DownloadRecoveryReview {
  public:
    DownloadRecoveryReviewCommand startReview(std::uint64_t operation_id, std::string_view job_id) override {
        return startAction(operation_id, job_id, DownloadRecoveryReviewAction::ReviewFiles);
    }
    DownloadRecoveryReviewCommand startDiscardPartials(std::uint64_t operation_id, std::string_view job_id) override {
        return startAction(operation_id, job_id, DownloadRecoveryReviewAction::DiscardPartials);
    }
    bool supportsDiscardPartials() const override { return discard_supported; }
    DownloadRecoveryReviewCommand startAction(std::uint64_t operation_id, std::string_view job_id,
                                              DownloadRecoveryReviewAction action) {
        requests.emplace_back(operation_id, std::string{job_id});
        actions.push_back(action);
        if (enforce_slot && active_operation)
            return DownloadRecoveryReviewCommand::Busy;
        if (enforce_slot && command == DownloadRecoveryReviewCommand::Accepted)
            active_operation = operation_id;
        if (on_start)
            on_start();
        if (throw_start)
            throw std::runtime_error{"private review start error"};
        return command;
    }
    std::optional<DownloadRecoveryReviewResult> poll(std::uint64_t operation_id) override {
        polled_ids.push_back(operation_id);
        if (on_poll)
            on_poll();
        if (throw_poll)
            throw std::runtime_error{"private review poll error"};
        auto result = std::exchange(completion, std::nullopt);
        if (result && result->operation_id == operation_id)
            active_operation.reset();
        return result;
    }
    void shutdown() override { ++shutdowns; }
    bool stopped() const override {
        if (throw_probe)
            throw std::runtime_error{"private review probe error"};
        return shutdowns != 0 && physical_stopped;
    }
    std::vector<std::pair<std::uint64_t, std::string>> requests;
    std::vector<DownloadRecoveryReviewAction> actions;
    std::vector<std::uint64_t> polled_ids;
    std::optional<DownloadRecoveryReviewResult> completion;
    DownloadRecoveryReviewCommand command{DownloadRecoveryReviewCommand::Accepted};
    std::function<void()> on_start;
    std::function<void()> on_poll;
    bool throw_start{false};
    bool throw_poll{false};
    bool throw_probe{false};
    bool enforce_slot{false};
    bool discard_supported{false};
    std::optional<std::uint64_t> active_operation;
    bool physical_stopped{false};
    int shutdowns{0};
    void complete(DownloadRecoveryReviewOutcome outcome) {
        completion = DownloadRecoveryReviewResult{.operation_id = requests.back().first,
                                                  .job_id = requests.back().second,
                                                  .outcome = outcome,
                                                  .action = actions.back()};
    }
};
class Validation final : public DownloadRecoveryValidation {
  public:
    DownloadRecoveryValidationCommand startValidation(std::uint64_t id, std::string_view job) override {
        requests.emplace_back(id, std::string{job});
        if (active)
            return DownloadRecoveryValidationCommand::Busy;
        if (command == DownloadRecoveryValidationCommand::Accepted)
            active = id;
        if (on_start)
            on_start();
        if (throw_start)
            throw std::runtime_error{"private validation start"};
        return command;
    }
    DownloadRecoveryValidationCommand prepareImport(std::uint64_t id) override {
        preparations.push_back(id);
        if (on_prepare)
            on_prepare();
        if (throw_prepare)
            throw std::runtime_error{"private import preparation"};
        return prepare_command;
    }
    bool release(std::uint64_t id) override {
        releases.push_back(id);
        if (throw_release)
            throw std::runtime_error{"private release"};
        return active && *active == id && allow_release;
    }
    std::optional<DownloadRecoveryValidationResult> poll(std::uint64_t id) override {
        polls.push_back(id);
        if (on_poll)
            on_poll();
        if (throw_poll)
            throw std::runtime_error{"private validation poll"};
        if (!completion)
            return std::nullopt;
        auto result = std::exchange(completion, std::nullopt);
        if (result->operation_id == id && result->outcome != DownloadRecoveryValidationOutcome::Validated &&
            result->outcome != DownloadRecoveryValidationOutcome::ImportReady)
            active.reset();
        return result;
    }
    void shutdown() override {
        ++shutdowns;
        if (throw_shutdown)
            throw std::runtime_error{"private validation shutdown"};
    }
    bool stopped() const override {
        if (throw_probe)
            throw std::runtime_error{"private validation stop probe"};
        return shutdowns != 0 && physical_stopped;
    }
    void complete(DownloadRecoveryValidationOutcome outcome) {
        completion = DownloadRecoveryValidationResult{requests.back().first, requests.back().second, outcome};
    }
    void readyImport(const std::filesystem::path& source = path()) {
        completion = DownloadRecoveryValidationResult{requests.back().first, requests.back().second,
                                                      DownloadRecoveryValidationOutcome::ImportReady, source};
    }
    std::vector<std::pair<std::uint64_t, std::string>> requests{};
    std::vector<std::uint64_t> releases{}, polls{}, preparations{};
    std::optional<std::uint64_t> active{};
    std::optional<DownloadRecoveryValidationResult> completion{};
    DownloadRecoveryValidationCommand command{DownloadRecoveryValidationCommand::Accepted};
    DownloadRecoveryValidationCommand prepare_command{DownloadRecoveryValidationCommand::Accepted};
    std::function<void()> on_start{}, on_poll{}, on_prepare{};
    bool throw_start{false}, throw_poll{false}, throw_release{false}, allow_release{true}, physical_stopped{false};
    bool throw_shutdown{false}, throw_probe{false};
    bool throw_prepare{false};
    int shutdowns{0};
};
struct Harness final {
    Platform platform;
    PlayerPresenter presenter;
    Execution execution;
    Importer importer;
    Identity identity;
    Io io;
    DownloadService service;
    DownloadAdmissionController controller;
    Review review;
    Validation validation;
    std::int64_t now{0};
    int clock_reads{0};
    bool throw_clock{false};
    DownloadApplicationBinding binding;
    explicit Harness(SessionLoadResult session = {}, bool with_review = false, bool with_validation = false)
        : presenter(
              FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE)),
              platform),
          service(execution, importer, "fresh", {}, &identity),
          controller(service, io, session, DownloadFinalSavePolicy::HoldForDecision),
          binding(presenter, controller, service,
                  {.shutdown = [&] { execution.shutdown(); }, .stopped = [&] { return execution.stopped(); }},
                  {.now_ms =
                       [&] {
                           ++clock_reads;
                           if (throw_clock)
                               throw std::runtime_error{"private clock error"};
                           return now;
                       },
                   .automatic_polling = false,
                   .recovery_review = with_review ? &review : nullptr,
                   .recovery_validation = with_validation ? &validation : nullptr}) {}
    bool ready() {
        if (!binding.start() || io.loads.size() != 1)
            return false;
        io.loaded();
        binding.poll();
        return controller.state() == DownloadAdmissionControllerState::Ready;
    }
    void playerPoll() { PlayerPresenterTestAccess::pollPersistence(presenter); }
    bool interrupted() {
        if (!binding.start())
            return false;
        const DownloadCheckpoint checkpoint{.receipts = {{.request_id = request_id,
                                                          .fingerprint = std::string(64, 'a'),
                                                          .job_ids = {"old-1", "old-2"},
                                                          .retained_request_bytes = 128}},
                                            .jobs = {{.job_id = "old-1", .title = "Interrupted first"},
                                                     {.job_id = "old-2", .title = "Interrupted second"}}};
        io.loaded({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
        binding.poll();
        return controller.state() == DownloadAdmissionControllerState::Ready;
    }
    QVariant reviewValue(int role, int row = 0) {
        auto* model = binding.jobsModel();
        return model->data(model->index(row, 0), role);
    }
    bool heldValidation() {
        if (!interrupted() || !binding.reviewFiles(QStringLiteral("old-1")))
            return false;
        review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        binding.poll();
        if (!binding.validateMedia(QStringLiteral("old-1")))
            return false;
        validation.complete(DownloadRecoveryValidationOutcome::Validated);
        binding.poll();
        return reviewValue(DownloadJobsModel::CanReleaseValidationRole).toBool();
    }
};
} // namespace

class DownloadApplicationBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void recoveredImportRequiresCurrentOneShotConsentAndNativePreparationBeforeHandoff() {
        Harness h({}, true, true);
        QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.heldValidation());
        QVERIFY(h.reviewValue(DownloadJobsModel::CanImportRecoveredRole).toBool());
        const auto rows = h.presenter.queueModelObject()->rowCount();
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        const auto cancelled_token = h.binding.importConsentToken();
        QVERIFY(!cancelled_token.isEmpty());
        QCOMPARE(h.binding.importConsentJobId(), QStringLiteral("old-1"));
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.validation.preparations.size(), std::size_t{0});
        h.binding.cancelRecoveredImportConfirmation(QStringLiteral("old-1"), cancelled_token);
        QVERIFY(h.binding.importConsentToken().isEmpty());
        QVERIFY(!h.binding.confirmRecoveredImport(QStringLiteral("old-1"), cancelled_token));
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        const auto token = h.binding.importConsentToken();
        QVERIFY(token != cancelled_token);
        QVERIFY(!h.binding.confirmRecoveredImport(QStringLiteral("old-2"), token));
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), token));
        QVERIFY(h.binding.importConsentToken().isEmpty());
        QVERIFY(!h.binding.confirmRecoveredImport(QStringLiteral("old-1"), token));
        QCOMPARE(h.validation.preparations.size(), std::size_t{1});
        QCOMPARE(h.validation.preparations.front(), h.validation.requests.back().first);
        QVERIFY2(h.importer.calls == 0, "Consent must prepare the retained candidate before any queue handoff");
        QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        h.binding.poll();
        QCOMPARE(h.importer.calls, 0);
        h.validation.readyImport();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.importer.inputs.front(), (std::pair{std::string{"old-1"}, path()}));
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Imported);
        QCOMPARE(h.validation.releases.size(), std::size_t{1});
        QVERIFY2(h.validation.active.has_value(), "Saved handoff still awaits actual native release acknowledgement");
        h.validation.complete(DownloadRecoveryValidationOutcome::Released);
        h.binding.poll();
        QVERIFY(!h.validation.active);
        QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), rows);
    }
    void recoveredImportPendingRetainsProofUntilCommitAndExitWaitsForBothAcknowledgements() {
        Harness h({}, true, true);
        h.importer.pending = true;
        QVERIFY(h.heldValidation());
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), h.binding.importConsentToken()));
        h.validation.readyImport();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.service.recoveredImportState("old-1"), std::optional{DownloadImportState::Pending});
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(!h.binding.releaseValidation(QStringLiteral("old-1")));
        QVERIFY(h.presenter.requestExit());
        QVERIFY2(h.validation.shutdowns == 0, "Shutdown must not release proof while Session is still pending");
        QVERIFY(h.validation.releases.empty());
        h.execution.physical_stopped = true;
        h.review.physical_stopped = true;
        h.io.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QVERIFY(!h.presenter.exitCommitted());
        QCOMPARE(h.validation.shutdowns, 0);
        QVERIFY(h.validation.releases.empty());
        h.importer.polled = DownloadImportState::Committed;
        h.binding.poll();
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Imported);
        QVERIFY(!h.io.saves.empty());
        h.io.saved();
        h.validation.complete(DownloadRecoveryValidationOutcome::Released);
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        QCOMPARE(h.validation.shutdowns, 1);
        h.validation.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.execution.starts, 0);
    }
    void recoveredImportFailureKeepsTheKnownJobAndProofForANewExplicitConsent() {
        Harness h({}, true, true);
        h.importer.begin_result = DownloadImportState::Failed;
        QVERIFY(h.heldValidation());
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        const auto first_token = h.binding.importConsentToken();
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), first_token));
        h.validation.readyImport();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.service.recoveredImportState("old-1"), std::optional{DownloadImportState::Failed});
        QVERIFY(h.validation.active.has_value());
        QVERIFY(h.validation.releases.empty());
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(h.reviewValue(DownloadJobsModel::CanImportRecoveredRole).toBool());
        QVERIFY(!h.binding.confirmRecoveredImport(QStringLiteral("old-1"), first_token));
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.importConsentToken() != first_token);
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), h.binding.importConsentToken()));
        QCOMPARE(h.validation.preparations.size(), std::size_t{2});
        h.importer.begin_result = DownloadImportState::Committed;
        h.validation.readyImport();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 2);
        QCOMPARE(h.importer.inputs[0], h.importer.inputs[1]);
        QCOMPARE(h.service.snapshot().size(), std::size_t{2});
        QCOMPARE(h.service.snapshot()[0].job_id, std::string{"old-1"});
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Imported);
        QCOMPARE(h.execution.starts, 0);
    }
    void malformedRecoveredImportPreparationCannotReachTheImporter_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("missing trusted path") << 0;
        QTest::newRow("relative trusted path") << 1;
        QTest::newRow("wrong format") << 2;
        QTest::newRow("wrong operation") << 3;
        QTest::newRow("wrong job") << 4;
        QTest::newRow("unexpected retained outcome with a path") << 5;
        QTest::newRow("unknown outcome") << 6;
    }
    void malformedRecoveredImportPreparationCannotReachTheImporter() {
        QFETCH(int, fault);
        Harness h({}, true, true);
        QVERIFY(h.heldValidation());
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), h.binding.importConsentToken()));
        h.validation.readyImport();
        if (fault == 0)
            h.validation.completion->trusted_mp3.clear();
        else if (fault == 1)
            h.validation.completion->trusted_mp3 = "relative.mp3";
        else if (fault == 2)
            h.validation.completion->trusted_mp3 = std::filesystem::path{L"C:/rc-music-synthetic/known.mp4"};
        else if (fault == 3)
            ++h.validation.completion->operation_id;
        else if (fault == 4)
            h.validation.completion->job_id = "old-2";
        else if (fault == 5)
            h.validation.completion->outcome = DownloadRecoveryValidationOutcome::Validated;
        else
            h.validation.completion->outcome = static_cast<DownloadRecoveryValidationOutcome>(999);
        h.binding.poll();
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.validation.shutdowns, 1);
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanImportRecoveredRole).toBool());
        QVERIFY(!h.binding.warningText().isEmpty());
        QVERIFY(!h.binding.warningText().contains(QStringLiteral("rc-music-synthetic")));
        QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-1")));
    }
    void unconfirmedPublicationAndMissingDurableCapabilityNeverImportRecoveredAudio() {
        Harness incapable({}, true, true);
        incapable.importer.durable = false;
        QVERIFY(incapable.heldValidation());
        QVERIFY(!incapable.reviewValue(DownloadJobsModel::CanImportRecoveredRole).toBool());
        QVERIFY(!incapable.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QCOMPARE(incapable.importer.calls, 0);
        Harness h({}, true, true);
        QVERIFY(h.heldValidation());
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), h.binding.importConsentToken()));
        h.validation.complete(DownloadRecoveryValidationOutcome::PublicationUnconfirmed);
        h.binding.poll();
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanImportRecoveredRole).toBool());
        QVERIFY(!h.validation.active.has_value());
        QVERIFY(!h.reviewValue(DownloadJobsModel::ValidationTextRole).toString().isEmpty());
    }
    void recoveredCapabilityQueryCannotAdmitHandoffAfterNestedPlayerExit() {
        Harness h({}, true, true);
        QVERIFY(h.heldValidation());
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), h.binding.importConsentToken()));
        bool exit_requested = false;
        // Install only after consent/native preparation. The next health query
        // occurs inside the service's actual beginRecoveredImport admission.
        h.importer.on_supports = [&] {
            if (!exit_requested) {
                exit_requested = true;
                QVERIFY(h.presenter.requestExit());
            }
        };
        h.validation.readyImport();
        h.binding.poll();
        QVERIFY(exit_requested);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.execution.starts, 0);
        QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.importConsentToken().isEmpty());
    }
    void recoveredConsentAndPreparationReentryCannotImportAfterShutdown() {
        Harness h({}, true, true);
        QVERIFY(h.heldValidation());
        connect(&h.binding, &DownloadApplicationBinding::importConsentChanged, &h.binding, [&] {
            if (!h.binding.importConsentToken().isEmpty())
                QVERIFY(!h.binding.confirmRecoveredImport(QStringLiteral("old-1"), h.binding.importConsentToken()));
        });
        QVERIFY(h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QCOMPARE(h.validation.preparations.size(), std::size_t{0});
        const auto token = h.binding.importConsentToken();
        h.validation.on_prepare = [&] {
            QVERIFY(!h.binding.confirmRecoveredImport(QStringLiteral("old-1"), token));
            QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-2")));
            QVERIFY(h.presenter.requestExit());
        };
        QVERIFY(h.binding.confirmRecoveredImport(QStringLiteral("old-1"), token));
        QCOMPARE(h.validation.shutdowns, 1);
        h.validation.readyImport();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QVERIFY(!h.binding.requestRecoveredImport(QStringLiteral("old-1")));
        QVERIFY(h.binding.importConsentToken().isEmpty());
    }
    void validationRequiresFreshCandidateObservationAndRetainsTheLeaseAfterResult() {
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(!h.binding.validateMedia(QStringLiteral("old-1")));
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        QVERIFY(h.binding.validateMedia(QStringLiteral("old-1")));
        QCOMPARE(h.validation.requests.size(), std::size_t{1});
        QVERIFY(h.reviewValue(DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
        QVERIFY(!h.binding.requestDiscardPartials(QStringLiteral("old-2")));
        h.validation.complete(DownloadRecoveryValidationOutcome::Validated);
        h.binding.poll();
        QVERIFY(!h.reviewValue(DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(h.reviewValue(DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(h.validation.active.has_value());
        QVERIFY(!h.binding.validateMedia(QStringLiteral("old-2")));
        QVERIFY(!h.binding.releaseValidation(QStringLiteral("old-2")));
        QVERIFY(h.binding.releaseValidation(QStringLiteral("old-1")));
        QCOMPARE(h.validation.releases.front(), h.validation.requests.front().first);
        QVERIFY(!h.binding.releaseValidation(QStringLiteral("old-1")));
        h.validation.complete(DownloadRecoveryValidationOutcome::Released);
        h.binding.poll();
        QVERIFY(!h.validation.active);
        QVERIFY(h.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool());
        QCOMPARE(h.service.snapshot().front().state, DownloadJobState::Interrupted);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(h.io.saves.empty());
    }
    void validationWithoutAPortIsAbsentAndAnInvalidFileCanBeExplicitlyRetried() {
        Harness absent({}, true);
        QVERIFY(absent.interrupted());
        QVERIFY(absent.binding.reviewFiles(QStringLiteral("old-1")));
        absent.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        absent.binding.poll();
        QVERIFY(!absent.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!absent.binding.validateMedia(QStringLiteral("old-1")));
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        QVERIFY(h.binding.validateMedia(QStringLiteral("old-1")));
        h.validation.complete(DownloadRecoveryValidationOutcome::InvalidMedia);
        h.binding.poll();
        QVERIFY(h.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(h.binding.validateMedia(QStringLiteral("old-1")));
        QVERIFY(h.validation.requests.back().first > h.validation.requests.front().first);
        QCOMPARE(h.importer.calls, 0);
    }
    void validationStartAndPollReentryCannotBypassTheSlotAndExitWaitsForPhysicalStop() {
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        h.validation.on_start = [&] { QVERIFY(!h.binding.validateMedia(QStringLiteral("old-1"))); };
        QVERIFY(h.binding.validateMedia(QStringLiteral("old-1")));
        h.validation.on_poll = [&] { QVERIFY(!h.binding.releaseValidation(QStringLiteral("old-1"))); };
        h.validation.complete(DownloadRecoveryValidationOutcome::Validated);
        h.binding.poll();
        h.presenter.requestExit();
        h.binding.poll();
        QCOMPARE(h.validation.shutdowns, 1);
        h.io.saved();
        h.io.physical_stopped = true;
        h.review.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QVERIFY(!h.presenter.exitCommitted());
        h.validation.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QVERIFY(h.presenter.exitCommitted());
    }
    void uncertainValidationKeepsItsOriginalCorrelationUntilDrain_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("admitted then start throws") << 0;
        QTest::newRow("poll throws") << 1;
        QTest::newRow("wrong operation") << 2;
        QTest::newRow("wrong job") << 3;
        QTest::newRow("unknown outcome") << 4;
        QTest::newRow("release throws") << 5;
        QTest::newRow("release refused") << 6;
    }
    void uncertainValidationKeepsItsOriginalCorrelationUntilDrain() {
        QFETCH(int, failure);
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        h.validation.throw_start = failure == 0;
        const bool accepted = h.binding.validateMedia(QStringLiteral("old-1"));
        QCOMPARE(accepted, failure != 0);
        QCOMPARE(h.validation.requests.size(), std::size_t{1});
        const auto id = h.validation.requests.front().first;
        if (failure != 0) {
            h.validation.complete(DownloadRecoveryValidationOutcome::Validated);
            h.validation.throw_poll = failure == 1;
            if (failure == 2)
                ++h.validation.completion->operation_id;
            if (failure == 3)
                h.validation.completion->job_id = "not-the-observed-job";
            if (failure == 4)
                h.validation.completion->outcome = static_cast<DownloadRecoveryValidationOutcome>(999);
            h.binding.poll();
            if (failure >= 5) {
                h.validation.throw_release = failure == 5;
                h.validation.allow_release = failure != 6;
                QVERIFY(!h.binding.releaseValidation(QStringLiteral("old-1")));
            }
        }
        QCOMPARE(h.validation.shutdowns, 1);
        QVERIFY(!h.binding.validateMedia(QStringLiteral("old-1")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!h.binding.warningText().isEmpty());
        h.validation.throw_start = h.validation.throw_poll = h.validation.throw_release = false;
        h.validation.complete(DownloadRecoveryValidationOutcome::Released);
        h.binding.poll();
        QCOMPARE(h.validation.polls.back(), id);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.execution.starts, 0);
        QVERIFY(h.io.saves.empty());
    }
    void validationBusyAllowsExplicitRetryButOtherRefusalsRetireTheOwner_data() {
        QTest::addColumn<int>("command");
        QTest::newRow("busy") << static_cast<int>(DownloadRecoveryValidationCommand::Busy);
        QTest::newRow("closed") << static_cast<int>(DownloadRecoveryValidationCommand::Closed);
        QTest::newRow("invalid") << static_cast<int>(DownloadRecoveryValidationCommand::InvalidRequest);
        QTest::newRow("unknown") << 999;
    }
    void validationBusyAllowsExplicitRetryButOtherRefusalsRetireTheOwner() {
        QFETCH(int, command);
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        h.validation.command = static_cast<DownloadRecoveryValidationCommand>(command);
        QVERIFY(!h.binding.validateMedia(QStringLiteral("old-1")));
        QCOMPARE(h.validation.requests.size(), std::size_t{1});
        const auto first_id = h.validation.requests.front().first;
        const bool retryable = command == static_cast<int>(DownloadRecoveryValidationCommand::Busy);
        QCOMPARE(h.validation.shutdowns, retryable ? 0 : 1);
        QCOMPARE(h.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool(), retryable);
        h.validation.command = DownloadRecoveryValidationCommand::Accepted;
        QCOMPARE(h.binding.validateMedia(QStringLiteral("old-1")), retryable);
        QCOMPARE(h.validation.requests.size(), retryable ? std::size_t{2} : std::size_t{1});
        if (retryable)
            QVERIFY(h.validation.requests.back().first > first_id);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.execution.starts, 0);
        QVERIFY(h.io.saves.empty());
    }
    void pendingValidationReleaseRequiresAnExplicitRequestAndPhysicalAcknowledgement_data() {
        QTest::addColumn<bool>("requested");
        QTest::newRow("explicit pending cancellation") << true;
        QTest::newRow("unsolicited release is uncertain") << false;
    }
    void pendingValidationReleaseRequiresAnExplicitRequestAndPhysicalAcknowledgement() {
        QFETCH(bool, requested);
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        QVERIFY(h.binding.validateMedia(QStringLiteral("old-1")));
        QVERIFY(h.reviewValue(DownloadJobsModel::ValidationBusyRole).toBool());
        if (requested) {
            QVERIFY(h.binding.releaseValidation(QStringLiteral("old-1")));
            QCOMPARE(h.validation.releases.size(), std::size_t{1});
            QVERIFY(!h.binding.releaseValidation(QStringLiteral("old-1")));
            QVERIFY(h.reviewValue(DownloadJobsModel::ValidationBusyRole).toBool());
            QVERIFY(!h.reviewValue(DownloadJobsModel::CanReleaseValidationRole).toBool());
        }
        QVERIFY(!h.binding.validateMedia(QStringLiteral("old-1")));
        h.validation.complete(DownloadRecoveryValidationOutcome::Released);
        h.binding.poll();
        QCOMPARE(h.validation.shutdowns, requested ? 0 : 1);
        QCOMPARE(h.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool(), requested);
        if (!requested)
            QVERIFY(!h.binding.warningText().isEmpty());
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(h.io.saves.empty());
    }
    void idleValidationWorkerAndUncertainStopCallsCannotApproveExit_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("idle owner still drains") << 0;
        QTest::newRow("shutdown throws") << 1;
        QTest::newRow("stopped throws") << 2;
    }
    void idleValidationWorkerAndUncertainStopCallsCannotApproveExit() {
        QFETCH(int, fault);
        Harness h({}, false, true);
        QVERIFY(h.ready());
        h.validation.throw_shutdown = fault == 1;
        h.validation.throw_probe = fault == 2;
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.validation.requests.empty());
        QVERIFY(h.validation.shutdowns > 0);
        h.io.saved();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QVERIFY(!h.presenter.exitCommitted());
        h.validation.physical_stopped = true;
        if (fault != 0) {
            h.binding.poll();
            h.playerPoll();
            QVERIFY(!h.presenter.exitCommitted());
            QVERIFY(!h.binding.warningText().isEmpty());
        }
        h.validation.throw_shutdown = h.validation.throw_probe = false;
        h.binding.poll();
        if (!h.presenter.exiting())
            QVERIFY(h.presenter.requestExit());
        h.playerPoll();
        QVERIFY(h.presenter.exitCommitted());
        QVERIFY(h.validation.requests.empty());
    }
    void validationApprovalCannotSurviveShutdownOrReturnAfterReversedExit_data() {
        QTest::addColumn<bool>("observed");
        QTest::newRow("held approval before shutdown") << true;
        QTest::newRow("late success after shutdown") << false;
    }
    void validationApprovalCannotSurviveShutdownOrReturnAfterReversedExit() {
        QFETCH(bool, observed);
        Harness h({}, true, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        QVERIFY(h.binding.validateMedia(QStringLiteral("old-1")));
        if (observed) {
            h.validation.complete(DownloadRecoveryValidationOutcome::Validated);
            h.binding.poll();
            QVERIFY(h.reviewValue(DownloadJobsModel::ValidationTextRole)
                        .toString()
                        .startsWith(QStringLiteral("MP3 validado")));
        }
        QVERIFY(h.presenter.requestExit());
        QVERIFY(!h.reviewValue(DownloadJobsModel::ValidationTextRole)
                     .toString()
                     .startsWith(QStringLiteral("MP3 validado")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReleaseValidationRole).toBool());
        if (!observed) {
            h.validation.complete(DownloadRecoveryValidationOutcome::Validated);
            h.binding.poll();
            QVERIFY(!h.reviewValue(DownloadJobsModel::ValidationTextRole)
                         .toString()
                         .startsWith(QStringLiteral("MP3 validado")));
        }
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.validation.physical_stopped = true;
        h.review.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QVERIFY(!h.presenter.exiting());
        QVERIFY(!h.presenter.exitCommitted());
        const auto status = h.reviewValue(DownloadJobsModel::ValidationTextRole).toString();
        QVERIFY(!status.isEmpty());
        QVERIFY(!status.startsWith(QStringLiteral("MP3 validado")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReleaseValidationRole).toBool());
        QCOMPARE(h.service.snapshot().front().state, DownloadJobState::Interrupted);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.execution.starts, 0);
    }
    void partialDiscardRequiresExplicitCurrentOneShotConsentAndDoesNotChangePlayback() {
        Harness h({}, true);
        h.review.discard_supported = true;
        QVERIFY(h.interrupted());
        QVERIFY(!h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.reviewValue(DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto token = h.binding.discardConsentToken();
        QCOMPARE(h.binding.discardConsentJobId(), QStringLiteral("old-1"));
        QVERIFY(!token.isEmpty());
        QCOMPARE(h.review.requests.size(), std::size_t{1});
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-2"), token));
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), token + QStringLiteral("0")));
        QVERIFY(h.binding.confirmDiscardPartials(QStringLiteral("old-1"), token));
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), token));
        QCOMPARE(h.review.requests.size(), std::size_t{2});
        QCOMPARE(h.review.actions.back(), DownloadRecoveryReviewAction::DiscardPartials);
        QVERIFY(h.review.requests.back().first > h.review.requests.front().first);
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
        QVERIFY(!h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::Discarded);
        h.binding.poll();
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole)
                    .toString()
                    .contains(QStringLiteral("histórico confirmado")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QVERIFY(h.reviewValue(DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.io.saves.size(), std::size_t{0});
    }
    void discardConsentIsRevokedByCancellationRecheckAndShutdown() {
        Harness h({}, true);
        h.review.discard_supported = true;
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto first = h.binding.discardConsentToken();
        h.binding.cancelDiscardPartialsConfirmation(QStringLiteral("old-2"), first);
        QCOMPARE(h.binding.discardConsentToken(), first);
        h.binding.cancelDiscardPartialsConfirmation(QStringLiteral("old-1"), first);
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), first));
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto second = h.binding.discardConsentToken();
        QVERIFY(second != first);
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-2")));
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), second));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto third = h.binding.discardConsentToken();
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), third));
        QCOMPARE(h.review.requests.size(), std::size_t{2});
    }
    void unavailableDiscardCapabilityNeverOffersDestructiveConsent() {
        Harness h({}, true);
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(!h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), QStringLiteral("1")));
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QCOMPARE(h.review.requests.size(), std::size_t{1});
    }
    void uncertainDiscardUsesMutationSpecificDiagnosticAndDrainsOriginalAction_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("start accepted then throws") << 0;
        QTest::newRow("poll throws") << 1;
        QTest::newRow("wrong action result") << 2;
        QTest::newRow("read-only partial outcome for discard") << 3;
    }
    void uncertainDiscardUsesMutationSpecificDiagnosticAndDrainsOriginalAction() {
        QFETCH(int, fault);
        Harness h({}, true);
        h.review.discard_supported = true;
        h.review.enforce_slot = true;
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto token = h.binding.discardConsentToken();
        h.review.throw_start = fault == 0;
        QCOMPARE(h.binding.confirmDiscardPartials(QStringLiteral("old-1"), token), fault != 0);
        if (fault != 0) {
            h.review.complete(DownloadRecoveryReviewOutcome::Discarded);
            h.review.throw_poll = fault == 1;
            if (fault == 2)
                h.review.completion->action = DownloadRecoveryReviewAction::ReviewFiles;
            if (fault == 3)
                h.review.completion->outcome = DownloadRecoveryReviewOutcome::OwnedPartial;
            h.binding.poll();
        }
        const auto text = h.reviewValue(DownloadJobsModel::ReviewTextRole).toString();
        QVERIFY(text.contains(QStringLiteral("não foi confirmado")));
        QVERIFY(text.contains(QStringLiteral("Reinicie")));
        QVERIFY(!text.contains(QStringLiteral("Tudo foi preservado")));
        QVERIFY(!h.binding.warningText().contains(QStringLiteral("dados foram preservados")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanDiscardPartialsRole).toBool());
        h.review.throw_start = false;
        h.review.throw_poll = false;
        h.review.complete(DownloadRecoveryReviewOutcome::Discarded);
        h.binding.poll();
        QVERIFY(
            h.reviewValue(DownloadJobsModel::ReviewTextRole).toString().contains(QStringLiteral("não foi confirmado")));
        QVERIFY(!h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.execution.starts, 0);
    }
    void consentNotificationsCannotConfirmReentrantlyOrExecuteAfterNestedExit() {
        Harness h({}, true);
        h.review.discard_supported = true;
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        connect(&h.binding, &DownloadApplicationBinding::discardConsentChanged, &h.binding, [&] {
            if (!h.binding.discardConsentToken().isEmpty())
                QVERIFY(!h.binding.confirmDiscardPartials(h.binding.discardConsentJobId(),
                                                          h.binding.discardConsentToken()));
        });
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto token = h.binding.discardConsentToken();
        connect(&h.binding, &DownloadApplicationBinding::discardConsentChanged, &h.binding, [&] {
            if (h.binding.discardConsentToken().isEmpty())
                QVERIFY(h.presenter.requestExit());
        });
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), token));
        QCOMPARE(h.review.requests.size(), std::size_t{1});
        QCOMPARE(h.execution.shutdowns, 1);
        QCOMPARE(h.review.shutdowns, 1);
    }
    void busyDiscardConsumesAssessmentAndRequiresFreshReadOnlyReview() {
        Harness h({}, true);
        h.review.discard_supported = true;
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto token = h.binding.discardConsentToken();
        h.review.command = DownloadRecoveryReviewCommand::Busy;
        QVERIFY(!h.binding.confirmDiscardPartials(QStringLiteral("old-1"), token));
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(!h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        QVERIFY(h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QCOMPARE(h.review.shutdowns, 0);
        h.review.command = DownloadRecoveryReviewCommand::Accepted;
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        QVERIFY(h.binding.discardConsentToken() != token);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.execution.starts, 0);
    }
    void cancellationConsentNotificationResumesNestedShutdown() {
        Harness h({}, true);
        h.review.discard_supported = true;
        QVERIFY(h.interrupted());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QVERIFY(h.binding.requestDiscardPartials(QStringLiteral("old-1")));
        const auto token = h.binding.discardConsentToken();
        connect(&h.binding, &DownloadApplicationBinding::discardConsentChanged, &h.binding, [&] {
            if (h.binding.discardConsentToken().isEmpty())
                QVERIFY(h.presenter.requestExit());
        });
        h.binding.cancelDiscardPartialsConfirmation(QStringLiteral("old-1"), token);
        QVERIFY(h.binding.discardConsentToken().isEmpty());
        QCOMPARE(h.review.requests.size(), std::size_t{1});
        QCOMPARE(h.execution.shutdowns, 1);
        QCOMPARE(h.review.shutdowns, 1);
    }
    void explicitReviewProjectsMatchedOutcomeWithoutChangingTheJobOrPlayback() {
        Harness h({}, true);
        QVERIFY(h.interrupted());
        QCOMPARE(h.review.requests.size(), std::size_t{0});
        QVERIFY(h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        QCOMPARE(h.review.requests.size(), std::size_t{1});
        QCOMPARE(h.review.requests[0].second, std::string{"old-1"});
        QVERIFY(h.review.requests[0].first > 0);
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReviewFilesRole, 1).toBool());
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
        h.binding.poll();
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewBusyRole).toBool());
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        QVERIFY(!h.reviewValue(DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole)
                    .toString()
                    .contains(QStringLiteral("Arquivo final reconhecido")));
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QVERIFY(h.reviewValue(DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-2")));
        QVERIFY(h.review.requests[1].first > h.review.requests[0].first);
        h.review.complete(DownloadRecoveryReviewOutcome::Missing);
        h.binding.poll();
        QVERIFY(
            h.reviewValue(DownloadJobsModel::ReviewTextRole, 1).toString().contains(QStringLiteral("Nenhum arquivo")));
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        h.review.complete(DownloadRecoveryReviewOutcome::Unreadable);
        h.binding.poll();
        QVERIFY(h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole)
                    .toString()
                    .contains(QStringLiteral("Não foi possível verificar")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::ReviewTextRole).toString().contains(QStringLiteral("Reinicie")));
        QCOMPARE(h.review.shutdowns, 0);
    }
    void reviewIsUnavailableWithoutPortOrOutsideExactReadyInterruptedIdentity() {
        Harness absent;
        QVERIFY(absent.interrupted());
        QVERIFY(!absent.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!absent.binding.reviewFiles(QStringLiteral("old-1")));
        Harness h({}, true);
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-1")));
        QVERIFY(h.binding.start());
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-1")));
        h.io.loaded();
        h.binding.poll();
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-1")));
        QVERIFY(h.controller.begin(input()).state == DownloadAdmissionState::Pending);
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("fresh-1")));
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Ready);
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("fresh-1")));
        QCOMPARE(h.review.requests.size(), std::size_t{0});
        Harness restored({}, true);
        QVERIFY(restored.interrupted());
        QVERIFY(!restored.binding.reviewFiles(QStringLiteral("OLD-1")));
        QVERIFY(!restored.binding.reviewFiles(QStringLiteral("old-1\0suffix")));
        QVERIFY(!restored.binding.reviewFiles(QStringLiteral("C:/private/file.mp3")));
        QVERIFY(!restored.binding.reviewFiles(QString{}));
        bool wrong_thread = true;
        std::thread worker([&] { wrong_thread = restored.binding.reviewFiles(QStringLiteral("old-1")); });
        worker.join();
        QVERIFY(!wrong_thread);
        QCOMPARE(restored.review.requests.size(), std::size_t{0});
    }
    void malformedReviewRepliesAndExceptionsFailClosedWithExplicitRecovery_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("wrong operation") << 0;
        QTest::newRow("wrong job") << 1;
        QTest::newRow("unknown outcome") << 2;
        QTest::newRow("poll exception") << 3;
        QTest::newRow("refused start") << 4;
        QTest::newRow("start exception") << 5;
        QTest::newRow("closed start") << 6;
        QTest::newRow("invalid request for known job") << 7;
        QTest::newRow("unknown start result") << 8;
    }
    void malformedReviewRepliesAndExceptionsFailClosedWithExplicitRecovery() {
        QFETCH(int, fault);
        Harness h({}, true);
        QVERIFY(h.interrupted());
        h.review.command = fault == 4   ? DownloadRecoveryReviewCommand::Busy
                           : fault == 6 ? DownloadRecoveryReviewCommand::Closed
                           : fault == 7 ? DownloadRecoveryReviewCommand::InvalidRequest
                           : fault == 8 ? static_cast<DownloadRecoveryReviewCommand>(999)
                                        : DownloadRecoveryReviewCommand::Accepted;
        h.review.throw_start = fault == 5;
        QCOMPARE(h.binding.reviewFiles(QStringLiteral("old-1")), fault < 4);
        if (fault < 4) {
            h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
            if (fault == 0)
                ++h.review.completion->operation_id;
            if (fault == 1)
                h.review.completion->job_id = "old-2";
            if (fault == 2)
                h.review.completion->outcome = static_cast<DownloadRecoveryReviewOutcome>(999);
            h.review.throw_poll = fault == 3;
            h.binding.poll();
        }
        QVERIFY(!h.reviewValue(DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole)
                    .toString()
                    .contains(QStringLiteral("Não foi possível verificar")));
        QCOMPARE(h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool(), fault == 4);
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole, 1).toString().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        h.review.command = DownloadRecoveryReviewCommand::Accepted;
        h.review.throw_start = false;
        h.review.throw_poll = false;
        QCOMPARE(h.binding.reviewFiles(QStringLiteral("old-1")), fault == 4);
        QCOMPARE(h.review.requests.size(), fault == 4 ? std::size_t{2} : std::size_t{1});
    }
    void uncertainReviewPortKeepsTheSlotQuarantinedUntilPhysicalDrain_data() {
        QTest::addColumn<bool>("start_fault");
        QTest::newRow("accepted start then exception") << true;
        QTest::newRow("poll exception retains completion") << false;
    }
    void uncertainReviewPortKeepsTheSlotQuarantinedUntilPhysicalDrain() {
        QFETCH(bool, start_fault);
        Harness h({}, true);
        QVERIFY(h.interrupted());
        h.review.enforce_slot = true;
        h.review.throw_start = start_fault;
        QCOMPARE(h.binding.reviewFiles(QStringLiteral("old-1")), !start_fault);
        if (!start_fault) {
            h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
            h.review.throw_poll = true;
            h.binding.poll();
        }
        QVERIFY(h.review.active_operation.has_value());
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
        QCOMPARE(h.review.requests.size(), std::size_t{1});
        QCOMPARE(h.review.shutdowns, 1);
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole).toString().contains(QStringLiteral("Reinicie")));
        h.review.throw_start = false;
        h.review.throw_poll = false;
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        h.binding.poll();
        QVERIFY(h.reviewValue(DownloadJobsModel::ReviewTextRole).toString().contains(QStringLiteral("Reinicie")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::ReviewTextRole)
                     .toString()
                     .contains(QStringLiteral("Arquivo final reconhecido")));
        QCOMPARE(h.review.polled_ids.back(), h.review.requests[0].first);
        QVERIFY(!h.review.active_operation.has_value());
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-1")));
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(h.presenter.requestExit());
        h.io.saved();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        h.review.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void reviewReentrancyCannotStartOrPollAnotherOperationAndNestedExitClosesReview() {
        Harness h({}, true);
        QVERIFY(h.interrupted());
        h.review.on_start = [&] {
            QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
            h.binding.poll();
        };
        QVERIFY(h.binding.reviewFiles(QStringLiteral("old-1")));
        QCOMPARE(h.review.polled_ids.size(), std::size_t{0});
        h.review.on_poll = [&] {
            h.binding.poll();
            QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
            QVERIFY(h.presenter.requestExit());
        };
        h.review.complete(DownloadRecoveryReviewOutcome::OwnedPartial);
        h.binding.poll();
        QCOMPARE(h.review.polled_ids.size(), std::size_t{1});
        h.review.on_poll = {};
        h.binding.poll();
        QCOMPARE(h.review.shutdowns, 1);
        QVERIFY(!h.binding.reviewFiles(QStringLiteral("old-2")));
        QVERIFY(!h.reviewValue(DownloadJobsModel::CanReviewFilesRole).toBool());
    }
    void exitWaitsForReviewPhysicalDrainEvenWithoutAnActiveReview() {
        Harness h({}, true);
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.review.shutdowns, 1);
        h.io.saved();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        h.review.throw_probe = true;
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        h.review.throw_probe = false;
        h.review.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void startsAndProjectsReconciledHistoryWithoutExecutionOrImport() {
        const SessionLoadResult session{
            .snapshot = {.download_import_receipts = {{.job_id = "old-1", .source_path = path()}}},
            .status = SessionLoadStatus::Loaded};
        Harness h(session);
        QVERIFY(h.binding.start());
        const DownloadCheckpoint checkpoint{
            .receipts = {{.request_id = request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"old-1"},
                          .retained_request_bytes = 128}},
            .jobs = {{.job_id = "old-1", .title = "<b>Literal title</b>", .completed_units = 2, .total_units = 9}}};
        h.io.loaded({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
        h.binding.poll();
        auto* model = h.binding.jobsModel();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->data(model->index(0, 0), DownloadJobsModel::StateRole).toString(), QStringLiteral("imported"));
        QCOMPARE(model->data(model->index(0, 0), DownloadJobsModel::TitleRole).toString(),
                 QStringLiteral("<b>Literal title</b>"));
        QVERIFY(model->data(model->index(0, 0), DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(!h.binding.canRetryStartup());
        QVERIFY(!h.binding.statusText().isEmpty());
        QVERIFY(!h.binding.start());
    }
    void startupFailureAndUnhealthySessionStayExplicitAndNeverRunJobs_data() {
        QTest::addColumn<int>("status");
        QTest::newRow("backup") << static_cast<int>(SessionLoadStatus::RecoveredFromBackup);
        QTest::newRow("invalid") << static_cast<int>(SessionLoadStatus::InvalidUsingDefaults);
        QTest::newRow("future") << static_cast<int>(SessionLoadStatus::FutureVersion);
        QTest::newRow("unknown") << 999;
    }
    void startupFailureAndUnhealthySessionStayExplicitAndNeverRunJobs() {
        QFETCH(int, status);
        Harness h({.status = static_cast<SessionLoadStatus>(status)});
        QVERIFY(h.binding.start());
        h.io.loaded();
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Blocked);
        QVERIFY(h.binding.warningText().contains(QStringLiteral("sessão")));
        QVERIFY(!h.binding.canRetryStartup());
        const auto loads = h.io.loads.size();
        QVERIFY(!h.binding.retryStartup());
        QCOMPARE(h.io.loads.size(), loads);
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Blocked);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(!h.binding.retryAdmission());
        QVERIFY(!h.binding.retryFinalSave());
        QVERIFY(h.io.saves.empty());
    }
    void checkpointReadFailureStillOffersExplicitRetryWithHealthySession() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded({.status = DownloadCheckpointLoadStatus::Unreadable});
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Blocked);
        QVERIFY(h.binding.canRetryStartup());
        QVERIFY(h.binding.retryStartup());
        QCOMPARE(h.io.loads.size(), std::size_t{2});
        h.io.loaded();
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Ready);
        QVERIFY(!h.binding.canRetryStartup());
        QVERIFY(h.binding.warningText().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(h.io.saves.empty());
    }
    void admissionFailureRetriesTheSameCheckpointBeforeStartingExecution() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.controller.begin(input()).state == DownloadAdmissionState::Pending);
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        QVERIFY(h.binding.canRetryAdmission());
        QVERIFY(!h.binding.warningText().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        const auto first = h.io.saves.back().second;
        QVERIFY(h.binding.retryAdmission());
        QCOMPARE(h.io.saves.back().second, first);
        QVERIFY(!h.binding.canRetryAdmission());
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
        QVERIFY(!h.binding.warningText().contains(QStringLiteral("never-display")));
        QCOMPARE(h.binding.jobsModel()->rowCount(), 1);
    }
    void exitNeedsSavedFinalHistoryAndBothPhysicalDrains() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.execution.shutdowns, 1);
        QCOMPARE(h.io.saves.size(), std::size_t{1});
        h.io.saved();
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(h.presenter.exiting());
        h.io.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void physicalWorkerDrainAndProbeFailureRemainVisibleUntilAHealthyObservation() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        h.io.saved();
        h.io.physical_stopped = true;
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Stopped);
        QCOMPARE(h.binding.statusText(), QStringLiteral("Finalizando encerramento dos downloads"));
        h.execution.throw_probe = true;
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        QCOMPARE(h.platform.exits, 0);
        h.execution.throw_probe = false;
        h.execution.physical_stopped = true;
        h.binding.poll();
        QVERIFY(h.binding.warningText().isEmpty());
        QCOMPARE(h.binding.statusText(), QStringLiteral("Downloads encerrados"));
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void projectionSignalsCannotStackAnotherControllerOrClockPoll() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded();
        connect(&h.binding, &DownloadApplicationBinding::stateChanged, &h.binding, [&] { h.binding.poll(); });
        h.binding.poll();
        QCOMPARE(h.clock_reads, 1);
        auto* model = h.binding.jobsModel();
        connect(model, &QAbstractItemModel::rowsInserted, &h.binding, [&] { h.binding.poll(); });
        QVERIFY(h.controller.begin(input()).state == DownloadAdmissionState::Pending);
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.clock_reads, 2);
        QCOMPARE(h.execution.starts, 1);
    }
    void finalSaveFailureReopensLocalIntentAndKeepsIoOpenForExplicitRetry() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.exiting());
        QCOMPARE(h.io.shutdowns, 0);
        QVERIFY(h.binding.canRetryFinalSave());
        QVERIFY(h.binding.canCloseAfterFinalSaveFailure());
        QVERIFY(!h.binding.canRetryAdmission());
        const auto failed_snapshot = h.io.saves.back().second;
        QVERIFY(h.binding.retryFinalSave());
        QCOMPARE(h.io.saves.back().second, failed_snapshot);
        h.io.saved();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 1);
        QCOMPARE(h.controller.begin(input()).result.error, DownloadSubmissionError::NotAvailable);
    }
    void stoppedPlusFailedIsNotReadyWithoutAnExplicitAcceptedDecision() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(!h.binding.closeAfterFinalSaveFailure());
        QVERIFY(h.presenter.requestExit());
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        h.playerPoll();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        // Bypassing the binding's explicit decision must not silently authorise exit.
        QVERIFY(h.controller.closeAfterFinalSaveFailure());
        h.binding.poll();
        static_cast<void>(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.exitCommitted());
    }
    void explicitCloseAfterFailureStillRequiresPhysicalDrain() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        h.playerPoll();
        QVERIFY(h.binding.closeAfterFinalSaveFailure());
        QVERIFY(!h.binding.closeAfterFinalSaveFailure());
        QVERIFY(h.presenter.requestExit());
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void blockedStartupPreservesHistoryAndCanExitAfterDrainWithoutWriting() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded({.status = DownloadCheckpointLoadStatus::FutureVersion});
        h.binding.poll();
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.io.saves.size(), std::size_t{0});
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void reentrantPollingAndWrongThreadControlsDoNotAdvanceBorrowedWork() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded();
        h.io.on_poll = [&] {
            h.binding.poll();
            QVERIFY(!h.binding.start());
            QVERIFY(!h.binding.retryStartup());
        };
        h.binding.poll();
        QCOMPARE(h.io.polls, 1);
        int previous = h.io.polls;
        bool result = true;
        std::thread worker([&] {
            h.binding.poll();
            result = h.binding.retryStartup();
        });
        worker.join();
        QVERIFY(!result);
        QCOMPARE(h.io.polls, previous);
    }
    void nestedImportExitClosesTheSameTickSuccessorStart() {
        Harness h;
        QVERIFY(h.ready());
        auto selected = input();
        selected.items.push_back({.url = "https://synthetic.example.test/second.mp3",
                                  .title = "Second synthetic item",
                                  .kind = DownloadMediaKind::Audio});
        QVERIFY(h.controller.begin(selected).state == DownloadAdmissionState::Pending);
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
        h.importer.pending = true;
        h.execution.update.state = DownloadExecutionState::Published;
        h.execution.update.phase = DownloadPhase::Publishing;
        h.execution.update.published_mp3 = path();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.execution.starts, 1);
        h.importer.polled = DownloadImportState::Committed;
        h.importer.on_poll = [&] { QVERIFY(h.presenter.requestExit()); };
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
        QCOMPARE(h.controller.begin(input()).result.error, DownloadSubmissionError::NotAvailable);
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
    }
    void clockAndExecutionProbeExceptionsNeverAcknowledgeExit() {
        Harness h;
        QVERIFY(h.ready());
        h.throw_clock = true;
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        static_cast<void>(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        h.throw_clock = false;
        h.execution.throw_probe = true;
        h.execution.physical_stopped = true;
        h.io.physical_stopped = true;
        if (!h.io.saves.empty())
            h.io.saved();
        h.binding.poll();
        static_cast<void>(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.exitCommitted());
    }
    void secondBindingCannotReplaceTheRegisteredParticipant() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded();
        h.binding.poll();
        // A second owner cannot replace a registered participant.
        DownloadApplicationBinding second(h.presenter, h.controller, h.service,
                                          {.shutdown = [] {}, .stopped = [] { return true; }},
                                          {.automatic_polling = false});
        QVERIFY(!second.start());
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.setExitPreparationCallbacks({}));
    }
    void destroyedBindingLeavesAnExpiredUsedGateFailedInsteadOfBypassingDrain() {
        Harness h;
        PlayerPresenter presenter(
            FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE)),
            h.platform);
        auto binding = std::make_unique<DownloadApplicationBinding>(
            presenter, h.controller, h.service,
            DownloadExecutionDrainCallbacks{.shutdown = [&] { h.execution.shutdown(); },
                                            .stopped = [&] { return h.execution.stopped(); }},
            DownloadApplicationBindingOptions{.automatic_polling = false});
        QVERIFY(binding->start());
        h.io.loaded();
        binding->poll();
        QVERIFY(presenter.requestExit());
        binding.reset();
        PlayerPresenterTestAccess::pollPersistence(presenter);
        QVERIFY(!presenter.exiting());
        QVERIFY(!presenter.exitCommitted());
        QVERIFY(!presenter.setExitPreparationCallbacks({}));
        static_cast<void>(presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
    }
};
} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::DownloadApplicationBindingTests)
#include "DownloadApplicationBindingTests.moc"
