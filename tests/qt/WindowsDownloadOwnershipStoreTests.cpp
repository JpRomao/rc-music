#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <AclAPI.h>
#include <Sddl.h>

#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
using download_artifacts::Handle;
using download_artifacts::nativePath;

class Security final {
  public:
    Security() {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return;
        const Handle token(raw);
        DWORD size = 0;
        static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
        std::vector<unsigned char> buffer(size);
        if (buffer.empty() || !GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
            return;
        const auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(user->User.Sid, &text))
            return;
        user_sid = QString::fromWCharArray(text);
        LocalFree(text);
    }
    ~Security() { LocalFree(descriptor_); }
    SECURITY_ATTRIBUTES* attributes(const bool allow_world = false) {
        LocalFree(std::exchange(descriptor_, nullptr));
        const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + user_sid + QLatin1Char(')') +
                          (allow_world ? QStringLiteral("(A;;FR;;;WD)") : QString{});
        if (user_sid.isEmpty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(
                                      sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
            return nullptr;
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
        return &attributes_;
    }
    QString user_sid{};

  private:
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    SECURITY_ATTRIBUTES attributes_{};
};

bool protectedPrivate(HANDLE handle, const QString& user_sid) {
    PSECURITY_DESCRIPTOR security = nullptr;
    PACL dacl = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
                        &security) != ERROR_SUCCESS)
        return false;
    const auto free = qScopeGuard([&] { LocalFree(security); });
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(security, &control, &revision) || !(control & SE_DACL_PROTECTED) || !dacl ||
        dacl->AceCount != 2)
        return false;
    bool user = false, system = false;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw))
            return false;
        const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 || ace->Mask != FILE_ALL_ACCESS)
            return false;
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(const_cast<DWORD*>(&ace->SidStart), &text))
            return false;
        const QString principal = QString::fromWCharArray(text);
        LocalFree(text);
        user |= principal == user_sid;
        system |= principal == QStringLiteral("S-1-5-18");
        if (principal != user_sid && principal != QStringLiteral("S-1-5-18"))
            return false;
    }
    return user && system;
}

struct Fixture final {
    QTemporaryDir root{};
    Security security{};
    QString directory{root.filePath(QStringLiteral("private ownership espaço 音"))};
    bool created{false};
    explicit Fixture(const bool allow_world = false) {
        auto* attributes = security.attributes(allow_world);
        created = root.isValid() && attributes && CreateDirectoryW(nativePath(directory).c_str(), attributes);
    }
    QString journal() const { return directory + QStringLiteral("/download-ownership.json"); }
    QString lock() const { return directory + QStringLiteral("/download-ownership.lock"); }
    QStringList entries() const { return QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
    bool write(const QString& path, const QByteArray& bytes, const bool allow_world = false) {
        auto* attributes = security.attributes(allow_world);
        if (!attributes)
            return false;
        const Handle handle(CreateFileW(nativePath(path).c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                        FILE_SHARE_READ, attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!handle.valid())
            return false;
        DWORD written = 0;
        return WriteFile(handle.get(), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
               written == static_cast<DWORD>(bytes.size()) && FlushFileBuffers(handle.get());
    }
    QByteArray bytes() const {
        QFile file(journal());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    bool privateFile(const QString& path) const {
        const Handle handle(CreateFileW(nativePath(path).c_str(), READ_CONTROL,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        return handle.valid() && protectedPrivate(handle.get(), security.user_sid);
    }
};

DownloadFileIdentity identity(const std::uint8_t value) {
    DownloadFileIdentity result{.volume_serial = 7};
    result.file_id[0] = value;
    return result;
}

DownloadOwnershipRecord planned(const unsigned int number = 1) {
    auto tail = std::to_string(number);
    tail.insert(0, 12 - tail.size(), '0');
    return {.job_id = "job-" + std::to_string(number),
            .workspace_id = "abcdefab-cdef-4abc-8def-" + tail,
            .staging_parent = identity(1),
            .publish_parent = identity(2)};
}

DownloadOwnershipRecord ready(const unsigned int number = 1) {
    auto record = planned(number);
    record.stage = DownloadOwnershipStage::WorkspaceReady;
    record.job_directory = identity(3);
    record.input = identity(4);
    record.output = identity(5);
    return record;
}

DownloadOwnershipRecord prepared(const unsigned int number = 1) {
    auto record = ready(number);
    record.stage = DownloadOwnershipStage::PublicationPrepared;
    record.input_bytes = 6;
    record.output_bytes = 8;
    record.output_sha256.assign(64, 'a');
    return record;
}
} // namespace

class WindowsDownloadOwnershipStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndMissingLoadAreInert() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto before = fixture.entries();
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(fixture.entries(), before);
        const auto result = store.load();
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::Missing);
        QVERIFY(!result.journal);
        QCOMPARE(fixture.entries(), before);
    }
    void plannedRecordAndMonotonicTransitionsSurviveFreshStore() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(store.record(ready()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(store.record(prepared()), DownloadCheckpointSaveStatus::Saved);
        auto record = prepared();
        record.stage = DownloadOwnershipStage::Published;
        QCOMPARE(store.record(record), DownloadCheckpointSaveStatus::Saved);
        record.cleanup_complete = true;
        QCOMPARE(store.record(record), DownloadCheckpointSaveStatus::Saved);
        WindowsDownloadOwnershipStore restarted(fixture.directory);
        const auto result = restarted.load();
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(result.journal);
        QVERIFY(result.journal->records == std::vector{record});
        QCOMPARE(fixture.entries(), QStringList({"download-ownership.json", "download-ownership.lock"}));
    }
    void upsertRetainsOtherRecordsAndIdempotentReplay() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore first(fixture.directory), second(fixture.directory);
        QCOMPARE(first.record(planned(1)), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(second.record(planned(2)), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(first.record(planned(1)), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(first.record(ready(1)), DownloadCheckpointSaveStatus::Saved);
        const auto loaded = second.load();
        QVERIFY(loaded.journal);
        QVERIFY(loaded.journal->records == std::vector({ready(1), planned(2)}));
    }
    void newRecordMustStartPlannedWithoutReplacingExistingData() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore store(fixture.directory);
        QVERIFY(store.record(ready()) != DownloadCheckpointSaveStatus::Saved);
        QVERIFY(!QFileInfo::exists(fixture.journal()));
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        QVERIFY(store.record(prepared(2)) != DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(fixture.bytes(), before);
    }
    void refusesIdentityPhaseAndCleanupRegressions_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 7; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void refusesIdentityPhaseAndCleanupRegressions() {
        QFETCH(int, kind);
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(store.record(ready()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(store.record(prepared()), DownloadCheckpointSaveStatus::Saved);
        auto final = prepared();
        final.stage = DownloadOwnershipStage::Published;
        final.cleanup_complete = true;
        QCOMPARE(store.record(final), DownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        auto conflict = final;
        switch (kind) {
        case 0:
            conflict.workspace_id = planned(2).workspace_id;
            break;
        case 1:
            conflict.job_id = "different-job";
            break;
        case 2:
            conflict.staging_parent = identity(6);
            break;
        case 3:
            conflict.output = identity(6);
            break;
        case 4:
            conflict.output_sha256.assign(64, 'b');
            break;
        case 5:
            conflict.stage = DownloadOwnershipStage::PublicationPrepared;
            break;
        case 6:
            conflict.cleanup_complete = false;
            break;
        }
        QCOMPARE(store.record(conflict), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), before);
    }
    void boundedUpsertDoesNotEvictFullHistoryAndStillReplays() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore store(fixture.directory);
        for (unsigned int i = 1; i <= DownloadOwnershipJournal::maximum_records; ++i)
            QCOMPARE(store.record(planned(i)), DownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        QCOMPARE(store.record(planned(129)), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), before);
        QCOMPARE(store.record(planned(1)), DownloadCheckpointSaveStatus::Saved);
        const auto loaded = store.load();
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), DownloadOwnershipJournal::maximum_records);
        QVERIFY(loaded.journal->records.front() == planned(1));
        QVERIFY(loaded.journal->records.back() == planned(128));
    }
    void invalidIncomingRecordDoesNotCreateLockOrTemporary() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore store(fixture.directory);
        auto invalid = planned();
        invalid.job_id.clear();
        QCOMPARE(store.record(invalid), DownloadCheckpointSaveStatus::Failed);
        QVERIFY(fixture.entries().isEmpty());
    }
    void refusesUnknownExistingDocumentWithoutChangingBytes_data() {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<int>("status");
        QTest::newRow("future") << QByteArray("{\"schemaVersion\":999,\"owner\":\"rc-music-download-ownership\"}")
                                << int(DownloadCheckpointLoadStatus::FutureVersion);
        QTest::newRow("corrupt") << QByteArray("{broken ownership") << int(DownloadCheckpointLoadStatus::Invalid);
        QTest::newRow("unknown-shape") << QByteArray("{\"schemaVersion\":1,\"unexpected\":true}")
                                       << int(DownloadCheckpointLoadStatus::Invalid);
        QTest::newRow("empty") << QByteArray{} << int(DownloadCheckpointLoadStatus::Invalid);
    }
    void refusesUnknownExistingDocumentWithoutChangingBytes() {
        QFETCH(QByteArray, document);
        QFETCH(int, status);
        Fixture fixture;
        QVERIFY(fixture.created);
        QVERIFY(fixture.write(fixture.journal(), document));
        WindowsDownloadOwnershipStore store(fixture.directory);
        const auto loaded = store.load();
        QCOMPARE(int(loaded.status), status);
        QVERIFY(!loaded.journal);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), document);
        QCOMPARE(fixture.entries(), QStringList{"download-ownership.json"});
    }
    void refusesOversizedDocumentBeforeAllocationAndPreservesIt() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const QByteArray bytes(WindowsDownloadOwnershipStore::maximum_file_size + 1, 'x');
        QVERIFY(fixture.write(fixture.journal(), bytes));
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Invalid);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), bytes);
    }
    void protectsJournalLockAndTemporaryWithExactPrivateDacl() {
        Fixture fixture;
        QVERIFY(fixture.created);
        bool observed = false;
        bool private_temporary = false;
        WindowsDownloadOwnershipStore store(
            fixture.directory, {.inject_fault = [&](const auto point) {
                if (point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) {
                    const auto temporaries =
                        QDir(fixture.directory).entryList({"download-ownership-*.tmp"}, QDir::Files);
                    observed = temporaries.size() == 1;
                    private_temporary = observed && fixture.privateFile(fixture.directory + '/' + temporaries[0]);
                }
                return false;
            }});
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(observed);
        QVERIFY(private_temporary);
        QVERIFY(fixture.privateFile(fixture.journal()));
        QVERIFY(fixture.privateFile(fixture.lock()));
    }
    void beforeCommitFailurePreservesOldDocumentAndRemovesOnlyOwnTemporary() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore initial(fixture.directory);
        QCOMPARE(initial.record(planned()), DownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        const auto unknown = fixture.directory + QStringLiteral("/download-ownership-unknown.tmp");
        QVERIFY(fixture.write(unknown, "keep"));
        WindowsDownloadOwnershipStore failing(fixture.directory, {.inject_fault = [](const auto point) {
                                                  return point == WindowsDownloadOwnershipFaultPoint::BeforeCommit;
                                              }});
        QCOMPARE(failing.record(ready()), DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(fixture.bytes(), before);
        QCOMPARE(QDir(fixture.directory).entryList({"*.tmp"}, QDir::Files),
                 QStringList{"download-ownership-unknown.tmp"});
    }
    void afterCommitFailureRemainsFailedAlthoughFreshLoadSeesTheRecord() {
        Fixture fixture;
        QVERIFY(fixture.created);
        unsigned int commits = 0;
        WindowsDownloadOwnershipStore store(fixture.directory, {.inject_fault = [&](const auto point) {
                                                if (point == WindowsDownloadOwnershipFaultPoint::AfterCommit) {
                                                    ++commits;
                                                    return true;
                                                }
                                                return false;
                                            }});
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Failed);
        WindowsDownloadOwnershipStore reader(fixture.directory);
        const auto loaded = reader.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.journal);
        QVERIFY(loaded.journal->records == std::vector{planned()});
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(commits, 2U); // Replay still writes, it does not promote readback.
        QCOMPARE(QDir(fixture.directory).entryList({"*.tmp"}, QDir::Files).size(), 0);
    }
    void upsertRunsUnderExclusiveWriterLockAndRetryDoesNotLoseOtherJobs() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore second(fixture.directory);
        bool observed = false;
        DownloadCheckpointSaveStatus nested = DownloadCheckpointSaveStatus::Saved;
        WindowsDownloadOwnershipStore first(fixture.directory, {.inject_fault = [&](const auto point) {
                                                if (point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) {
                                                    observed = true;
                                                    nested = second.record(planned(2));
                                                }
                                                return false;
                                            }});
        QCOMPARE(first.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(observed);
        QCOMPARE(nested, DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(second.record(planned(2)), DownloadCheckpointSaveStatus::Saved);
        const auto loaded = second.load();
        QVERIFY(loaded.journal);
        QVERIFY(loaded.journal->records == std::vector({planned(1), planned(2)}));
    }
    void deniesBroadDirectoryOrFileAclWithoutRepairingIt_data() {
        QTest::addColumn<bool>("broad_directory");
        QTest::newRow("directory") << true;
        QTest::newRow("file") << false;
    }
    void deniesBroadDirectoryOrFileAclWithoutRepairingIt() {
        QFETCH(bool, broad_directory);
        Fixture fixture(broad_directory);
        QVERIFY(fixture.created);
        if (!broad_directory)
            QVERIFY(fixture.write(fixture.journal(), "unknown", true));
        const auto before = fixture.bytes();
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QVERIFY(store.record(planned()) != DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(fixture.bytes(), before);
    }
    void deniesHardlinkedJournalAndKeepsBothNames() {
        Fixture fixture;
        QVERIFY(fixture.created);
        QVERIFY(fixture.write(fixture.journal(), "unknown"));
        const auto alias = fixture.directory + QStringLiteral("/keep-alias.json");
        QVERIFY(CreateHardLinkW(nativePath(alias).c_str(), nativePath(fixture.journal()).c_str(), nullptr));
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), QByteArray("unknown"));
        QVERIFY(QFileInfo::exists(alias));
    }
    void deniedReplacementSharingPreservesOldDocumentAndExplicitRetryWorks() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadOwnershipStore store(fixture.directory);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        {
            const Handle observer(CreateFileW(nativePath(fixture.journal()).c_str(), GENERIC_READ, FILE_SHARE_READ,
                                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QVERIFY(observer.valid());
            QCOMPARE(store.record(ready()), DownloadCheckpointSaveStatus::Failed);
            QCOMPARE(fixture.bytes(), before);
        }
        QCOMPARE(store.record(ready()), DownloadCheckpointSaveStatus::Saved);
        const auto loaded = store.load();
        QVERIFY(loaded.journal);
        QVERIFY(loaded.journal->records == std::vector{ready()});
    }
    void missingConfiguredDirectoryIsNotCreated() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto missing = root.filePath(QStringLiteral("not-created"));
        WindowsDownloadOwnershipStore store(missing);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Failed);
        QVERIFY(!QFileInfo::exists(missing));
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadOwnershipStoreTests)
#include "WindowsDownloadOwnershipStoreTests.moc"
