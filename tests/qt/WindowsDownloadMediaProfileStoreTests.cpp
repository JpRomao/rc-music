#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/DownloadMediaProfileJson.hpp"
#include "adapters/DownloadOwnershipJson.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadMediaProfileStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <AclAPI.h>
#include <Sddl.h>
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
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
        std::vector<unsigned char> bytes(size);
        if (bytes.empty() || !GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
            return;
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &text))
            return;
        user_sid = QString::fromWCharArray(text);
        LocalFree(text);
    }
    ~Security() { LocalFree(descriptor_); }
    SECURITY_ATTRIBUTES* attributes(const bool broad = false) {
        LocalFree(std::exchange(descriptor_, nullptr));
        const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + user_sid + ')' +
                          (broad ? QStringLiteral("(A;;FR;;;WD)") : QString{});
        if (user_sid.isEmpty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(
                                      sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
            return nullptr;
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
        return &attributes_;
    }
    QString user_sid;

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
    const auto cleanup = qScopeGuard([&] { LocalFree(security); });
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
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
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
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
struct Fixture final {
    QTemporaryDir root;
    Security security;
    QString directory{root.filePath(QStringLiteral("private profile espaço 音"))};
    bool created{false};
    explicit Fixture(const bool broad = false) {
        auto* attributes = security.attributes(broad);
        created = root.isValid() && attributes && CreateDirectoryW(nativePath(directory).c_str(), attributes);
    }
    QString journal() const { return directory + QStringLiteral("/download-media-profile.json"); }
    QString lock() const { return directory + QStringLiteral("/download-media-profile.lock"); }
    QString ownerLock() const { return directory + QStringLiteral("/download-media-profile-owner.lock"); }
    QStringList entries() const { return QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
    bool write(const QString& path, const QByteArray& bytes, const bool broad = false) {
        auto* attributes = security.attributes(broad);
        if (!attributes)
            return false;
        const Handle handle(CreateFileW(nativePath(path).c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                        FILE_SHARE_READ, attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD count = 0;
        return handle.valid() &&
               WriteFile(handle.get(), bytes.constData(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
               count == static_cast<DWORD>(bytes.size()) && FlushFileBuffers(handle.get());
    }
    bool privateFile(const QString& path) const {
        const Handle handle(CreateFileW(nativePath(path).c_str(), READ_CONTROL,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        return handle.valid() && protectedPrivate(handle.get(), security.user_sid);
    }
};
DownloadMediaProfileRecord planned(const unsigned int number = 1) {
    auto suffix = std::to_string(number);
    suffix.insert(0, 12 - suffix.size(), '0');
    return {.job_id = "job-" + std::to_string(number), .operation_id = "abcdefab-cdef-4abc-8def-" + suffix};
}
DownloadMediaProfileRecord created() {
    auto value = planned();
    value.stage = DownloadMediaProfileStage::Created;
    DownloadMediaProfileSid sid{};
    sid[0] = 1;
    sid[1] = 8;
    sid[7] = 15;
    sid[8] = 2;
    for (std::size_t index = 1; index < 8; ++index)
        sid[8 + index * 4] = static_cast<std::uint8_t>(index);
    value.sid = sid;
    return value;
}
DownloadMediaProfileRecord stage(const DownloadMediaProfileStage next) {
    auto value = created();
    value.stage = next;
    return value;
}
bool junction(const QString& directory, const QString& target) {
    struct Header final {
        DWORD tag;
        WORD length, reserved, substitute_offset, substitute_length, print_offset, print_length;
    };
    static_assert(sizeof(Header) == 16);
    const auto print = QDir::toNativeSeparators(target).toStdWString();
    const auto substitute = std::wstring(L"\\??\\") + print;
    const auto substitute_bytes = substitute.size() * sizeof(wchar_t);
    const auto print_bytes = print.size() * sizeof(wchar_t);
    std::vector<std::byte> bytes(sizeof(Header) + substitute_bytes + print_bytes + 2 * sizeof(wchar_t));
    auto* header = reinterpret_cast<Header*>(bytes.data());
    header->tag = IO_REPARSE_TAG_MOUNT_POINT;
    header->length = static_cast<WORD>(bytes.size() - 8);
    header->substitute_length = static_cast<WORD>(substitute_bytes);
    header->print_offset = static_cast<WORD>(substitute_bytes + sizeof(wchar_t));
    header->print_length = static_cast<WORD>(print_bytes);
    std::memcpy(bytes.data() + sizeof(Header), substitute.data(), substitute_bytes);
    std::memcpy(bytes.data() + sizeof(Header) + header->print_offset, print.data(), print_bytes);
    const Handle handle(CreateFileW(nativePath(directory).c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    DWORD returned = 0;
    return handle.valid() && DeviceIoControl(handle.get(), FSCTL_SET_REPARSE_POINT, bytes.data(),
                                             static_cast<DWORD>(bytes.size()), nullptr, 0, &returned, nullptr);
}
struct OwnedJunction final {
    QTemporaryDir& root;
    QString path;
    bool armed{false};
    void arm() {
        armed = true;
        root.setAutoRemove(false);
    }
    ~OwnedJunction() {
        if (!armed || !path.startsWith(root.path() + '/'))
            return;
        const auto attributes = GetFileAttributesW(nativePath(path).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) && RemoveDirectoryW(nativePath(path).c_str()))
            root.setAutoRemove(true);
    }
};
} // namespace

class WindowsDownloadMediaProfileStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndMissingLoadDoNotCreateFiles() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore store(h.directory);
        QVERIFY(h.entries().isEmpty());
        const auto loaded = store.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Missing);
        QVERIFY(!loaded.record);
        QVERIFY(h.entries().isEmpty());
    }
    void exclusiveLeaseOutlivesStoreAndReleasesWithoutDeletingItsLock() {
        Fixture h;
        QVERIFY(h.created);
        std::unique_ptr<DownloadMediaProfileLease> lease;
        {
            WindowsDownloadMediaProfileStore first(h.directory);
            lease = first.acquireLease();
            QVERIFY(lease);
        }
        WindowsDownloadMediaProfileStore second(h.directory);
        QVERIFY(!second.acquireLease());
        const auto raw = CreateFileW(nativePath(h.ownerLock()).c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        const auto error = GetLastError();
        const Handle blocked(raw);
        QVERIFY(!blocked.valid());
        QCOMPARE(error, DWORD(ERROR_SHARING_VIOLATION));
        lease.reset();
        QVERIFY(QFileInfo::exists(h.ownerLock()));
        QCOMPARE(QFileInfo(h.ownerLock()).size(), qint64(0));
        QVERIFY(h.privateFile(h.ownerLock()));
        QCOMPARE(h.entries(), QStringList{"download-media-profile-owner.lock"});
        lease = second.acquireLease();
        QVERIFY(lease);
    }
    void journalWriterAndPreviousDocumentsRemainIndependentOfTheLiveLease() {
        Fixture h;
        QVERIFY(h.created);
        const auto checkpoint_bytes = DownloadCheckpointJson::encode({});
        const auto ownership_bytes = DownloadOwnershipJson::encode({});
        const auto checkpoint = h.directory + "/download-checkpoint.json";
        const auto ownership = h.directory + "/download-ownership.json";
        QVERIFY(h.write(checkpoint, checkpoint_bytes));
        QVERIFY(h.write(ownership, ownership_bytes));
        WindowsDownloadMediaProfileStore store(h.directory);
        auto lease = store.acquireLease();
        QVERIFY(lease);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(store.record(created()), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(store.load().record == created());
        QVERIFY(!WindowsDownloadMediaProfileStore(h.directory).acquireLease());
        QCOMPARE(read(checkpoint), checkpoint_bytes);
        QCOMPARE(read(ownership), ownership_bytes);
        lease.reset();
        // A loaded active record may be inspected under a new lease, but it
        // does not become a new operation or grant profile adoption authority.
        lease = WindowsDownloadMediaProfileStore(h.directory).acquireLease();
        QVERIFY(lease);
        QCOMPARE(store.record(planned(2)), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QVERIFY(store.load().record == created());
        QCOMPARE(read(checkpoint), checkpoint_bytes);
        QCOMPARE(read(ownership), ownership_bytes);
    }
    void nonPrivateOrNonemptyOwnerLockIsNeverAdopted_data() {
        QTest::addColumn<bool>("broad");
        QTest::newRow("broad-dacl") << true;
        QTest::newRow("nonempty") << false;
    }
    void nonPrivateOrNonemptyOwnerLockIsNeverAdopted() {
        QFETCH(bool, broad);
        Fixture h;
        QVERIFY(h.created);
        const auto bytes = broad ? QByteArray{} : QByteArray{"unknown owner lock"};
        QVERIFY(h.write(h.ownerLock(), bytes, broad));
        WindowsDownloadMediaProfileStore store(h.directory);
        QVERIFY(!store.acquireLease());
        QVERIFY(QFileInfo::exists(h.ownerLock()));
        QCOMPARE(read(h.ownerLock()), bytes);
        QCOMPARE(h.entries(), QStringList{"download-media-profile-owner.lock"});
    }
    void hardlinkedOwnerLockIsPreservedUnderBothNames() {
        Fixture h;
        QVERIFY(h.created);
        QVERIFY(h.write(h.ownerLock(), {}));
        const auto alias = h.directory + "/keep-owner.lock";
        QVERIFY(CreateHardLinkW(nativePath(alias).c_str(), nativePath(h.ownerLock()).c_str(), nullptr));
        WindowsDownloadMediaProfileStore store(h.directory);
        QVERIFY(!store.acquireLease());
        QVERIFY(QFileInfo::exists(h.ownerLock()) && QFileInfo::exists(alias));
        QCOMPARE(QFileInfo(h.ownerLock()).size(), qint64(0));
        QCOMPARE(QFileInfo(alias).size(), qint64(0));
        QCOMPARE(h.entries().size(), 2);
    }
    void reparseOwnerLockDoesNotFollowOrChangeItsTarget() {
        Fixture h;
        QVERIFY(h.created);
        const auto target = h.root.filePath("owner-lock-target");
        QVERIFY(CreateDirectoryW(nativePath(target).c_str(), h.security.attributes()));
        const auto marker = target + "/keep.txt";
        QVERIFY(h.write(marker, "preserve"));
        QVERIFY(CreateDirectoryW(nativePath(h.ownerLock()).c_str(), h.security.attributes()));
        OwnedJunction cleanup{h.root, h.ownerLock()};
        if (!junction(h.ownerLock(), target))
            QSKIP("Owned temporary junction creation is unavailable on this host");
        cleanup.arm();
        WindowsDownloadMediaProfileStore store(h.directory);
        QVERIFY(!store.acquireLease());
        QCOMPARE(read(marker), QByteArray{"preserve"});
        QCOMPARE(QDir(target).entryList(QDir::Files), QStringList{"keep.txt"});
        const auto attributes = GetFileAttributesW(nativePath(h.ownerLock()).c_str());
        QVERIFY(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
    }
    void leaseDoesNotFollowAReparseConfiguredDirectory() {
        Fixture h;
        QVERIFY(h.created);
        const auto alias = h.root.filePath("owner-alias");
        QVERIFY(CreateDirectoryW(nativePath(alias).c_str(), h.security.attributes()));
        OwnedJunction cleanup{h.root, alias};
        if (!junction(alias, h.directory))
            QSKIP("Owned temporary junction creation is unavailable on this host");
        cleanup.arm();
        WindowsDownloadMediaProfileStore store(alias);
        QVERIFY(!store.acquireLease());
        QVERIFY(h.entries().isEmpty());
    }
    void completeLifecycleSurvivesFreshStoreWithoutProfileApis() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore store(h.directory);
        for (const auto& value :
             {planned(), created(), stage(DownloadMediaProfileStage::InUse),
              stage(DownloadMediaProfileStage::CleanupPending), stage(DownloadMediaProfileStage::Removed)})
            QCOMPARE(store.record(value), DownloadCheckpointSaveStatus::Saved);
        const auto loaded = WindowsDownloadMediaProfileStore(h.directory).load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.record == stage(DownloadMediaProfileStage::Removed));
        QCOMPARE(h.entries(), QStringList({"download-media-profile.json", "download-media-profile.lock"}));
    }
    void invalidIncomingAndMissingNonPlannedDoNotCreateJournal() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore store(h.directory);
        auto invalid = planned();
        invalid.job_id.clear();
        QCOMPARE(store.record(invalid), DownloadCheckpointSaveStatus::Failed);
        QVERIFY(h.entries().isEmpty());
        QCOMPARE(store.record(created()), DownloadCheckpointSaveStatus::Failed);
        QVERIFY(!QFileInfo::exists(h.journal()));
    }
    void activeSlotAndImmutableIdentityAreNotOverwritten() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore store(h.directory);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(store.record(created()), DownloadCheckpointSaveStatus::Saved);
        const auto bytes = read(h.journal());
        auto different_job = created();
        different_job.job_id = "different-job";
        auto different_sid = created();
        (*different_sid.sid)[12] = 99;
        for (const auto& conflict : {planned(2), different_job, different_sid, planned()}) {
            QCOMPARE(store.record(conflict), DownloadCheckpointSaveStatus::BlockedByExistingData);
            QCOMPARE(read(h.journal()), bytes);
        }
    }
    void terminalReplacementIsAtomicAndNeverEvictsAnActiveOperation_data() {
        QTest::addColumn<bool>("abandoned");
        QTest::newRow("removed") << false;
        QTest::newRow("abandoned") << true;
    }
    void terminalReplacementIsAtomicAndNeverEvictsAnActiveOperation() {
        QFETCH(bool, abandoned);
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore store(h.directory);
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        auto terminal = planned();
        if (abandoned) {
            terminal.stage = DownloadMediaProfileStage::Abandoned;
        } else {
            QCOMPARE(store.record(created()), DownloadCheckpointSaveStatus::Saved);
            QCOMPARE(store.record(stage(DownloadMediaProfileStage::InUse)), DownloadCheckpointSaveStatus::Saved);
            QCOMPARE(store.record(stage(DownloadMediaProfileStage::CleanupPending)),
                     DownloadCheckpointSaveStatus::Saved);
            terminal = stage(DownloadMediaProfileStage::Removed);
        }
        QCOMPARE(store.record(terminal), DownloadCheckpointSaveStatus::Saved);
        const auto before = read(h.journal());
        WindowsDownloadMediaProfileStore failing(h.directory, {.inject_fault = [](const auto point) {
                                                     return point ==
                                                            WindowsDownloadMediaProfileFaultPoint::BeforeCommit;
                                                 }});
        QCOMPARE(failing.record(planned(2)), DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(read(h.journal()), before);
        QCOMPARE(store.record(planned(2)), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(store.load().record == planned(2));
        QCOMPARE(store.record(planned(3)), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QVERIFY(store.load().record == planned(2));
    }
    void coexistencePreservesCheckpointAndOwnershipBytesAndTheirSchemas() {
        Fixture h;
        QVERIFY(h.created);
        const auto checkpoint_bytes = DownloadCheckpointJson::encode({});
        const auto ownership_bytes = DownloadOwnershipJson::encode({});
        QVERIFY(!checkpoint_bytes.isEmpty() && !ownership_bytes.isEmpty());
        WindowsPrivateDownloadDocument checkpoint(
            h.directory, WindowsPrivateDownloadDocumentKind::Checkpoint, [](const QByteArrayView bytes) {
                return DownloadCheckpointJson::decode(bytes).status == DownloadCheckpointJsonStatus::Valid
                           ? DownloadCheckpointLoadStatus::Loaded
                           : DownloadCheckpointLoadStatus::Invalid;
            });
        WindowsPrivateDownloadDocument ownership(
            h.directory, WindowsPrivateDownloadDocumentKind::Ownership,
            [](const QByteArrayView bytes) { return DownloadOwnershipJson::decode(bytes).status; });
        QCOMPARE(checkpoint.update([&](const auto&) {
            return WindowsPrivateDownloadDocumentMutation{.status = DownloadCheckpointSaveStatus::Saved,
                                                          .bytes = checkpoint_bytes};
        }),
                 DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(ownership.update([&](const auto&) {
            return WindowsPrivateDownloadDocumentMutation{.status = DownloadCheckpointSaveStatus::Saved,
                                                          .bytes = ownership_bytes};
        }),
                 DownloadCheckpointSaveStatus::Saved);
        WindowsDownloadMediaProfileStore profile(h.directory);
        QCOMPARE(profile.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(profile.record(created()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(checkpoint.load().bytes, checkpoint_bytes);
        QCOMPARE(ownership.load().bytes, ownership_bytes);
        QCOMPARE(read(h.directory + "/download-checkpoint.json"), checkpoint_bytes);
        QCOMPARE(read(h.directory + "/download-ownership.json"), ownership_bytes);
        QCOMPARE(h.entries().size(), 6);
    }
    void unknownOrFutureDocumentsArePreservedWithoutCreatingWriterLock_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("status");
        auto future = QJsonDocument::fromJson(DownloadMediaProfileJson::encode(planned())).object();
        future["schemaVersion"] = 999;
        QTest::newRow("future") << QJsonDocument(future).toJson(QJsonDocument::Compact)
                                << int(DownloadCheckpointLoadStatus::FutureVersion);
        QTest::newRow("corrupt") << QByteArray("{broken profile") << int(DownloadCheckpointLoadStatus::Invalid);
        QTest::newRow("shape") << QByteArray("{\"unexpected\":true}") << int(DownloadCheckpointLoadStatus::Invalid);
        QTest::newRow("empty") << QByteArray{} << int(DownloadCheckpointLoadStatus::Invalid);
    }
    void unknownOrFutureDocumentsArePreservedWithoutCreatingWriterLock() {
        QFETCH(QByteArray, bytes);
        QFETCH(int, status);
        Fixture h;
        QVERIFY(h.created);
        QVERIFY(h.write(h.journal(), bytes));
        WindowsDownloadMediaProfileStore store(h.directory);
        const auto loaded = store.load();
        QCOMPARE(int(loaded.status), status);
        QVERIFY(!loaded.record);
        QVERIFY(!store.acquireLease());
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(read(h.journal()), bytes);
        QCOMPARE(h.entries(), QStringList{"download-media-profile.json"});
    }
    void eightKiBBoundaryIsInclusiveAndOversizePreservesBytes_data() {
        QTest::addColumn<int>("extra");
        QTest::newRow("exact") << 0;
        QTest::newRow("over") << 1;
    }
    void eightKiBBoundaryIsInclusiveAndOversizePreservesBytes() {
        QFETCH(int, extra);
        Fixture h;
        QVERIFY(h.created);
        auto bytes = DownloadMediaProfileJson::encode(planned());
        QVERIFY(!bytes.isEmpty());
        bytes.append(WindowsDownloadMediaProfileStore::maximum_file_size + extra - bytes.size(), ' ');
        QVERIFY(h.write(h.journal(), bytes));
        WindowsDownloadMediaProfileStore store(h.directory);
        QCOMPARE(store.load().status,
                 extra ? DownloadCheckpointLoadStatus::Invalid : DownloadCheckpointLoadStatus::Loaded);
        if (extra) {
            QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::BlockedByExistingData);
            QCOMPARE(read(h.journal()), bytes);
        } else {
            QVERIFY(store.load().record == planned());
            QCOMPARE(store.record(created()), DownloadCheckpointSaveStatus::Saved);
            QVERIFY(store.load().record == created());
        }
    }
    void journalLockAndFreshTemporaryHaveExactPrivateDacl() {
        Fixture h;
        QVERIFY(h.created);
        bool observed = false, private_temporary = false;
        WindowsDownloadMediaProfileStore store(
            h.directory, {.inject_fault = [&](const auto point) {
                if (point == WindowsDownloadMediaProfileFaultPoint::BeforeCommit) {
                    const auto files = QDir(h.directory).entryList({"download-media-profile-*.tmp"}, QDir::Files);
                    observed = files.size() == 1;
                    private_temporary = observed && h.privateFile(h.directory + '/' + files[0]);
                }
                return false;
            }});
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(observed && private_temporary);
        QVERIFY(h.privateFile(h.journal()));
        QVERIFY(h.privateFile(h.lock()));
    }
    void beforeCommitFailureKeepsPriorBytesAndOnlyCleansItsOwnTemporary() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore initial(h.directory);
        QCOMPARE(initial.record(planned()), DownloadCheckpointSaveStatus::Saved);
        const auto before = read(h.journal());
        QVERIFY(h.write(h.directory + "/download-media-profile-unknown.tmp", "preserve"));
        WindowsDownloadMediaProfileStore failing(h.directory, {.inject_fault = [](const auto point) {
                                                     return point ==
                                                            WindowsDownloadMediaProfileFaultPoint::BeforeCommit;
                                                 }});
        QCOMPARE(failing.record(created()), DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(read(h.journal()), before);
        QCOMPARE(QDir(h.directory).entryList({"*.tmp"}, QDir::Files),
                 QStringList{"download-media-profile-unknown.tmp"});
    }
    void afterCommitFailureIsNotPromotedByLoadAndIdenticalRetryWritesAgain() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore initial(h.directory);
        QCOMPARE(initial.record(planned()), DownloadCheckpointSaveStatus::Saved);
        int commits = 0;
        WindowsDownloadMediaProfileStore failing(h.directory, {.inject_fault = [&](const auto point) {
                                                     if (point == WindowsDownloadMediaProfileFaultPoint::AfterCommit) {
                                                         ++commits;
                                                         return true;
                                                     }
                                                     return false;
                                                 }});
        QCOMPARE(failing.record(created()), DownloadCheckpointSaveStatus::Failed);
        QVERIFY(failing.load().record == created());
        QCOMPARE(failing.record(created()), DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(commits, 2);
        int rewrites = 0;
        WindowsDownloadMediaProfileStore retry(h.directory, {.inject_fault = [&](const auto point) {
                                                   if (point == WindowsDownloadMediaProfileFaultPoint::BeforeCommit)
                                                       ++rewrites;
                                                   return false;
                                               }});
        QCOMPARE(retry.record(created()), DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(rewrites, 1);
        QVERIFY(retry.load().record == created());
    }
    void nestedWriterIsRefusedAndCannotReplaceTheActiveSlot() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore second(h.directory);
        bool observed = false;
        auto nested = DownloadCheckpointSaveStatus::Saved;
        WindowsDownloadMediaProfileStore first(h.directory, {.inject_fault = [&](const auto point) {
                                                   if (point == WindowsDownloadMediaProfileFaultPoint::BeforeCommit) {
                                                       observed = true;
                                                       nested = second.record(planned(2));
                                                   }
                                                   return false;
                                               }});
        QCOMPARE(first.record(planned()), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(observed);
        QCOMPARE(nested, DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(second.record(planned(2)), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QVERIFY(second.load().record == planned());
    }
    void broadDirectoryOrJournalAclIsRefusedWithoutRepair_data() {
        QTest::addColumn<bool>("directory");
        QTest::newRow("directory") << true;
        QTest::newRow("file") << false;
    }
    void broadDirectoryOrJournalAclIsRefusedWithoutRepair() {
        QFETCH(bool, directory);
        Fixture h(directory);
        QVERIFY(h.created);
        if (!directory)
            QVERIFY(h.write(h.journal(), "preserve", true));
        const auto before = read(h.journal());
        WindowsDownloadMediaProfileStore store(h.directory);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QVERIFY(!store.acquireLease());
        QVERIFY(!QFileInfo::exists(h.ownerLock()));
        QVERIFY(store.record(planned()) != DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(read(h.journal()), before);
    }
    void hardlinkedJournalIsPreservedUnderBothNames() {
        Fixture h;
        QVERIFY(h.created);
        QVERIFY(h.write(h.journal(), "preserve"));
        const auto alias = h.directory + "/keep.json";
        QVERIFY(CreateHardLinkW(nativePath(alias).c_str(), nativePath(h.journal()).c_str(), nullptr));
        WindowsDownloadMediaProfileStore store(h.directory);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QVERIFY(!store.acquireLease());
        QVERIFY(!QFileInfo::exists(h.ownerLock()));
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(read(h.journal()), QByteArray{"preserve"});
        QCOMPARE(read(alias), QByteArray{"preserve"});
    }
    void reparseConfiguredDirectoryDoesNotFollowOrChangeItsTarget() {
        Fixture h;
        QVERIFY(h.created);
        WindowsDownloadMediaProfileStore original(h.directory);
        QCOMPARE(original.record(planned()), DownloadCheckpointSaveStatus::Saved);
        const auto before = read(h.journal());
        const auto alias = h.root.filePath("alias");
        QVERIFY(CreateDirectoryW(nativePath(alias).c_str(), h.security.attributes()));
        OwnedJunction cleanup{h.root, alias};
        if (!junction(alias, h.directory))
            QSKIP("Owned temporary junction creation is unavailable on this host");
        cleanup.arm();
        WindowsDownloadMediaProfileStore store(alias);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QVERIFY(store.record(created()) != DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(read(h.journal()), before);
    }
    void missingDirectoryIsNotCreated() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto directory = root.filePath("not-created");
        WindowsDownloadMediaProfileStore store(directory);
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Unreadable);
        QVERIFY(!store.acquireLease());
        QCOMPARE(store.record(planned()), DownloadCheckpointSaveStatus::Failed);
        QVERIFY(!QFileInfo::exists(directory));
    }
};
QTEST_GUILESS_MAIN(WindowsDownloadMediaProfileStoreTests)
#include "WindowsDownloadMediaProfileStoreTests.moc"
