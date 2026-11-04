#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsRecoveredMediaValidator.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sddl.h>

using namespace music_player::m0;
namespace {
using download_artifacts::Handle;
using download_artifacts::nativePath;
bool privateDirectory(const QString& path) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<std::byte> buffer(size);
    if (!size || !GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
        return false;
    const auto user = QString::fromWCharArray(sid);
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto sddl = QStringLiteral("O:%1D:P(A;;FA;;;%1)(A;;FA;;;SY)").arg(user);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const bool created = CreateDirectoryW(nativePath(path).c_str(), &attributes) != 0;
    LocalFree(descriptor);
    return created;
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QByteArray trustedMp3() {
    const auto bytes = read(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + "/mp3-unicode-tags.mp3");
    return bytes.size() == 24966 && QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
                                        "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"
               ? bytes
               : QByteArray{};
}
WindowsLocalMp3ValidatorConfig tools() {
    return {QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH), QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
}
bool writable(const QString& path) {
    Handle writer(CreateFileW(nativePath(path).c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    return writer.valid();
}
class ObservedStore final : public DownloadOwnershipStore {
  public:
    WindowsDownloadOwnershipStore real;
    bool refuse_published{false};
    int writes{0};
    explicit ObservedStore(const QString& directory) : real(directory) {}
    DownloadOwnershipLoadResult load() override { return real.load(); }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& record) override {
        ++writes;
        if (refuse_published && record.stage == DownloadOwnershipStage::Published)
            return DownloadCheckpointSaveStatus::Failed;
        return real.record(record);
    }
};
struct Fixture {
    QTemporaryDir root{QDir::tempPath() + "/RC Music recovered 日本-XXXXXX"};
    QString staging{root.filePath("staging")}, publication{root.filePath("published")},
        journal{root.filePath("journal")}, final{};
    ObservedStore store{journal};
    DownloadOwnershipRecord record{};
    WindowsDownloadWorkspaceConfig config() const { return {staging, publication, 1024 * 1024}; }
    bool prepare(const QByteArray& bytes, bool publication_confirmed = true) {
        if (bytes.isEmpty() || !root.isValid() || !QDir().mkdir(staging) || !QDir().mkdir(publication) ||
            !privateDirectory(journal))
            return false;
        store.refuse_published = !publication_confirmed;
        {
            WindowsDownloadWorkspace workspace(config(), "recovered-1", store);
            if (!workspace.create() || workspace.inputSink()->write(bytes) != bytes.size() || !workspace.sealInput() ||
                workspace.outputSink()->write(bytes) != bytes.size() || !workspace.sealOutput())
                return false;
            final = workspace.validateAndPublish([](const QString&) { return true; });
            if (!publication_confirmed)
                final = workspace.retainedPublicationPath();
            if (final.isEmpty())
                return false;
        } // Old writer/DELETE pins must be gone before a fresh read-only proof.
        const auto loaded = store.load();
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded || !loaded.journal ||
            loaded.journal->records.size() != 1)
            return false;
        record = loaded.journal->records.front();
        return true;
    }
    QByteArray journalBytes() const { return read(journal + "/download-ownership.json"); }
};
RecoveredMediaValidationUpdate finish(WindowsRecoveredMediaValidator& validator, qint64 now = 0) {
    QElapsedTimer clock;
    clock.start();
    RecoveredMediaValidationUpdate update;
    do {
        update = validator.poll(now);
        if (update.state == RecoveredMediaValidationState::Validated ||
            update.state == RecoveredMediaValidationState::Failed ||
            update.state == RecoveredMediaValidationState::Cancelled)
            return update;
        QTest::qWait(5);
    } while (clock.elapsed() < 10000);
    return update;
}
} // namespace
class WindowsRecoveredMediaValidatorTests final : public QObject {
    Q_OBJECT
  private slots:
    void validatesRealMp3WithPinsAndNeverPromotesPublication_data() {
        QTest::addColumn<bool>("published");
        QTest::addColumn<bool>("native_conversion");
        QTest::newRow("Published") << true << false;
        QTest::newRow("Prepared without publication acknowledgement") << false << false;
        QTest::newRow("Published native validation") << true << true;
        QTest::newRow("Prepared native validation without publication acknowledgement") << false << true;
    }
    void validatesRealMp3WithPinsAndNeverPromotesPublication() {
        QFETCH(bool, published);
        QFETCH(bool, native_conversion);
        Fixture f;
        const auto bytes = trustedMp3();
        QVERIFY(f.prepare(bytes, published));
        const auto before = f.journalBytes();
        const auto writes = f.store.writes;
        QCOMPARE(f.record.stage,
                 published ? DownloadOwnershipStage::Published : DownloadOwnershipStage::PublicationPrepared);
        {
            auto configuration = tools();
            if (native_conversion) {
                configuration = {};
                configuration.backend = WindowsLocalMediaBackend::MediaFoundation;
                configuration.media_foundation_executable = QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH);
            }
            WindowsRecoveredMediaValidator validator(f.config(), configuration);
            QVERIFY(validator.start(f.record, 0));
            QVERIFY(!validator.validatedCandidate());
            QVERIFY(!writable(f.final));
            const auto update = finish(validator);
            QCOMPARE(update.state, RecoveredMediaValidationState::Validated);
            QCOMPARE(update.error, DownloadFailure::None);
            QCOMPARE(update.ownership, DownloadOwnershipAssessment::OwnedFinalCandidate);
            const auto* candidate = validator.validatedCandidate();
            QVERIFY(candidate);
            QCOMPARE(candidate->pathForValidation(), f.final);
            QVERIFY(candidate->record() == f.record);
            QCOMPARE(candidate->revalidate(), DownloadOwnershipAssessment::OwnedFinalCandidate);
            QVERIFY(!writable(f.final));
            QVERIFY(!DeleteFileW(nativePath(f.final).c_str()));
            QVERIFY(!MoveFileExW(nativePath(f.final).c_str(), nativePath(f.final + ".moved").c_str(), 0));
            QCOMPARE(read(f.final), bytes);
            QVERIFY(!validator.start(f.record, 0));
        }
        QVERIFY(writable(f.final));
        QCOMPARE(f.journalBytes(), before);
        QCOMPARE(f.store.writes, writes);
        QCOMPARE(read(f.final), bytes);
    }
    void matchingOwnedTextIsNotAnApprovedMp3() {
        Fixture f;
        const QByteArray bytes("owned but not MP3");
        QVERIFY(f.prepare(bytes));
        const auto before = f.journalBytes();
        WindowsRecoveredMediaValidator validator(f.config(), tools());
        QVERIFY(validator.start(f.record, 0));
        const auto update = finish(validator);
        QCOMPARE(update.state, RecoveredMediaValidationState::Failed);
        QVERIFY(update.error != DownloadFailure::None);
        QVERIFY(!validator.validatedCandidate());
        QVERIFY(writable(f.final));
        QCOMPARE(f.journalBytes(), before);
        QCOMPARE(read(f.final), bytes);
    }
    void changedPrivateSecurityCannotEscapeAsAnApprovedCandidate() {
        Fixture f;
        const auto bytes = trustedMp3();
        QVERIFY(f.prepare(bytes));
        const auto before = f.journalBytes();
        WindowsRecoveredMediaValidator validator(f.config(), tools());
        QVERIFY(validator.start(f.record, 0));
        // Sharing pins protect bytes and names, not every security-descriptor
        // update by the owner. The final fresh proof must still reject this.
        Handle file(CreateFileW(nativePath(f.final).c_str(), WRITE_DAC | READ_CONTROL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        QVERIFY(file.valid());
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)", SDDL_REVISION_1, &descriptor,
                                                                     nullptr));
        const bool changed = SetKernelObjectSecurity(file.get(), DACL_SECURITY_INFORMATION, descriptor) != 0;
        LocalFree(descriptor);
        QVERIFY(changed);
        const auto update = finish(validator);
        QCOMPARE(update.state, RecoveredMediaValidationState::Failed);
        QCOMPARE(update.ownership, DownloadOwnershipAssessment::Conflict);
        QCOMPARE(update.error, DownloadFailure::Publication);
        QVERIFY(!validator.validatedCandidate());
        QCOMPARE(f.journalBytes(), before);
        QCOMPARE(read(f.final), bytes);
    }
    void conflictingOwnershipNeverStartsToolsOrExportsACandidate() {
        Fixture f;
        QVERIFY(f.prepare(trustedMp3()));
        auto record = f.record;
        record.output_sha256 = std::string(64, '0');
        WindowsRecoveredMediaValidator validator(f.config(), tools());
        QVERIFY(!validator.start(record, 0));
        const auto update = validator.poll(0);
        QCOMPARE(update.state, RecoveredMediaValidationState::Failed);
        QCOMPARE(update.ownership, DownloadOwnershipAssessment::Conflict);
        QVERIFY(!validator.validatedCandidate());
        QVERIFY(writable(f.final));
    }
    void cancellationRetainsPinsUntilTheDecoderHasPhysicallyStopped() {
        Fixture f;
        QVERIFY(f.prepare(trustedMp3()));
        const auto helper = f.root.filePath("validation-hang.exe");
        QVERIFY(QFile::copy(QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH), helper));
        auto config = tools();
        config.ffmpeg_executable = helper;
        WindowsRecoveredMediaValidator validator(f.config(), config);
        QVERIFY(validator.start(f.record, 0));
        QElapsedTimer clock;
        clock.start();
        while (!QFile::exists(f.publication + "/validation-started") && clock.elapsed() < 5000) {
            QCOMPARE(validator.poll(0).state, RecoveredMediaValidationState::Validating);
            QTest::qWait(5);
        }
        QVERIFY(QFile::exists(f.publication + "/validation-started"));
        validator.cancel();
        QVERIFY(!writable(f.final));
        QVERIFY(!validator.validatedCandidate());
        QCOMPARE(finish(validator).state, RecoveredMediaValidationState::Cancelled);
        QVERIFY(writable(f.final));
    }
    void cancellationOrTimeoutBeforeFirstPollNeverLaunchesAMissingTool_data() {
        QTest::addColumn<bool>("cancelled");
        QTest::newRow("cancel before launch") << true;
        QTest::newRow("expired preparation") << false;
    }
    void cancellationOrTimeoutBeforeFirstPollNeverLaunchesAMissingTool() {
        QFETCH(bool, cancelled);
        Fixture f;
        QVERIFY(f.prepare(trustedMp3()));
        auto config = tools();
        config.ffmpeg_executable = config.ffprobe_executable = f.root.filePath("never-created.exe");
        config.total_timeout_ms = 5;
        WindowsRecoveredMediaValidator validator(f.config(), config);
        QVERIFY(validator.start(f.record, 0));
        if (cancelled)
            validator.cancel();
        const auto update = finish(validator, 5);
        QCOMPARE(update.state,
                 cancelled ? RecoveredMediaValidationState::Cancelled : RecoveredMediaValidationState::Failed);
        QCOMPARE(update.error, cancelled ? DownloadFailure::None : DownloadFailure::Timeout);
        QVERIFY(!validator.validatedCandidate());
        QVERIFY(writable(f.final));
    }
};
QTEST_GUILESS_MAIN(WindowsRecoveredMediaValidatorTests)
#include "WindowsRecoveredMediaValidatorTests.moc"
