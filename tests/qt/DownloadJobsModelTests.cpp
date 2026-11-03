#include "adapters/DownloadJobsModel.hpp"
#include "m0/domain/SessionSnapshot.hpp"

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

using namespace music_player::m0;

namespace {

const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";
const std::string other_request_id = "01234567-89ab-4cde-8fab-0123456789ac";

class Execution final : public DownloadExecution {
  public:
    int starts{0};
    int polls{0};
    int cancellations{0};
    std::string active_id{};
    std::string cancelled_id{};
    DownloadExecutionUpdate update{};

    bool start(const std::string& job_id, const DownloadItem&) override {
        ++starts;
        active_id = job_id;
        update.job_id = job_id;
        return true;
    }
    void cancel(std::string_view job_id) override {
        ++cancellations;
        cancelled_id = job_id;
    }
    DownloadExecutionUpdate poll(std::string_view) override {
        ++polls;
        return update;
    }
};

class Importer final : public DownloadImport {
  public:
    int calls{0};
    int polls{0};
    bool accepted{true};
    std::string last_id{};
    std::filesystem::path last_path{};
    bool pending{false};
    DownloadImportState polled{DownloadImportState::Pending};

    bool accept(const std::string& job_id, const std::filesystem::path& path) override {
        ++calls;
        last_id = job_id;
        last_path = path;
        return accepted;
    }
    DownloadImportState begin(const std::string& job_id, const std::filesystem::path& path) override {
        if (!pending)
            return DownloadImport::begin(job_id, path);
        static_cast<void>(accept(job_id, path));
        return DownloadImportState::Pending;
    }
    DownloadImportState pollImport(std::string_view) override {
        ++polls;
        return polled;
    }
};

DownloadRequest request(std::string id = request_id, std::string title = "Audio") {
    return {.request_id = std::move(id),
            .items = {{.url = "https://media.example.test/music.mp3?secret=private-token",
                       .title = std::move(title),
                       .kind = DownloadMediaKind::Audio}}};
}
class ReviewIdentity final : public DownloadRequestIdentity {
  public:
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};

QVariant value(const DownloadJobsModel& model, int role, int row = 0) { return model.data(model.index(row, 0), role); }

std::filesystem::path finalPath() { return std::filesystem::path(L"C:\\RC Music synthetic downloads\\Música.mp3"); }

struct RecoveryModelHarness final {
    ReviewIdentity identity;
    Execution execution;
    Importer importer;
    DownloadService service{execution, importer, "fresh", {}, &identity};
    bool restored = service.restore(
        {.receipts = {{.request_id = request_id,
                       .fingerprint = std::string(64, 'a'),
                       .job_ids = {"previous-1", "previous-2"},
                       .retained_request_bytes = 128}},
         .jobs = {{.job_id = "previous-1", .title = "First"}, {.job_id = "previous-2", .title = "Second"}}});
    DownloadJobsModel model{service};
    bool reviewed(const QString& id, DownloadRecoveryReviewOutcome outcome) {
        model.setReviewAvailable(true);
        return model.beginReview(id) && model.finishReview(id, outcome);
    }
};

} // namespace

class DownloadJobsModelTests final : public QObject {
    Q_OBJECT

  private slots:
    void validationRequiresAvailableCurrentFinalAssessmentAndExactInterruptedIdentity() {
        RecoveryModelHarness h;
        QVERIFY(h.restored);
        QVERIFY(!value(h.model, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!h.model.beginValidation(QStringLiteral("previous-1")));
        h.model.setValidationAvailable(true);
        QVERIFY(!h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedPartial));
        QVERIFY(!value(h.model, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        QVERIFY(value(h.model, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!h.model.beginValidation(QStringLiteral("PREVIOUS-1")));
        QVERIFY(!h.model.beginValidation(QStringLiteral("unknown")));
        h.model.setValidationAvailable(false);
        QVERIFY(!h.model.beginValidation(QStringLiteral("previous-1")));
        h.model.setValidationAvailable(true);
        QVERIFY(h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(value(h.model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(h.service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
    }
    void validationHoldsTheGlobalSlotUntilMatchingReleaseAndNeverImports() {
        RecoveryModelHarness h;
        QVERIFY(h.restored);
        QAbstractItemModelTester tester(&h.model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        QVERIFY(h.reviewed(QStringLiteral("previous-2"), DownloadRecoveryReviewOutcome::OwnedPartial));
        h.model.setDiscardSupported(true);
        h.model.setValidationAvailable(true);
        const auto before = h.service.snapshot();
        QVERIFY(value(h.model, DownloadJobsModel::CanDiscardPartialsRole, 1).toBool());
        QVERIFY(h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(value(h.model, DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(value(h.model, DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(!value(h.model, DownloadJobsModel::CanReleaseValidationRole, 1).toBool());
        QVERIFY(!value(h.model, DownloadJobsModel::CanReviewFilesRole, 1).toBool());
        QVERIFY(!value(h.model, DownloadJobsModel::CanDiscardPartialsRole, 1).toBool());
        QVERIFY(!h.model.beginReview(QStringLiteral("previous-2")));
        QVERIFY(!h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(!h.model.finishValidation(QStringLiteral("previous-2"), DownloadRecoveryValidationOutcome::Validated));
        QVERIFY(h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Validated));
        QVERIFY(!value(h.model, DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(value(h.model, DownloadJobsModel::CanReleaseValidationRole).toBool());
        const auto text = value(h.model, DownloadJobsModel::ValidationTextRole).toString();
        QVERIFY(text.contains(QStringLiteral("MP3 validado")));
        QVERIFY(text.contains(QStringLiteral("não foi importado")));
        QVERIFY(value(h.model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(!h.model.beginReview(QStringLiteral("previous-1")));
        QVERIFY(!h.model.beginValidationRelease(QStringLiteral("previous-2")));
        QVERIFY(h.model.beginValidationRelease(QStringLiteral("previous-1")));
        QVERIFY(value(h.model, DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(!value(h.model, DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(!h.model.beginValidationRelease(QStringLiteral("previous-1")));
        QVERIFY(!h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Validated));
        QVERIFY(h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Released));
        QVERIFY(!value(h.model, DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(value(h.model, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(value(h.model, DownloadJobsModel::CanReviewFilesRole, 1).toBool());
        QVERIFY(value(h.model, DownloadJobsModel::CanDiscardPartialsRole, 1).toBool());
        QCOMPARE(h.service.snapshot(), before);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.execution.polls, 0);
        QCOMPARE(h.execution.cancellations, 0);
        QCOMPARE(h.importer.calls, 0);
    }
    void validationFailuresAreTypedPrivateAndReleaseTheModelSlot_data() {
        QTest::addColumn<int>("outcome");
        for (auto outcome :
             {DownloadRecoveryValidationOutcome::Unverified, DownloadRecoveryValidationOutcome::OwnedPartial,
              DownloadRecoveryValidationOutcome::Missing, DownloadRecoveryValidationOutcome::Conflict,
              DownloadRecoveryValidationOutcome::Unreadable, DownloadRecoveryValidationOutcome::HistoryMissing,
              DownloadRecoveryValidationOutcome::HistoryInvalid, DownloadRecoveryValidationOutcome::HistoryFuture,
              DownloadRecoveryValidationOutcome::InvalidMedia, DownloadRecoveryValidationOutcome::Timeout})
            QTest::newRow(qPrintable(QString::number(int(outcome)))) << int(outcome);
        QTest::newRow("unknown") << 999;
    }
    void validationFailuresAreTypedPrivateAndReleaseTheModelSlot() {
        QFETCH(int, outcome);
        RecoveryModelHarness h;
        QVERIFY(h.restored);
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        h.model.setValidationAvailable(true);
        QVERIFY(h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(h.model.finishValidation(QStringLiteral("previous-1"),
                                         static_cast<DownloadRecoveryValidationOutcome>(outcome)));
        const auto text = value(h.model, DownloadJobsModel::ValidationTextRole).toString();
        QVERIFY(!text.isEmpty());
        QVERIFY(!text.contains(QStringLiteral("https:")));
        QVERIFY(!text.contains(QStringLiteral("C:/")));
        QVERIFY(!text.contains(QStringLiteral("MP3 validado")));
        QVERIFY(!value(h.model, DownloadJobsModel::ValidationBusyRole).toBool());
        QVERIFY(!value(h.model, DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(value(h.model, DownloadJobsModel::CanReviewFilesRole).toBool());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
    }
    void validationCanBeReleasedWhilePendingAndCannotRaceAnotherReview() {
        RecoveryModelHarness h;
        QVERIFY(h.restored);
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        h.model.setValidationAvailable(true);
        QVERIFY(h.model.beginReview(QStringLiteral("previous-2")));
        QVERIFY(!h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(h.model.finishReview(QStringLiteral("previous-2"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        QVERIFY(h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(!h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Released));
        QVERIFY(h.model.beginValidationRelease(QStringLiteral("previous-1")));
        QVERIFY(!h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Validated));
        QVERIFY(h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Released));
        QVERIFY(!value(h.model, DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(
            !value(h.model, DownloadJobsModel::ValidationTextRole).toString().contains(QStringLiteral("MP3 validado")));
    }
    void validationUnavailableIsLatchedAndNeverPreservesAClaimOfValidatedMedia() {
        RecoveryModelHarness h;
        QVERIFY(h.restored);
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        h.model.setValidationAvailable(true);
        QVERIFY(h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Validated));
        h.model.setValidationUnavailable();
        h.model.setValidationAvailable(true);
        QVERIFY(!value(h.model, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!value(h.model, DownloadJobsModel::CanReleaseValidationRole).toBool());
        QVERIFY(!h.model.beginValidationRelease(QStringLiteral("previous-1")));
        QVERIFY(!h.model.beginReview(QStringLiteral("previous-2")));
        QVERIFY(!h.model.finishValidation(QStringLiteral("previous-1"), DownloadRecoveryValidationOutcome::Validated));
        const auto text = value(h.model, DownloadJobsModel::ValidationTextRole).toString();
        QVERIFY(text.contains(QStringLiteral("interrompida")));
        QVERIFY(!text.contains(QStringLiteral("MP3 validado")));
        QVERIFY(value(h.model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
    }
    void validationSignalsObserveFrozenControlsBeforeReentrantRequests() {
        RecoveryModelHarness h;
        QVERIFY(h.restored);
        QVERIFY(h.reviewed(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        h.model.setValidationAvailable(true);
        bool observed = false;
        connect(&h.model, &QAbstractItemModel::dataChanged, &h.model, [&] {
            if (!value(h.model, DownloadJobsModel::ValidationBusyRole).toBool() || observed)
                return;
            observed = true;
            QVERIFY(!value(h.model, DownloadJobsModel::CanValidateMediaRole).toBool());
            QVERIFY(!value(h.model, DownloadJobsModel::CanReviewFilesRole, 1).toBool());
            QVERIFY(!h.model.beginValidation(QStringLiteral("previous-1")));
            QVERIFY(!h.model.beginReview(QStringLiteral("previous-2")));
        });
        QVERIFY(h.model.beginValidation(QStringLiteral("previous-1")));
        QVERIFY(observed);
    }
    void discardCapabilityNeedsOwnPartialsAndConsumesTheirOldAssessment() {
        ReviewIdentity identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        QVERIFY(service.restore({.receipts = {{.request_id = request_id,
                                               .fingerprint = std::string(64, 'a'),
                                               .job_ids = {"previous-1"},
                                               .retained_request_bytes = 128}},
                                 .jobs = {{.job_id = "previous-1", .title = "First"}}}));
        DownloadJobsModel model(service);
        model.setReviewAvailable(true);
        model.setDiscardSupported(true);
        QVERIFY(!value(model, DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(model.beginReview(QStringLiteral("previous-1")));
        QVERIFY(model.finishReview(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedPartial));
        QVERIFY(value(model, DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(model.beginReview(QStringLiteral("previous-1"), DownloadRecoveryReviewAction::DiscardPartials));
        QVERIFY(!value(model, DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::ReviewTextRole).toString().contains(QStringLiteral("Descartando")));
        QVERIFY(model.finishReview(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::Discarded));
        QVERIFY(!value(model, DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::ReviewTextRole)
                    .toString()
                    .contains(QStringLiteral("histórico confirmado")));
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(service.snapshot()[0].state, DownloadJobState::Interrupted);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.calls, 0);
    }
    void discardOutcomesDoNotConflateDeletionAndDurability_data() {
        QTest::addColumn<int>("outcome");
        QTest::addColumn<QString>("expected");
        QTest::newRow("confirmed") << int(DownloadRecoveryReviewOutcome::Discarded)
                                   << QStringLiteral("histórico confirmado");
        QTest::newRow("incomplete") << int(DownloadRecoveryReviewOutcome::DiscardIncomplete)
                                    << QStringLiteral("não foi concluído");
        QTest::newRow("unconfirmed") << int(DownloadRecoveryReviewOutcome::DiscardUnconfirmed)
                                     << QStringLiteral("não foi confirmado");
    }
    void discardOutcomesDoNotConflateDeletionAndDurability() {
        QFETCH(int, outcome);
        QFETCH(QString, expected);
        ReviewIdentity identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        QVERIFY(service.restore({.receipts = {{.request_id = request_id,
                                               .fingerprint = std::string(64, 'a'),
                                               .job_ids = {"previous-1"},
                                               .retained_request_bytes = 128}},
                                 .jobs = {{.job_id = "previous-1", .title = "First"}}}));
        DownloadJobsModel model(service);
        model.setReviewAvailable(true);
        QVERIFY(model.beginReview(QStringLiteral("previous-1")));
        QVERIFY(model.finishReview(QStringLiteral("previous-1"), static_cast<DownloadRecoveryReviewOutcome>(outcome)));
        QVERIFY(value(model, DownloadJobsModel::ReviewTextRole).toString().contains(expected));
        QVERIFY(!value(model, DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
    }
    void interruptedReviewProjectionIsExplicitBoundedAndDoesNotExposeFileAuthority() {
        ReviewIdentity identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        const DownloadCheckpoint checkpoint{
            .receipts = {{.request_id = request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"previous-1", "previous-2"},
                          .retained_request_bytes = 128}},
            .jobs = {{.job_id = "previous-1", .title = "First"}, {.job_id = "previous-2", .title = "Second"}}};
        QVERIFY(service.restore(checkpoint));
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        const QPersistentModelIndex selected(model.index(0, 0));
        QVERIFY(!value(model, DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!model.beginReview(QStringLiteral("previous-1")));
        model.setReviewAvailable(true);
        QVERIFY(value(model, DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!model.beginReview(QStringLiteral("PREVIOUS-1")));
        QVERIFY(model.beginReview(QStringLiteral("previous-1")));
        QVERIFY(value(model, DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanReviewFilesRole, 1).toBool());
        QVERIFY(!model.beginReview(QStringLiteral("previous-2")));
        QVERIFY(!model.finishReview(QStringLiteral("previous-2"), DownloadRecoveryReviewOutcome::OwnedPartial));
        QVERIFY(model.finishReview(QStringLiteral("previous-1"), DownloadRecoveryReviewOutcome::OwnedFinalCandidate));
        QVERIFY(value(model, DownloadJobsModel::ReviewTextRole)
                    .toString()
                    .contains(QStringLiteral("Arquivo final reconhecido")));
        QVERIFY(value(model, DownloadJobsModel::CanReviewFilesRole, 1).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(service.snapshot()[0].state, DownloadJobState::Interrupted);
        model.refresh();
        QVERIFY(!value(model, DownloadJobsModel::ReviewTextRole).toString().isEmpty());
        model.setReviewAvailable(false);
        QVERIFY(!value(model, DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::ReviewTextRole).toString().isEmpty());
        QVERIFY(changed.size() > 0);
        QCOMPARE(reset.size(), 0);
        QVERIFY(selected.isValid());
        QCOMPARE(selected.row(), 0);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.calls, 0);
    }
    void reviewOutcomesGiveConservativeSafeGuidance_data() {
        QTest::addColumn<int>("outcome");
        for (auto outcome :
             {DownloadRecoveryReviewOutcome::Unverified, DownloadRecoveryReviewOutcome::OwnedPartial,
              DownloadRecoveryReviewOutcome::OwnedFinalCandidate, DownloadRecoveryReviewOutcome::Missing,
              DownloadRecoveryReviewOutcome::Conflict, DownloadRecoveryReviewOutcome::Unreadable,
              DownloadRecoveryReviewOutcome::HistoryMissing, DownloadRecoveryReviewOutcome::HistoryInvalid,
              DownloadRecoveryReviewOutcome::HistoryFuture})
            QTest::newRow(qPrintable(QString::number(static_cast<int>(outcome)))) << static_cast<int>(outcome);
        QTest::newRow("unknown") << 999;
    }
    void reviewOutcomesGiveConservativeSafeGuidance() {
        QFETCH(int, outcome);
        ReviewIdentity identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        QVERIFY(service.restore({.receipts = {{.request_id = request_id,
                                               .fingerprint = std::string(64, 'a'),
                                               .job_ids = {"previous-1"},
                                               .retained_request_bytes = 128}},
                                 .jobs = {{.job_id = "previous-1", .title = "First"}}}));
        DownloadJobsModel model(service);
        model.setReviewAvailable(true);
        QVERIFY(model.beginReview(QStringLiteral("previous-1")));
        QVERIFY(model.finishReview(QStringLiteral("previous-1"), static_cast<DownloadRecoveryReviewOutcome>(outcome)));
        const auto text = value(model, DownloadJobsModel::ReviewTextRole).toString();
        QVERIFY(!text.isEmpty());
        QVERIFY(!text.contains(QStringLiteral("C:/")));
        QVERIFY(!text.contains(QStringLiteral("https:")));
        QVERIFY(!value(model, DownloadJobsModel::ReviewBusyRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(execution.polls, 0);
        QCOMPARE(importer.calls, 0);
    }
    void reconciledImportProjectsDurableCompletionWithoutInventingTransferMetrics_data() {
        QTest::addColumn<bool>("known_total");
        QTest::newRow("partial transfer metrics") << true;
        QTest::newRow("unknown transfer total") << false;
    }

    void reconciledImportProjectsDurableCompletionWithoutInventingTransferMetrics() {
        QFETCH(bool, known_total);
        class Identity final : public DownloadRequestIdentity {
          public:
            std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
        } identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        const std::string title = "<img src=x onerror=alert(1)> & música 日本";
        const DownloadCheckpoint checkpoint{
            .receipts = {{.request_id = request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"previous-1"},
                          .retained_request_bytes = 128}},
            .jobs = {{.job_id = "previous-1",
                      .title = title,
                      .state = DownloadJobState::Queued,
                      .completed_units = 2,
                      .total_units = known_total ? std::optional<std::uint64_t>{9} : std::nullopt}}};
        const std::vector<DownloadImportReceipt> receipts{{.job_id = "previous-1", .source_path = finalPath()}};
        QVERIFY(service.restore(checkpoint, receipts));
        QVERIFY(service.snapshot()[0].state == DownloadJobState::Imported);
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy summary(&model, &DownloadJobsModel::summaryChanged);
        QCOMPARE(value(model, DownloadJobsModel::JobIdRole).toString(), QStringLiteral("previous-1"));
        QCOMPARE(value(model, DownloadJobsModel::TitleRole).toString(), QString::fromStdString(title));
        QCOMPARE(value(model, Qt::DisplayRole).toString(), QString::fromStdString(title));
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("imported"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Já importado na fila"));
        QCOMPARE(value(model, DownloadJobsModel::CompletedUnitsRole).toULongLong(), qulonglong{2});
        if (known_total)
            QCOMPARE(value(model, DownloadJobsModel::TotalUnitsRole).toULongLong(), qulonglong{9});
        else
            QVERIFY(!value(model, DownloadJobsModel::TotalUnitsRole).isValid());
        QCOMPARE(value(model, DownloadJobsModel::ProgressRatioRole).toDouble(), 1.0);
        QVERIFY(!value(model, DownloadJobsModel::IndeterminateRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(value(model, DownloadJobsModel::ErrorTextRole).toString().isEmpty());
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.completedCount(), 1);
        QVERIFY(!model.cancel(QStringLiteral("previous-1")));
        QVERIFY(!model.retryImport(QStringLiteral("previous-1")));
        service.tick(0);
        model.refresh();
        QCOMPARE(changed.size(), 0);
        QCOMPARE(summary.size(), 0);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(execution.polls, 0);
        QCOMPARE(execution.cancellations, 0);
        QCOMPARE(importer.calls, 0);
        QCOMPARE(importer.polls, 0);
    }

    void completedCheckpointWithoutSessionReceiptProjectsInterruptedReviewOnly() {
        class Identity final : public DownloadRequestIdentity {
          public:
            std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
        } identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        const DownloadCheckpoint checkpoint{.receipts = {{.request_id = request_id,
                                                          .fingerprint = std::string(64, 'a'),
                                                          .job_ids = {"previous-1"},
                                                          .retained_request_bytes = 128}},
                                            .jobs = {{.job_id = "previous-1",
                                                      .title = "Synthetic title",
                                                      .state = DownloadJobState::Completed,
                                                      .completed_units = 9,
                                                      .total_units = 9,
                                                      .completed_mp3 = finalPath()}}};
        QVERIFY(service.restore(checkpoint, std::span<const DownloadImportReceipt>{}));
        QVERIFY(service.snapshot()[0].state == DownloadJobState::Interrupted);
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("interrupted"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(),
                 QStringLiteral("Interrompido; revisão necessária"));
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.completedCount(), 0);
        QVERIFY(!model.cancel(QStringLiteral("previous-1")));
        QVERIFY(!model.retryImport(QStringLiteral("previous-1")));
        service.tick(0);
        model.refresh();
        QCOMPARE(execution.starts, 0);
        QCOMPARE(execution.polls, 0);
        QCOMPARE(execution.cancellations, 0);
        QCOMPARE(importer.calls, 0);
        QCOMPARE(importer.polls, 0);
    }

    void pendingImportShowsPersistenceWaitWithoutCancelRetryOrFalseCompletion() {
        Execution execution;
        Importer importer;
        importer.pending = true;
        DownloadService service(execution, importer, "model");
        const auto receipt = service.submit(request());
        DownloadJobsModel model(service);
        service.tick(0);
        execution.update = {.job_id = receipt.job_ids[0],
                            .state = DownloadExecutionState::Published,
                            .phase = DownloadPhase::Publishing,
                            .published_mp3 = finalPath()};
        service.tick(1);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("importing"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(),
                 QStringLiteral("Confirmando importação na fila"));
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.completedCount(), 0);
        QCOMPARE(value(model, DownloadJobsModel::ProgressRatioRole).toDouble(), 1.0);
        QVERIFY(!value(model, DownloadJobsModel::IndeterminateRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::ErrorTextRole).toString().isEmpty());
        QCOMPARE(value(model, DownloadJobsModel::OutputPathRole).toString(),
                 QString::fromStdWString(finalPath().wstring()));
        QVERIFY(!model.cancel(QString::fromStdString(receipt.job_ids[0])));
        QVERIFY(!model.retryImport(QString::fromStdString(receipt.job_ids[0])));
        importer.polled = DownloadImportState::Committed;
        service.tick(2);
        model.refresh();
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.completedCount(), 1);
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("completed"));
        QCOMPARE(importer.calls, 1);
        QCOMPARE(execution.starts, 1);
        QCOMPARE(execution.cancellations, 0);
    }
    void recoveredJobsExposeReviewStateWithoutExecutableActionsOrCandidatePath() {
        class Identity final : public DownloadRequestIdentity {
          public:
            std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
        } identity;
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "fresh", {}, &identity);
        DownloadCheckpoint checkpoint{.receipts = {{.request_id = request_id,
                                                    .fingerprint = std::string(64, 'a'),
                                                    .job_ids = {"previous-1"},
                                                    .retained_request_bytes = 128}},
                                      .jobs = {{.job_id = "previous-1",
                                                .title = "Synthetic title",
                                                .state = DownloadJobState::ImportFailed,
                                                .completed_units = 1,
                                                .total_units = 1,
                                                .completed_mp3 = finalPath(),
                                                .error = DownloadFailure::Import}}};
        QVERIFY(service.restore(checkpoint));
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("interrupted"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(),
                 QStringLiteral("Interrompido; revisão necessária"));
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(!model.retryImport(QStringLiteral("previous-1")));
        QVERIFY(!model.cancel(QStringLiteral("previous-1")));
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.calls, 0);
    }
    void emptyProjectionHasOnlySafePublicRolesAndNoSideEffects() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto roles = model.roleNames();
        const QList<QByteArray> expected{"jobId",
                                         "title",
                                         "state",
                                         "statusText",
                                         "completedUnits",
                                         "totalUnits",
                                         "indeterminate",
                                         "progressRatio",
                                         "outputPath",
                                         "errorText",
                                         "canCancel",
                                         "canRetryImport",
                                         "canReviewFiles",
                                         "reviewText",
                                         "reviewBusy",
                                         "canDiscardPartials",
                                         "canValidateMedia",
                                         "validationText",
                                         "validationBusy",
                                         "canReleaseValidation"};
        QCOMPARE(roles.size(), expected.size());
        for (const auto& role : expected)
            QVERIFY(roles.values().contains(role));
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.completedCount(), 0);
        QVERIFY(!model.data({}).isValid());
        model.refresh();
        QCOMPARE(execution.starts, 0);
        QCOMPARE(execution.polls, 0);
        QCOMPARE(execution.cancellations, 0);
        QCOMPARE(importer.calls, 0);
    }

    void projectsExistingJobsWithoutStartingExecution() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        const auto receipt = service.submit(request());
        QCOMPARE(receipt.error, DownloadSubmissionError::None);
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.completedCount(), 0);
        QCOMPARE(value(model, DownloadJobsModel::JobIdRole).toString(), QString::fromStdString(receipt.job_ids[0]));
        QCOMPARE(value(model, DownloadJobsModel::TitleRole).toString(), QStringLiteral("Audio"));
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("queued"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Na fila"));
        QVERIFY(value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(execution.starts, 0);
        QCOMPARE(execution.polls, 0);
        QCOMPARE(importer.calls, 0);
    }

    void refreshAppendsAndProgressDoesNotResetRowsOrFocus() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy summary(&model, &DownloadJobsModel::summaryChanged);
        QCOMPARE(service.submit(request()).error, DownloadSubmissionError::None);
        model.refresh();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(summary.size(), 1);
        const QPersistentModelIndex selected(model.index(0, 0));
        const auto selected_id = selected.data(DownloadJobsModel::JobIdRole);
        QCOMPARE(service.submit(request(other_request_id, "Second")).error, DownloadSubmissionError::None);
        model.refresh();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(inserted.size(), 2);
        QCOMPARE(summary.size(), 2);
        service.tick(0);
        model.refresh();
        execution.update.completed_units = 25;
        execution.update.total_units = 100;
        service.tick(1);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("downloading"));
        QCOMPARE(value(model, DownloadJobsModel::CompletedUnitsRole).toULongLong(), qulonglong{25});
        QCOMPARE(value(model, DownloadJobsModel::TotalUnitsRole).toULongLong(), qulonglong{100});
        QCOMPARE(value(model, DownloadJobsModel::ProgressRatioRole).toDouble(), 0.25);
        QVERIFY(!value(model, DownloadJobsModel::IndeterminateRole).toBool());
        QVERIFY(changed.size() >= 1);
        QCOMPARE(summary.size(), 2);
        QCOMPARE(reset.size(), 0);
        QVERIFY(selected.isValid());
        QCOMPARE(selected.row(), 0);
        QCOMPARE(selected.data(DownloadJobsModel::JobIdRole), selected_id);
        QCOMPARE(value(model, DownloadJobsModel::TitleRole, 1).toString(), QStringLiteral("Second"));
        const auto changes = changed.size();
        model.refresh();
        QCOMPARE(changed.size(), changes);
        QCOMPARE(inserted.size(), 2);
        QCOMPARE(summary.size(), 2);
    }

    void reentrantActionFromChangedSignalDoesNotRestoreStaleJobState() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        auto input = request();
        input.items.push_back(input.items[0]);
        const auto receipt = service.submit(input);
        QCOMPARE(receipt.error, DownloadSubmissionError::None);
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QPersistentModelIndex selected(model.index(1, 0));
        const auto second_id = QString::fromStdString(receipt.job_ids[1]);
        bool attempted = false;
        bool cancelled = false;
        QObject::connect(&model, &QAbstractItemModel::dataChanged, &model, [&] {
            model.refresh();
            if (!attempted) {
                attempted = true;
                cancelled = model.cancel(second_id);
            }
        });
        service.tick(0);
        model.refresh();
        QVERIFY(attempted);
        QVERIFY(cancelled);
        QCOMPARE(service.snapshot()[1].state, DownloadJobState::Cancelled);
        QCOMPARE(value(model, DownloadJobsModel::StateRole, 1).toString(), QStringLiteral("cancelled"));
        QCOMPARE(model.activeCount(), 1);
        QVERIFY(selected.isValid());
        QCOMPARE(selected.row(), 1);
        QCOMPARE(selected.data(DownloadJobsModel::JobIdRole).toString(), second_id);
        QCOMPARE(execution.starts, 1);
        QCOMPARE(execution.cancellations, 0);
    }

    void reentrantRefreshDuringInsertionKeepsOneRowPerAdmittedJob() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        DownloadJobsModel model(service);
        bool attempted = false;
        DownloadSubmissionResult second;
        QObject::connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, &model, [&] {
            if (!attempted) {
                attempted = true;
                second = service.submit(request(other_request_id, "Second"));
                model.refresh();
            }
        });
        const auto first = service.submit(request());
        QCOMPARE(first.error, DownloadSubmissionError::None);
        model.refresh();
        QVERIFY(attempted);
        QCOMPARE(second.error, DownloadSubmissionError::None);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.activeCount(), 2);
        QCOMPARE(value(model, DownloadJobsModel::JobIdRole, 0).toString(), QString::fromStdString(first.job_ids[0]));
        QCOMPARE(value(model, DownloadJobsModel::JobIdRole, 1).toString(), QString::fromStdString(second.job_ids[0]));
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(execution.starts, 0);
    }

    void unknownProgressAndPhaseChangesRemainHonest() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        QCOMPARE(service.submit(request()).error, DownloadSubmissionError::None);
        DownloadJobsModel model(service);
        QVERIFY(value(model, DownloadJobsModel::IndeterminateRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::TotalUnitsRole).isValid());
        QCOMPARE(value(model, DownloadJobsModel::ProgressRatioRole).toDouble(), 0.0);
        service.tick(0);
        execution.update.phase = DownloadPhase::Converting;
        execution.update.completed_units = 3;
        execution.update.total_units = 10;
        service.tick(1);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("converting"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Convertendo para MP3"));
        QCOMPARE(value(model, DownloadJobsModel::ProgressRatioRole).toDouble(), 0.3);
        execution.update.phase = DownloadPhase::Publishing;
        execution.update.completed_units = 0;
        execution.update.total_units.reset();
        service.tick(2);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("publishing"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Finalizando arquivo"));
        QVERIFY(value(model, DownloadJobsModel::IndeterminateRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
    }

    void cancellationTargetsExactIdAndWaitsForAcknowledgement() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        const auto receipt = service.submit(request());
        QCOMPARE(receipt.error, DownloadSubmissionError::None);
        const auto id = QString::fromStdString(receipt.job_ids[0]);
        DownloadJobsModel model(service);
        QVERIFY(!model.cancel(id + QStringLiteral("-wrong")));
        service.tick(0);
        model.refresh();
        QVERIFY(model.cancel(id));
        QCOMPARE(execution.cancellations, 1);
        QCOMPARE(execution.cancelled_id, receipt.job_ids[0]);
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("cancelling"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Cancelando"));
        QCOMPARE(model.activeCount(), 1);
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        execution.update.state = DownloadExecutionState::Cancelled;
        service.tick(1);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("cancelled"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Cancelado"));
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.completedCount(), 0);
        QVERIFY(!model.cancel(id));
        QCOMPARE(importer.calls, 0);
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
    }

    void failedImportExposesOnlyFinalFileAndExplicitRetryDoesNotRedownload() {
        Execution execution;
        Importer importer;
        importer.accepted = false;
        DownloadService service(execution, importer, "model");
        const auto receipt = service.submit(request());
        QCOMPARE(receipt.error, DownloadSubmissionError::None);
        const auto id = QString::fromStdString(receipt.job_ids[0]);
        DownloadJobsModel model(service);
        service.tick(0);
        execution.update.state = DownloadExecutionState::Published;
        execution.update.phase = DownloadPhase::Publishing;
        execution.update.completed_units = 1;
        execution.update.total_units = 1;
        execution.update.published_mp3 = finalPath();
        service.tick(1);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("import_failed"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(),
                 QStringLiteral("Arquivo pronto; importação pendente"));
        QCOMPARE(value(model, DownloadJobsModel::OutputPathRole).toString(),
                 QString::fromStdWString(finalPath().wstring()));
        QVERIFY(!value(model, DownloadJobsModel::ErrorTextRole).toString().isEmpty());
        QVERIFY(value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.completedCount(), 0);
        QCOMPARE(importer.calls, 1);
        model.refresh();
        QCOMPARE(importer.calls, 1);
        QVERIFY(!model.retryImport(id + QStringLiteral("-wrong")));
        QCOMPARE(importer.calls, 1);
        importer.accepted = true;
        QVERIFY(model.retryImport(id));
        QCOMPARE(importer.calls, 2);
        QCOMPARE(importer.last_id, receipt.job_ids[0]);
        QCOMPARE(importer.last_path, finalPath());
        QCOMPARE(execution.starts, 1);
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("completed"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Concluído"));
        QVERIFY(value(model, DownloadJobsModel::ErrorTextRole).toString().isEmpty());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::IndeterminateRole).toBool());
        QCOMPARE(value(model, DownloadJobsModel::ProgressRatioRole).toDouble(), 1.0);
        QCOMPARE(model.completedCount(), 1);
        QVERIFY(!model.retryImport(id));
        QCOMPARE(importer.calls, 2);
    }

    void typedFailuresHaveSafeActionableText_data() {
        QTest::addColumn<int>("error");
        const std::vector<DownloadFailure> errors{
            DownloadFailure::Unavailable,   DownloadFailure::Network,          DownloadFailure::Expired,
            DownloadFailure::Restricted,    DownloadFailure::UnsupportedMedia, DownloadFailure::NoAudio,
            DownloadFailure::LimitExceeded, DownloadFailure::Conversion,       DownloadFailure::Publication,
            DownloadFailure::Timeout,       DownloadFailure::InvalidResult};
        for (const auto error : errors)
            QTest::newRow(qPrintable(QString::number(static_cast<int>(error)))) << static_cast<int>(error);
    }

    void typedFailuresHaveSafeActionableText() {
        QFETCH(int, error);
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        QCOMPARE(service.submit(request()).error, DownloadSubmissionError::None);
        DownloadJobsModel model(service);
        service.tick(0);
        execution.update.state = DownloadExecutionState::Failed;
        execution.update.error = static_cast<DownloadFailure>(error);
        service.tick(1);
        model.refresh();
        QCOMPARE(value(model, DownloadJobsModel::StateRole).toString(), QStringLiteral("failed"));
        QCOMPARE(value(model, DownloadJobsModel::StatusTextRole).toString(), QStringLiteral("Falhou"));
        const auto text = value(model, DownloadJobsModel::ErrorTextRole).toString();
        QVERIFY(!text.isEmpty());
        QVERIFY(!text.contains(QStringLiteral("https:")));
        QVERIFY(!text.contains(QStringLiteral("private-token")));
        QVERIFY(!value(model, DownloadJobsModel::CanCancelRole).toBool());
        QVERIFY(!value(model, DownloadJobsModel::CanRetryImportRole).toBool());
        QVERIFY(value(model, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(model.activeCount(), 0);
    }

    void safeBoundsLiteralTitlesAndNoReadSideEffects() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        const std::string title = "<img src=x onerror=alert(1)> & música";
        QCOMPARE(service.submit(request(request_id, title)).error, DownloadSubmissionError::None);
        DownloadJobsModel model(service);
        DownloadJobsModel other(service);
        QCOMPARE(value(model, DownloadJobsModel::TitleRole).toString(), QString::fromStdString(title));
        const auto roles = model.roleNames();
        QVERIFY(!model.data({}).isValid());
        QVERIFY(!model.data(model.index(-1, 0)).isValid());
        QVERIFY(!model.data(model.index(1, 0)).isValid());
        QVERIFY(!model.data(model.index(0, 1)).isValid());
        QVERIFY(!model.data(other.index(0, 0), DownloadJobsModel::TitleRole).isValid());
        QVERIFY(!model.data(model.index(0, 0), Qt::UserRole + 500).isValid());
        QCOMPARE(model.rowCount(model.index(0, 0)), 0);
        for (int pass = 0; pass < 3; ++pass) {
            model.refresh();
            for (auto role = roles.cbegin(); role != roles.cend(); ++role) {
                const auto visible = value(model, role.key()).toString();
                QVERIFY(!visible.contains(QStringLiteral("private-token")));
                QVERIFY(!visible.contains(QStringLiteral("https://media.example.test")));
            }
        }
        QVERIFY(!model.cancel(QString{}));
        QVERIFY(!model.retryImport(QString{}));
        QCOMPARE(execution.starts, 0);
        QCOMPARE(execution.polls, 0);
        QCOMPARE(execution.cancellations, 0);
        QCOMPARE(importer.calls, 0);
    }
};

QTEST_APPLESS_MAIN(DownloadJobsModelTests)
#include "DownloadJobsModelTests.moc"
