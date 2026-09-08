#include "adapters/DownloadJobsModel.hpp"

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

#include <filesystem>
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
    bool accepted{true};
    std::string last_id{};
    std::filesystem::path last_path{};

    bool accept(const std::string& job_id, const std::filesystem::path& path) override {
        ++calls;
        last_id = job_id;
        last_path = path;
        return accepted;
    }
};

DownloadRequest request(std::string id = request_id, std::string title = "Audio") {
    return {.request_id = std::move(id),
            .items = {{.url = "https://media.example.test/music.mp3?secret=private-token",
                       .title = std::move(title),
                       .kind = DownloadMediaKind::Audio}}};
}

QVariant value(const DownloadJobsModel& model, int role, int row = 0) { return model.data(model.index(row, 0), role); }

std::filesystem::path finalPath() { return std::filesystem::path(L"C:\\RC Music synthetic downloads\\Música.mp3"); }

} // namespace

class DownloadJobsModelTests final : public QObject {
    Q_OBJECT

  private slots:
    void emptyProjectionHasOnlySafePublicRolesAndNoSideEffects() {
        Execution execution;
        Importer importer;
        DownloadService service(execution, importer, "model");
        DownloadJobsModel model(service);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto roles = model.roleNames();
        const QList<QByteArray> expected{"jobId",          "title",      "state",         "statusText",
                                         "completedUnits", "totalUnits", "indeterminate", "progressRatio",
                                         "outputPath",     "errorText",  "canCancel",     "canRetryImport"};
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
