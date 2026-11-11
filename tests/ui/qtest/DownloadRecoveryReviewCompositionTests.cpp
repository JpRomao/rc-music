#include "adapters/DownloadJobsModel.hpp"
#include "adapters/DownloadOwnershipJson.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsDownloadRecoveryReview.hpp"
#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "presentation/DownloadApplicationBinding.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <Sddl.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {
constexpr auto old_job = "old-1";

// Only create a new directory inside the test's own TEMP. Do not repair an
// existing ACL, consult user configuration or install/register any component.
bool createPrivateObject(const QString& path, const QByteArray* contents = nullptr, bool workspace_object = false) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token{raw};
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    std::vector<unsigned char> bytes(size);
    if (bytes.empty() || !GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
        return false;
    const auto free_sid = qScopeGuard([&] { LocalFree(sid); });
    const auto sddl = (workspace_object ? QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;")
                                        : QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;")) +
                      QString::fromWCharArray(sid) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    if (contents == nullptr)
        return CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != FALSE;
    const download_artifacts::Handle file{CreateFileW(download_artifacts::nativePath(path).c_str(), GENERIC_WRITE, 0,
                                                      &attributes, CREATE_NEW,
                                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    DWORD written = 0;
    return file.valid() &&
           WriteFile(file.get(), contents->constData(), static_cast<DWORD>(contents->size()), &written, nullptr) &&
           written == static_cast<DWORD>(contents->size()) && FlushFileBuffers(file.get());
}

QByteArray readOwnedFile(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

std::optional<DownloadFileIdentity> ownIdentity(const QString& path, bool directory) {
    const download_artifacts::Handle handle{CreateFileW(
        download_artifacts::nativePath(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr)};
    FILE_ID_INFO info{};
    if (!handle.valid() || !download_artifacts::information(handle.get(), directory, info))
        return std::nullopt;
    DownloadFileIdentity identity{.volume_serial = info.VolumeSerialNumber};
    std::copy(std::begin(info.FileId.Identifier), std::end(info.FileId.Identifier), identity.file_id.begin());
    return identity;
}

class Platform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return false; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
};

class Audio final : public PlaybackBackend {
  public:
    const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback callback) override { callback_ = std::move(callback); }
    PlaybackCommandResult load(const std::filesystem::path&) override {
        ++loads;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override {
        ++plays;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult pause() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }
    int loads{0};
    int plays{0};

  private:
    PlaybackSnapshot snapshot_{};
    PlaybackEventCallback callback_{};
};

class Metadata final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest) override {
        ++reads;
        return MetadataCommandResult::Accepted;
    }
    MetadataCommandResult cancelRead(MetadataGeneration) override { return MetadataCommandResult::Accepted; }
    MetadataPollResult poll(MetadataGeneration) override { return {}; }
    std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
    int reads{0};
};

class Execution final : public DownloadExecution {
  public:
    bool start(const std::string&, const DownloadItem&) override {
        ++starts;
        return false;
    }
    void cancel(std::string_view) override {}
    DownloadExecutionUpdate poll(std::string_view) override { return {}; }
    int starts{0};
};

class Importer final : public DownloadImport {
  public:
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++imports;
        return false;
    }
    int imports{0};
};

class Identity final : public DownloadRequestIdentity {
  public:
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};

class ObservedCheckpointStore final : public DownloadCheckpointStore {
  public:
    ObservedCheckpointStore(QString directory, std::atomic<int>& writes)
        : store_(std::move(directory)), writes_(writes) {}
    DownloadCheckpointLoadResult load() override { return store_.load(); }
    DownloadCheckpointSaveStatus save(const DownloadCheckpoint& checkpoint) override {
        ++writes_;
        return store_.save(checkpoint);
    }

  private:
    WindowsDownloadCheckpointStore store_;
    std::atomic<int>& writes_;
};

bool click(QQuickWindow& window, QQuickItem* control) {
    if (!control || !control->isVisible() || !control->isEnabled() || control->width() <= 0 || control->height() <= 0)
        return false;
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      control->mapToScene(QPointF{control->width() / 2, control->height() / 2}).toPoint());
    return true;
}

QQuickItem* visualControl(QQuickItem* parent, const QString& name) {
    if (parent == nullptr || parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems())
        if (auto* found = visualControl(child, name))
            return found;
    return nullptr;
}
} // namespace

class DownloadRecoveryReviewCompositionTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { Q_INIT_RESOURCE(m0_qt_shell_assets); }

    void realWorkerPreservesMissingInvalidFutureAndUnrecordedHistory_data() {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<bool>("exists");
        QTest::addColumn<int>("expected");
        QTest::newRow("missing") << QByteArray{} << false << int(DownloadRecoveryReviewOutcome::HistoryMissing);
        QTest::newRow("invalid") << QByteArray{"{broken synthetic ownership"} << true
                                 << int(DownloadRecoveryReviewOutcome::HistoryInvalid);
        QTest::newRow("future") << QByteArray{"{\"schemaVersion\":999,\"owner\":\"rc-music-download-ownership\"}"}
                                << true << int(DownloadRecoveryReviewOutcome::HistoryFuture);
        QTest::newRow("loaded-empty") << DownloadOwnershipJson::encode({}) << true
                                      << int(DownloadRecoveryReviewOutcome::Unverified);
        DownloadFileIdentity staging{.volume_serial = 7};
        staging.file_id[0] = 1;
        auto publication = staging;
        publication.file_id[0] = 2;
        const DownloadOwnershipJournal unrecorded{.records = {{.job_id = "other-1",
                                                               .workspace_id = "abcdefab-cdef-4abc-8def-000000000001",
                                                               .staging_parent = staging,
                                                               .publish_parent = publication}}};
        QTest::newRow("different-record")
            << DownloadOwnershipJson::encode(unrecorded) << true << int(DownloadRecoveryReviewOutcome::Unverified);
    }

    void realWorkerPreservesMissingInvalidFutureAndUnrecordedHistory() {
        QFETCH(QByteArray, document);
        QFETCH(bool, exists);
        QFETCH(int, expected);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto directory = temporary.filePath(QStringLiteral("documents"));
        QVERIFY(createPrivateObject(directory));
        const auto journal_path = QDir{directory}.filePath(QStringLiteral("download-ownership.json"));
        if (exists) {
            QVERIFY(!document.isEmpty());
            QVERIFY(createPrivateObject(journal_path, &document));
        }
        const auto sentinel = temporary.filePath(QStringLiteral("preserve-sentinel"));
        const QByteArray sentinel_bytes{"synthetic external sibling; preserve"};
        QVERIFY(createPrivateObject(sentinel, &sentinel_bytes));
        const auto entries = QDir{directory}.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        // No physical assessment is permitted without a matching journal
        // record, so absent trusted parents must remain absent in these rows.
        const WindowsDownloadWorkspaceConfig config{
            .staging_parent = temporary.filePath(QStringLiteral("never-created-staging")),
            .publish_parent = temporary.filePath(QStringLiteral("never-created-publication"))};
        WindowsDownloadRecoveryReview review{directory, config};
        const auto drain = qScopeGuard([&] { review.shutdown(); });
        QCOMPARE(review.startReview(1, old_job), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value() || (result = review.poll(1)).has_value(), 5'000);
        QCOMPARE(result->operation_id, std::uint64_t{1});
        QCOMPARE(result->job_id, std::string{old_job});
        QCOMPARE(int(result->outcome), expected);
        QCOMPARE(result->action, DownloadRecoveryReviewAction::ReviewFiles);
        QCOMPARE(review.startDiscardPartials(2, old_job), DownloadRecoveryReviewCommand::Accepted);
        result.reset();
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value() || (result = review.poll(2)).has_value(), 5'000);
        QCOMPARE(result->operation_id, std::uint64_t{2});
        QCOMPARE(result->job_id, std::string{old_job});
        QCOMPARE(result->action, DownloadRecoveryReviewAction::DiscardPartials);
        QCOMPARE(int(result->outcome), expected);
        QCOMPARE(QFile::exists(journal_path), exists);
        if (exists)
            QCOMPARE(readOwnedFile(journal_path), document);
        QCOMPARE(QDir{directory}.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name), entries);
        QCOMPARE(readOwnedFile(sentinel), sentinel_bytes);
        QVERIFY(!QDir{config.staging_parent}.exists());
        QVERIFY(!QDir{config.publish_parent}.exists());
        review.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(review.stopped(), 5'000);
    }

    void realMainReviewsOwnedFinalAndRechecksChangedBytesWithoutImporting() {
        QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/rc-music-review-espaço-音-XXXXXX")};
        QVERIFY(temporary.isValid());
        const auto journal_directory = temporary.filePath(QStringLiteral("documents"));
        QVERIFY(createPrivateObject(journal_directory));
        const WindowsDownloadWorkspaceConfig workspace_config{
            .staging_parent = temporary.filePath(QStringLiteral("staging")),
            .publish_parent = temporary.filePath(QStringLiteral("publication"))};
        QVERIFY(QDir{}.mkdir(workspace_config.staging_parent));
        QVERIFY(QDir{}.mkdir(workspace_config.publish_parent));

        const QByteArray original{"synthetic owned final; NOT validated MP3"};
        QString final_path;
        WindowsDownloadOwnershipStore ownership{journal_directory};
        {
            WindowsDownloadWorkspace workspace{workspace_config, old_job, ownership};
            QVERIFY(workspace.create());
            QCOMPARE(workspace.inputSink()->write(original), original.size());
            QVERIFY(workspace.sealInput());
            QCOMPARE(workspace.outputSink()->write(original), original.size());
            QVERIFY(workspace.sealOutput());
            // Deliberate synthetic validator: this journey proves ownership
            // observation only, not MP3 content, conversion or import authority.
            final_path = workspace.validateAndPublish([](const QString&) { return true; });
            QVERIFY(!final_path.isEmpty());
            QVERIFY(workspace.cleanup());
        }
        const auto journal = ownership.load();
        QCOMPARE(journal.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(journal.journal.has_value());
        QCOMPARE(journal.journal->records.size(), std::size_t{1});
        QCOMPARE(journal.journal->records.front().stage, DownloadOwnershipStage::Published);
        QVERIFY(journal.journal->records.front().cleanup_complete);

        const DownloadCheckpoint checkpoint{.receipts = {{.request_id = "01234567-89ab-4cde-8fab-0123456789ab",
                                                          .fingerprint = std::string(64, 'a'),
                                                          .job_ids = {old_job},
                                                          .retained_request_bytes = 128}},
                                            .jobs = {{.job_id = old_job,
                                                      .title = "<b>Owned synthetic title</b>",
                                                      .state = DownloadJobState::Interrupted,
                                                      .completed_mp3 = std::filesystem::path{final_path.toStdWString()},
                                                      .error = DownloadFailure::Unavailable}}};
        WindowsDownloadCheckpointStore seed{journal_directory};
        QCOMPARE(seed.save(checkpoint), DownloadCheckpointSaveStatus::Saved);
        const auto checkpoint_path = QDir{journal_directory}.filePath(QStringLiteral("download-checkpoint.json"));
        const auto ownership_path = QDir{journal_directory}.filePath(QStringLiteral("download-ownership.json"));
        const auto checkpoint_before = readOwnedFile(checkpoint_path);
        const auto ownership_before = readOwnedFile(ownership_path);
        QVERIFY(!checkpoint_before.isEmpty());
        QVERIFY(!ownership_before.isEmpty());

        Platform platform;
        Audio audio;
        Metadata metadata;
        PlayerPresenter presenter{platform, audio, metadata, [] { return std::vector<std::filesystem::path>{}; }};
        ShellUiContract contract{nullptr};
        Execution execution;
        Importer importer;
        Identity identity;
        std::atomic<int> checkpoint_writes{0};
        SingleWorkerDownloadCheckpointIo checkpoint_io{
            [&] { return std::make_unique<ObservedCheckpointStore>(journal_directory, checkpoint_writes); }};
        WindowsDownloadRecoveryReview review{journal_directory, workspace_config};
        const auto fallback_drain = qScopeGuard([&] {
            review.shutdown();
            checkpoint_io.shutdown();
        });
        DownloadService service{execution, importer, "fresh", {}, &identity};
        DownloadAdmissionController controller{service, checkpoint_io, SessionLoadResult{},
                                               DownloadFinalSavePolicy::HoldForDecision};
        DownloadApplicationBinding binding{presenter,
                                           controller,
                                           service,
                                           {.shutdown = [] {}, .stopped = [] { return true; }},
                                           {.recovery_review = &review}};
        QVERIFY(binding.start());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), DownloadAdmissionControllerState::Ready, 5'000);
        auto* model = binding.jobsModel();
        QCOMPARE(model->rowCount(), 1);
        const auto index = model->index(0, 0);
        QCOMPARE(model->data(index, DownloadJobsModel::StateRole).toString(), QStringLiteral("interrupted"));
        QVERIFY(model->data(index, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(model->data(index, DownloadJobsModel::CanReviewFilesRole).toBool());
        std::vector<bool> observed_busy;
        const auto observe_projection = connect(model, &QAbstractItemModel::dataChanged, &binding, [&] {
            observed_busy.push_back(model->data(index, DownloadJobsModel::ReviewBusyRole).toBool());
        });

        QQmlEngine engine;
        QQmlComponent component{&engine};
        component.loadFromModule(QStringLiteral("MusicPlayer.M0"), QStringLiteral("Main"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root{
            component.createWithInitialProperties({{QStringLiteral("uiContract"), QVariant::fromValue(&contract)},
                                                   {QStringLiteral("presenter"), QVariant::fromValue(&presenter)},
                                                   {QStringLiteral("downloads"), QVariant::fromValue(&binding)}})};
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(click(*window, root->findChild<QQuickItem*>(QStringLiteral("downloads"))));
        auto* panel = root->findChild<QObject*>(QStringLiteral("downloads-panel"));
        QVERIFY(panel != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(panel->property("opened").toBool(), 5'000);
        QQuickItem* review_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (review_button = visualControl(window->contentItem(), QStringLiteral("download-review-files"))) != nullptr,
            5'000);
        QTRY_VERIFY_WITH_TIMEOUT(review_button->isVisible() && review_button->isEnabled(), 5'000);
        QCOMPARE(review_button->property("text").toString(), QStringLiteral("Verificar arquivos"));
        QVERIFY(click(*window, review_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::ReviewTextRole)
                                     .toString()
                                     .contains(QStringLiteral("Arquivo final reconhecido")),
                                 5'000);
        QVERIFY(std::ranges::find(observed_busy, true) != observed_busy.end());
        QVERIFY(!model->data(index, DownloadJobsModel::ReviewBusyRole).toBool());
        auto* review_status = visualControl(window->contentItem(), QStringLiteral("download-review-status"));
        QVERIFY(review_status != nullptr);
        QVERIFY(review_status->property("text").toString().contains(QStringLiteral("Arquivo final reconhecido")));
        QCOMPARE(review_button->property("text").toString(), QStringLiteral("Reverificar arquivos"));
        QCOMPARE(readOwnedFile(final_path), original);
        QCOMPARE(readOwnedFile(checkpoint_path), checkpoint_before);
        QCOMPARE(readOwnedFile(ownership_path), ownership_before);
        QCOMPARE(checkpoint_writes.load(), 0);

        auto changed = original;
        changed[0] = 'X';
        {
            QFile final{final_path};
            QVERIFY(final.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(final.write(changed), changed.size());
        }
        QVERIFY(click(*window, review_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::ReviewTextRole)
                                     .toString()
                                     .contains(QStringLiteral("conflito"), Qt::CaseInsensitive),
                                 5'000);
        QVERIFY(review_status->property("text").toString().contains(QStringLiteral("conflito"), Qt::CaseInsensitive));
        QCOMPARE(readOwnedFile(final_path), changed);
        QCOMPARE(readOwnedFile(checkpoint_path), checkpoint_before);
        QCOMPARE(readOwnedFile(ownership_path), ownership_before);
        QCOMPARE(checkpoint_writes.load(), 0);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.imports, 0);
        QCOMPARE(audio.loads, 0);
        QCOMPARE(audio.plays, 0);
        QCOMPARE(metadata.reads, 0);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
        QCOMPARE(platform.exits, 0);
        QVERIFY(!QFile::exists(temporary.filePath(QStringLiteral("session.json"))));
        QVERIFY(!binding.warningText().contains(temporary.path()));
        QVERIFY(!review_status->property("text").toString().contains(final_path));
        QCOMPARE(model->data(index, DownloadJobsModel::StateRole).toString(), QStringLiteral("interrupted"));
        QVERIFY(model->data(index, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        disconnect(observe_projection);
        // Ordinary owner shutdown may save final history. It is separate from
        // the explicit read-only reviews whose zero-write evidence is above.
        QVERIFY(presenter.requestExit());
        QTRY_VERIFY_WITH_TIMEOUT((PlayerPresenterTestAccess::pollPersistence(presenter), platform.exits == 1), 5'000);
        QVERIFY(controller.stopped());
        QCOMPARE(controller.saveStatus(), std::optional{DownloadCheckpointSaveStatus::Saved});
        QTRY_VERIFY_WITH_TIMEOUT(review.stopped(), 5'000);
        root.reset();
    }
    void realMainDiscardsOnlyFreshlyVerifiedPartialsAfterExplicitConsent_data() {
        QTest::addColumn<bool>("substitute_after_consent");
        QTest::addColumn<bool>("already_clean_after_restart");
        QTest::newRow("owned-partials") << false << false;
        QTest::newRow("substituted-after-consent") << true << false;
        QTest::newRow("cleanup-saved-before-restart") << false << true;
    }
    void realMainDiscardsOnlyFreshlyVerifiedPartialsAfterExplicitConsent() {
        QFETCH(bool, substitute_after_consent);
        QFETCH(bool, already_clean_after_restart);
        QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/rc-music-discard-espaço-音-XXXXXX")};
        QVERIFY(temporary.isValid());
        const auto documents = temporary.filePath(QStringLiteral("documents"));
        QVERIFY(createPrivateObject(documents));
        const WindowsDownloadWorkspaceConfig config{temporary.filePath(QStringLiteral("staging")),
                                                    temporary.filePath(QStringLiteral("publication"))};
        QVERIFY(QDir{}.mkdir(config.staging_parent));
        QVERIFY(QDir{}.mkdir(config.publish_parent));
        const std::string uuid{"01234567-89ab-4cde-8fab-0123456789ab"};
        const auto job =
            QDir{config.staging_parent}.filePath(QStringLiteral("rc-music-job-") + QString::fromStdString(uuid));
        const auto input = QDir{job}.filePath(QStringLiteral("input.bin"));
        const auto output = QDir{job}.filePath(QStringLiteral("converted.part"));
        const QByteArray input_bytes{"owned synthetic input"}, output_bytes{"owned partial output"};
        QVERIFY(createPrivateObject(job, nullptr, true));
        QVERIFY(createPrivateObject(input, &input_bytes, true));
        QVERIFY(createPrivateObject(output, &output_bytes, true));
        const auto sibling = QDir{config.staging_parent}.filePath(QStringLiteral("not-owned.txt"));
        const auto other_final = QDir{config.publish_parent}.filePath(QStringLiteral("other-final.mp3"));
        const QByteArray sentinel{"preserve sibling and final byte for byte"};
        QVERIFY(createPrivateObject(sibling, &sentinel));
        QVERIFY(createPrivateObject(other_final, &sentinel));
        const auto staging_id = ownIdentity(config.staging_parent, true);
        const auto publish_id = ownIdentity(config.publish_parent, true);
        const auto job_id = ownIdentity(job, true);
        const auto input_id = ownIdentity(input, false);
        const auto output_id = ownIdentity(output, false);
        QVERIFY(staging_id && publish_id && job_id && input_id && output_id);
        WindowsDownloadOwnershipStore ownership{documents};
        DownloadOwnershipRecord record{
            .job_id = old_job, .workspace_id = uuid, .staging_parent = *staging_id, .publish_parent = *publish_id};
        QCOMPARE(ownership.record(record), DownloadCheckpointSaveStatus::Saved);
        record.stage = DownloadOwnershipStage::WorkspaceReady;
        record.job_directory = *job_id;
        record.input = *input_id;
        record.output = *output_id;
        QCOMPARE(ownership.record(record), DownloadCheckpointSaveStatus::Saved);
        if (already_clean_after_restart) {
            QVERIFY(QFile::remove(input));
            QVERIFY(QFile::remove(output));
            QVERIFY(QDir{}.rmdir(job));
            auto cleaned = record;
            cleaned.cleanup_complete = true;
            QCOMPARE(ownership.record(cleaned), DownloadCheckpointSaveStatus::Saved);
        }
        const DownloadCheckpoint checkpoint{.receipts = {{.request_id = uuid,
                                                          .fingerprint = std::string(64, 'a'),
                                                          .job_ids = {old_job},
                                                          .retained_request_bytes = 128}},
                                            .jobs = {{.job_id = old_job,
                                                      .title = "Synthetic interrupted download",
                                                      .state = DownloadJobState::Interrupted,
                                                      .error = DownloadFailure::Unavailable}}};
        WindowsDownloadCheckpointStore seed{documents};
        QCOMPARE(seed.save(checkpoint), DownloadCheckpointSaveStatus::Saved);
        const auto checkpoint_path = QDir{documents}.filePath(QStringLiteral("download-checkpoint.json"));
        const auto ownership_path = QDir{documents}.filePath(QStringLiteral("download-ownership.json"));
        const auto checkpoint_before = readOwnedFile(checkpoint_path);
        const auto ownership_before = readOwnedFile(ownership_path);
        Platform platform;
        Audio audio;
        Metadata metadata;
        PlayerPresenter presenter{platform, audio, metadata, [] { return std::vector<std::filesystem::path>{}; }};
        ShellUiContract contract{nullptr};
        Execution execution;
        Importer importer;
        Identity identity;
        std::atomic<int> checkpoint_writes{0};
        SingleWorkerDownloadCheckpointIo checkpoint_io{
            [&] { return std::make_unique<ObservedCheckpointStore>(documents, checkpoint_writes); }};
        WindowsDownloadRecoveryReview recovery{documents, config};
        const auto fallback_drain = qScopeGuard([&] {
            recovery.shutdown();
            checkpoint_io.shutdown();
        });
        DownloadService service{execution, importer, "fresh", {}, &identity};
        DownloadAdmissionController controller{service, checkpoint_io, SessionLoadResult{},
                                               DownloadFinalSavePolicy::HoldForDecision};
        DownloadApplicationBinding binding{presenter,
                                           controller,
                                           service,
                                           {.shutdown = [] {}, .stopped = [] { return true; }},
                                           {.recovery_review = &recovery}};
        QVERIFY(binding.start());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), DownloadAdmissionControllerState::Ready, 5'000);
        auto* model = binding.jobsModel();
        const auto index = model->index(0, 0);
        QQmlEngine engine;
        QQmlComponent component{&engine};
        component.loadFromModule(QStringLiteral("MusicPlayer.M0"), QStringLiteral("Main"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root{
            component.createWithInitialProperties({{QStringLiteral("uiContract"), QVariant::fromValue(&contract)},
                                                   {QStringLiteral("presenter"), QVariant::fromValue(&presenter)},
                                                   {QStringLiteral("downloads"), QVariant::fromValue(&binding)}})};
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(click(*window, root->findChild<QQuickItem*>(QStringLiteral("downloads"))));
        QQuickItem* review_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (review_button = visualControl(window->contentItem(), QStringLiteral("download-review-files"))) &&
                review_button->isVisible() && review_button->isEnabled(),
            5'000);
        QVERIFY(click(*window, review_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::CanDiscardPartialsRole).toBool(), 5'000);
        QQuickItem* discard_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (discard_button = visualControl(window->contentItem(), QStringLiteral("download-discard-partials"))) &&
                discard_button->isVisible() && discard_button->isEnabled(),
            5'000);
        QVERIFY(click(*window, discard_button));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.discardConsentToken().isEmpty(), 5'000);
        const auto cancelled_token = binding.discardConsentToken();
        QQuickItem* cancel_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (cancel_button = visualControl(window->contentItem(), QStringLiteral("download-discard-cancel"))) &&
                cancel_button->isVisible() && cancel_button->isEnabled(),
            5'000);
        QVERIFY(click(*window, cancel_button));
        QTRY_VERIFY_WITH_TIMEOUT(binding.discardConsentToken().isEmpty(), 5'000);
        if (already_clean_after_restart) {
            QVERIFY(!QDir{job}.exists());
        } else {
            QCOMPARE(readOwnedFile(input), input_bytes);
            QCOMPARE(readOwnedFile(output), output_bytes);
        }
        QCOMPARE(readOwnedFile(ownership_path), ownership_before);
        QVERIFY(!binding.confirmDiscardPartials(QString::fromLatin1(old_job), cancelled_token));
        QVERIFY(click(*window, discard_button));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.discardConsentToken().isEmpty(), 5'000);
        QVERIFY(binding.discardConsentToken() != cancelled_token);
        QByteArray replacement{"unowned replacement after consent"};
        if (substitute_after_consent) {
            // Keep the old object alive under another leaf so its file ID cannot
            // be recycled; the UI's preceding review/token is not a capability.
            QVERIFY(QFile::rename(input, temporary.filePath(QStringLiteral("unowned-old-input"))));
            QVERIFY(createPrivateObject(input, &replacement, true));
        }
        QQuickItem* confirm_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (confirm_button = visualControl(window->contentItem(), QStringLiteral("download-discard-confirm"))) &&
                confirm_button->isVisible() && confirm_button->isEnabled(),
            5'000);
        confirm_button->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(binding.discardConsentToken().isEmpty(), 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(!model->data(index, DownloadJobsModel::ReviewBusyRole).toBool(), 5'000);
        if (substitute_after_consent) {
            QVERIFY(model->data(index, DownloadJobsModel::ReviewTextRole)
                        .toString()
                        .contains(QStringLiteral("conflito"), Qt::CaseInsensitive));
            QCOMPARE(readOwnedFile(input), replacement);
            QCOMPARE(readOwnedFile(output), output_bytes);
            QCOMPARE(readOwnedFile(ownership_path), ownership_before);
            QVERIFY(QDir{job}.exists());
        } else {
            QVERIFY(model->data(index, DownloadJobsModel::ReviewTextRole)
                        .toString()
                        .contains(QStringLiteral("confirmada no registro"), Qt::CaseInsensitive));
            QVERIFY(!QFile::exists(input));
            QVERIFY(!QFile::exists(output));
            QVERIFY(!QDir{job}.exists());
            const auto after = ownership.load();
            QCOMPARE(after.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(after.journal && after.journal->records.size() == 1);
            auto expected = record;
            expected.cleanup_complete = true;
            QCOMPARE(after.journal->records.front(), expected);
        }
        QCOMPARE(readOwnedFile(sibling), sentinel);
        QCOMPARE(readOwnedFile(other_final), sentinel);
        QCOMPARE(readOwnedFile(checkpoint_path), checkpoint_before);
        QCOMPARE(checkpoint_writes.load(), 0);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.imports, 0);
        QCOMPARE(audio.loads, 0);
        QCOMPARE(audio.plays, 0);
        QCOMPARE(metadata.reads, 0);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
        QCOMPARE(model->data(index, DownloadJobsModel::StateRole).toString(),
                 substitute_after_consent ? QStringLiteral("interrupted") : QStringLiteral("cancelled"));
        QVERIFY(model->data(index, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(!model->data(index, DownloadJobsModel::CanDiscardPartialsRole).toBool());
        QVERIFY(!binding.warningText().contains(temporary.path()));
        QVERIFY(!binding.statusText().contains(temporary.path()));
        if (!substitute_after_consent) {
            QVERIFY(binding.canArchiveSettled());
            QVERIFY(binding.requestArchiveConfirmation());
            QVERIFY(binding.confirmArchive(binding.archiveConsentToken()));
            QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 0, 5'000);
            const auto archived = seed.load();
            QCOMPARE(archived.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(archived.checkpoint);
            QVERIFY(archived.checkpoint->jobs.empty());
            QCOMPARE(archived.checkpoint->archived_receipts.size(), std::size_t{1});
            QCOMPARE(archived.checkpoint->archived_receipts.front().job_ids, std::vector<std::string>{old_job});
            const DownloadRequest replay{
                uuid, {{"https://media.example.test/fixture.mp3", "Synthetic", DownloadMediaKind::Audio}}};
            QCOMPARE(controller.begin(replay).result.job_ids, std::vector<std::string>{old_job});
            QCOMPARE(execution.starts, 0);
        } else {
            QVERIFY(!binding.canArchiveSettled());
        }
        QVERIFY(presenter.requestExit());
        QTRY_VERIFY_WITH_TIMEOUT((PlayerPresenterTestAccess::pollPersistence(presenter), platform.exits == 1), 5'000);
        QVERIFY(controller.stopped());
        QCOMPARE(controller.saveStatus(), std::optional{DownloadCheckpointSaveStatus::Saved});
        QTRY_VERIFY_WITH_TIMEOUT(recovery.stopped(), 5'000);
        root.reset();
    }
};
} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::DownloadRecoveryReviewCompositionTests)
#include "DownloadRecoveryReviewCompositionTests.moc"
