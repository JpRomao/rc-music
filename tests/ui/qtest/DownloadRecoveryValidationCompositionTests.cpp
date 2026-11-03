#include "adapters/DownloadJobsModel.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsDownloadRecoveryReview.hpp"
#include "adapters/WindowsDownloadRecoveryValidation.hpp"
#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "presentation/DownloadApplicationBinding.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QCryptographicHash>
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

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {
constexpr auto old_job = "old-1";

// Create only a fresh, private journal directory in this test's own TEMP.
// Existing ACLs, installed data, browser state and personal media are untouched.
bool createPrivateDirectory(const QString& path) {
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
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != FALSE;
}

QByteArray readOwnedFile(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool canOpenForWriting(const QString& path) {
    const download_artifacts::Handle writer{CreateFileW(download_artifacts::nativePath(path).c_str(), GENERIC_WRITE,
                                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    return writer.valid(); // Opens a handle only; no bytes or attributes are changed.
}

class SeedOwnershipStore final : public DownloadOwnershipStore {
  public:
    SeedOwnershipStore(QString directory, bool published) : real_(std::move(directory)), published_(published) {}
    DownloadOwnershipLoadResult load() override { return real_.load(); }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& record) override {
        // Exercise a physically renamed final whose Published write received no
        // Saved acknowledgement. Later validation must not promote that stage.
        return !published_ && record.stage == DownloadOwnershipStage::Published ? DownloadCheckpointSaveStatus::Failed
                                                                                : real_.record(record);
    }

  private:
    WindowsDownloadOwnershipStore real_;
    bool published_;
};

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

class DownloadRecoveryValidationCompositionTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() { Q_INIT_RESOURCE(m0_qt_shell_assets); }

    void realMainValidatesRetainsReleasesAndDrainsWithoutImporting_data() {
        QTest::addColumn<bool>("published");
        QTest::newRow("published") << true;
        QTest::newRow("prepared-without-saved-acknowledgement") << false;
    }
    void realMainValidatesRetainsReleasesAndDrainsWithoutImporting() {
        QFETCH(bool, published);
        const auto original =
            readOwnedFile(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + QStringLiteral("/mp3-unicode-tags.mp3"));
        QCOMPARE(original.size(), 24966);
        QCOMPARE(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex(),
                 QByteArray{"65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"});
        QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/rc-music-validate-espaço-音-XXXXXX")};
        QVERIFY(temporary.isValid());
        const auto documents = temporary.filePath(QStringLiteral("documents"));
        QVERIFY(createPrivateDirectory(documents));
        const WindowsDownloadWorkspaceConfig workspace_config{temporary.filePath(QStringLiteral("staging")),
                                                              temporary.filePath(QStringLiteral("publication")),
                                                              1024 * 1024};
        QVERIFY(QDir{}.mkdir(workspace_config.staging_parent));
        QVERIFY(QDir{}.mkdir(workspace_config.publish_parent));
        SeedOwnershipStore ownership{documents, published};
        QString final_path;
        {
            WindowsDownloadWorkspace workspace{workspace_config, old_job, ownership};
            QVERIFY(workspace.create());
            QCOMPARE(workspace.inputSink()->write(original), original.size());
            QVERIFY(workspace.sealInput());
            QCOMPARE(workspace.outputSink()->write(original), original.size());
            QVERIFY(workspace.sealOutput());
            // Seeding acknowledges only ownership. The actual MP3 probe and
            // full decode below run in the real native validation worker.
            final_path = workspace.validateAndPublish([](const QString&) { return true; });
            if (!published)
                final_path = workspace.retainedPublicationPath();
            QVERIFY(!final_path.isEmpty());
        }
        const auto seeded_ownership = ownership.load();
        QCOMPARE(seeded_ownership.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(seeded_ownership.journal && seeded_ownership.journal->records.size() == 1);
        QCOMPARE(seeded_ownership.journal->records.front().stage,
                 published ? DownloadOwnershipStage::Published : DownloadOwnershipStage::PublicationPrepared);
        QVERIFY(canOpenForWriting(final_path));
        const DownloadCheckpoint checkpoint{.receipts = {{.request_id = "01234567-89ab-4cde-8fab-0123456789ab",
                                                          .fingerprint = std::string(64, 'a'),
                                                          .job_ids = {old_job},
                                                          .retained_request_bytes = 128}},
                                            .jobs = {{.job_id = old_job,
                                                      .title = "<b>Recovered MP3</b>",
                                                      .state = DownloadJobState::Interrupted,
                                                      .completed_mp3 = std::filesystem::path{final_path.toStdWString()},
                                                      .error = DownloadFailure::Unavailable}}};
        WindowsDownloadCheckpointStore checkpoint_seed{documents};
        QCOMPARE(checkpoint_seed.save(checkpoint), DownloadCheckpointSaveStatus::Saved);
        const auto checkpoint_path = QDir{documents}.filePath(QStringLiteral("download-checkpoint.json"));
        const auto ownership_path = QDir{documents}.filePath(QStringLiteral("download-ownership.json"));
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
            [&] { return std::make_unique<ObservedCheckpointStore>(documents, checkpoint_writes); }};
        WindowsDownloadRecoveryReview review{documents, workspace_config};
        const WindowsLocalMp3ValidatorConfig tools{QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH),
                                                   QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
        WindowsDownloadRecoveryValidation validation{documents, workspace_config, tools};
        const auto fallback_drain = qScopeGuard([&] {
            validation.shutdown();
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
                                           {.recovery_review = &review, .recovery_validation = &validation}};
        QVERIFY(binding.start());
        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), DownloadAdmissionControllerState::Ready, 5'000);
        auto* model = binding.jobsModel();
        QCOMPARE(model->rowCount(), 1);
        const auto index = model->index(0, 0);
        QVERIFY(!model->data(index, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(!binding.validateMedia(QString::fromUtf8(old_job)));

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
        window->resize(720, 520);
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
        QVERIFY(click(*window, review_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::CanValidateMediaRole).toBool(), 5'000);
        QQuickItem* validate_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((validate_button = visualControl(
                                      window->contentItem(), QStringLiteral("download-validate-media"))) != nullptr,
                                 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(validate_button->isVisible() && validate_button->isEnabled(), 5'000);
        QVERIFY(click(*window, validate_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::ValidationTextRole)
                                     .toString()
                                     .contains(QStringLiteral("MP3 validado")),
                                 10'000);
        QVERIFY(!canOpenForWriting(final_path));
        QVERIFY(!model->data(index, DownloadJobsModel::CanReviewFilesRole).toBool());
        QVERIFY(!model->data(index, DownloadJobsModel::CanValidateMediaRole).toBool());
        QVERIFY(model->data(index, DownloadJobsModel::CanReleaseValidationRole).toBool());
        auto* validation_status = visualControl(window->contentItem(), QStringLiteral("download-validation-status"));
        QVERIFY(validation_status != nullptr);
        QVERIFY(validation_status->isVisible());
        QCOMPARE(validation_status->property("textFormat").toInt(), int(Qt::PlainText));
        QVERIFY(validation_status->property("text").toString().contains(QStringLiteral("não foi importado")));
        QVERIFY(!validation_status->property("text").toString().contains(final_path));
        auto* review_status = visualControl(window->contentItem(), QStringLiteral("download-review-status"));
        QVERIFY(review_status != nullptr);
        QVERIFY(!review_status->isVisible());
        QCOMPARE(readOwnedFile(final_path), original);
        QCOMPARE(readOwnedFile(checkpoint_path), checkpoint_before);
        QCOMPARE(readOwnedFile(ownership_path), ownership_before);
        QCOMPARE(checkpoint_writes.load(), 0);

        QQuickItem* release_button = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((release_button = visualControl(
                                      window->contentItem(), QStringLiteral("download-release-validation"))) != nullptr,
                                 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(release_button->isVisible() && release_button->isEnabled(), 5'000);
        QVERIFY(click(*window, release_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::CanValidateMediaRole).toBool(), 5'000);
        QVERIFY(canOpenForWriting(final_path));
        QVERIFY(!model->data(index, DownloadJobsModel::CanReleaseValidationRole).toBool());
        QCOMPARE(readOwnedFile(final_path), original);
        QCOMPARE(readOwnedFile(ownership_path), ownership_before);
        QCOMPARE(readOwnedFile(checkpoint_path), checkpoint_before);
        QCOMPARE(checkpoint_writes.load(), 0);

        // A new validation uses fresh proof. Retain this second capability and
        // exit through the real presenter/binding instead of releasing in UI.
        QTRY_VERIFY_WITH_TIMEOUT(validate_button->isVisible() && validate_button->isEnabled(), 5'000);
        QVERIFY(click(*window, validate_button));
        QTRY_VERIFY_WITH_TIMEOUT(model->data(index, DownloadJobsModel::ValidationTextRole)
                                     .toString()
                                     .contains(QStringLiteral("MP3 validado")),
                                 10'000);
        QVERIFY(!canOpenForWriting(final_path));
        QVERIFY(!validation.stopped());
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.imports, 0);
        QCOMPARE(audio.loads, 0);
        QCOMPARE(audio.plays, 0);
        QCOMPARE(metadata.reads, 0);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
        QCOMPARE(model->data(index, DownloadJobsModel::StateRole).toString(), QStringLiteral("interrupted"));
        QVERIFY(model->data(index, DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QVERIFY(!binding.warningText().contains(temporary.path()));
        QVERIFY(!binding.statusText().contains(temporary.path()));
        QVERIFY(!QFile::exists(temporary.filePath(QStringLiteral("session.json"))));
        QCOMPARE(platform.exits, 0);
        QVERIFY(presenter.requestExit());
        QTRY_VERIFY_WITH_TIMEOUT((PlayerPresenterTestAccess::pollPersistence(presenter), platform.exits == 1), 5'000);
        QVERIFY(controller.stopped());
        QCOMPARE(controller.saveStatus(), std::optional{DownloadCheckpointSaveStatus::Saved});
        QVERIFY(validation.stopped());
        QVERIFY(review.stopped());
        QVERIFY(canOpenForWriting(final_path));
        QCOMPARE(readOwnedFile(final_path), original);
        QCOMPARE(readOwnedFile(ownership_path), ownership_before);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.imports, 0);
        QCOMPARE(audio.loads, 0);
        QCOMPARE(audio.plays, 0);
        QCOMPARE(metadata.reads, 0);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
        root.reset();
    }
};
} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::DownloadRecoveryValidationCompositionTests)
#include "DownloadRecoveryValidationCompositionTests.moc"
