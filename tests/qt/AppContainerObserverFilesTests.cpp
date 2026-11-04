#include "AppContainerObserverFiles.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

using Files = music_player::sandbox_probe::AppContainerObserverFiles;
using music_player::m0::download_artifacts::Handle;
using music_player::m0::download_artifacts::nativePath;
namespace {
QByteArray currentUserSid() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return {};
    const Handle token(raw);
    DWORD needed = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &needed));
    if (!needed || needed > 64 * 1024)
        return {};
    std::vector<unsigned char> bytes(needed);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), needed, &needed))
        return {};
    const auto sid = reinterpret_cast<const TOKEN_USER*>(bytes.data())->User.Sid;
    if (!sid || !IsValidSid(sid))
        return {};
    return {static_cast<const char*>(sid), static_cast<qsizetype>(GetLengthSid(sid))};
}
QString sidText(const QByteArray& sid) {
    LPWSTR text = nullptr;
    if (sid.isEmpty() || !ConvertSidToStringSidW(const_cast<char*>(sid.constData()), &text))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}
QByteArray sidBytes(const wchar_t* text) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(text, &sid))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(sid); });
    return {static_cast<const char*>(sid), static_cast<qsizetype>(GetLengthSid(sid))};
}
QString dacl(const QByteArray& driver, const bool directory) {
    const auto flags = directory ? QStringLiteral("OICI") : QString{};
    return QStringLiteral("D:P(A;") + flags + QStringLiteral(";FA;;;SY)(A;") + flags + QStringLiteral(";FA;;;BA)(A;") +
           flags + QStringLiteral(";FA;;;") + sidText(driver) + ')';
}
struct Security final {
    PSECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    explicit Security(const QString& text) {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1,
                                                                 &descriptor, nullptr))
            attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() { LocalFree(descriptor); }
    bool valid() const { return descriptor != nullptr; }
};
bool createDirectory(const QString& path, const QByteArray& driver, const QString& acl = {}) {
    const Security security(QStringLiteral("O:") + sidText(driver) + (acl.isEmpty() ? dacl(driver, true) : acl));
    return security.valid() &&
           CreateDirectoryW(nativePath(path).c_str(), const_cast<SECURITY_ATTRIBUTES*>(&security.attributes)) != FALSE;
}
Handle createFile(const QString& path, const QByteArray& driver, const DWORD sharing, const QString& acl = {}) {
    const Security security(QStringLiteral("O:") + sidText(driver) + (acl.isEmpty() ? dacl(driver, false) : acl));
    if (!security.valid())
        return Handle{};
    return Handle(CreateFileW(nativePath(path).c_str(), GENERIC_WRITE | READ_CONTROL, sharing,
                              const_cast<SECURITY_ATTRIBUTES*>(&security.attributes), CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
}
bool writeHandle(const Handle& file, const QByteArray& bytes) {
    DWORD written = 0;
    return file.valid() &&
           WriteFile(file.get(), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
           written == static_cast<DWORD>(bytes.size()) && FlushFileBuffers(file.get());
}
bool createBytes(const QString& path, const QByteArray& driver, const QByteArray& bytes, const QString& acl = {}) {
    const auto file = createFile(path, driver, 0, acl);
    return writeHandle(file, bytes);
}
QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QString descriptorText(PSECURITY_DESCRIPTOR descriptor) {
    LPWSTR text = nullptr;
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
            descriptor, SDDL_REVISION_1, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &text, nullptr))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}
QString securityText(const QString& path, const bool directory) {
    const Handle handle(CreateFileW(
        nativePath(path).c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    if (!handle.valid())
        return {};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle.get(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr,
                        nullptr, nullptr, nullptr, &descriptor) != ERROR_SUCCESS)
        return {};
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    return descriptorText(descriptor);
}
Handle mutableSecurity(const QString& path, const bool directory) {
    return Handle(CreateFileW(nativePath(path).c_str(), READ_CONTROL | WRITE_DAC,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
}
DWORD replaceDacl(const HANDLE handle, const QString& acl, const bool protect = true) {
    Security security(acl);
    PACL native_acl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    if (!security.valid() || !GetSecurityDescriptorDacl(security.descriptor, &present, &native_acl, &defaulted) ||
        !present || !native_acl)
        return ERROR_INVALID_SECURITY_DESCR;
    return SetSecurityInfo(handle, SE_FILE_OBJECT,
                           DACL_SECURITY_INFORMATION |
                               (protect ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
                           nullptr, nullptr, native_acl, nullptr);
}
struct DaclFacts final {
    struct Ace final {
        QByteArray trustee{};
        DWORD mask{};
        BYTE flags{};
    };
    bool valid{false};
    bool protected_dacl{false};
    DWORD count{};
    std::vector<BYTE> flags{};
    std::vector<Ace> aces{};
};
DaclFacts daclFacts(const HANDLE handle) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
                        &descriptor) != ERROR_SUCCESS)
        return {};
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!acl || !GetSecurityDescriptorControl(descriptor, &control, &revision))
        return {};
    DaclFacts result{true, (control & SE_DACL_PROTECTED) != 0, acl->AceCount, {}, {}};
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(acl, index, &raw) || !raw)
            return {};
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
            ace->Header.AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + 8)
            return {};
        auto* sid = const_cast<DWORD*>(&ace->SidStart);
        if (!IsValidSid(sid) || GetLengthSid(sid) > ace->Header.AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart))
            return {};
        result.flags.push_back(ace->Header.AceFlags);
        result.aces.push_back(
            {QByteArray{reinterpret_cast<const char*>(sid), static_cast<qsizetype>(GetLengthSid(sid))}, ace->Mask,
             ace->Header.AceFlags});
    }
    return result;
}
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music Observer Files 日本-XXXXXX")};
    QByteArray driver{currentUserSid()};
    QString directory{parent.filePath(QStringLiteral("channel com espaços"))};
    bool initialize(const QString& acl = {}) {
        return parent.isValid() && !driver.isEmpty() && createDirectory(directory, driver, acl);
    }
    QString file(const Files::Slot slot) const {
        switch (slot) {
        case Files::Slot::Request:
            return directory + QStringLiteral("/wfp-request.json");
        case Files::Slot::Ready:
            return directory + QStringLiteral("/wfp-ready.json");
        case Files::Slot::Result:
            return directory + QStringLiteral("/wfp-result.json");
        }
        return {};
    }
};
// Owned junction only; both target and link remain inside one QTemporaryDir.
bool setJunction(const QString& path, const QString& target) {
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
    const Handle handle(CreateFileW(nativePath(path).c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    DWORD returned = 0;
    return handle.valid() && DeviceIoControl(handle.get(), FSCTL_SET_REPARSE_POINT, bytes.data(),
                                             static_cast<DWORD>(bytes.size()), nullptr, 0, &returned, nullptr);
}
} // namespace

class AppContainerObserverFilesTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionIsInertAndUnopenedOperationsRefuse() {
        Fixture f;
        QVERIFY(f.parent.isValid());
        Files files({f.directory, f.driver});
        QVERIFY(files.driverSid().isEmpty());
        QVERIFY(!QFile::exists(f.directory));
        QCOMPARE(files.read(Files::Slot::Request).status, Files::ReadStatus::Unsafe);
        QCOMPARE(files.writeNew(Files::Slot::Request, QByteArray{"{}"}).status, Files::WriteStatus::Unsafe);
        QVERIFY(!QFile::exists(f.directory));
    }
    void openPinsExistingDirectoryAndDerivesTheDriverOwner_data() {
        QTest::addColumn<bool>("bootstrap");
        QTest::newRow("explicit") << false;
        QTest::newRow("bootstrap from owner") << true;
    }
    void openPinsExistingDirectoryAndDerivesTheDriverOwner() {
        QFETCH(bool, bootstrap);
        Fixture f;
        QVERIFY(f.initialize());
        const auto original = securityText(f.directory, true);
        Files files({f.directory, bootstrap ? QByteArray{} : f.driver});
        QVERIFY(files.driverSid().isEmpty());
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        QCOMPARE(files.driverSid(), f.driver);
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        QCOMPARE(securityText(f.directory, true), original);
        const auto read = files.read(Files::Slot::Request);
        QCOMPARE(read.status, Files::ReadStatus::Missing);
        QVERIFY(read.bytes.isEmpty() && !read.lease);
    }
    void openNeverCreatesAMissingDirectory() {
        Fixture f;
        QVERIFY(f.parent.isValid());
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::IoError);
        QVERIFY(files.driverSid().isEmpty());
        QVERIFY(!QFile::exists(f.directory));
    }
    void openRejectsUntrustedPathsAndDriverSids_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 6; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void openRejectsUntrustedPathsAndDriverSids() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        auto path = f.directory;
        auto sid = f.driver;
        if (kind == 0)
            path += QStringLiteral("/../channel com espaços");
        if (kind == 1)
            path = QStringLiteral("relative/channel");
        if (kind == 2)
            sid = QByteArray{"invalid SID"};
        if (kind == 3)
            sid = sidBytes(L"S-1-5-18");
        if (kind == 4)
            sid = sidBytes(L"S-1-5-32-544");
        if (kind == 5)
            sid = sidBytes(L"S-1-5-21-101-102-103-104");
        const auto original = securityText(f.directory, true);
        Files files({path, sid});
        QCOMPARE(files.open().status, Files::OpenStatus::Unsafe);
        QVERIFY(files.driverSid().isEmpty());
        QCOMPARE(securityText(f.directory, true), original);
        QVERIFY(QDir(f.directory).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }
    void directoryDaclMustContainExactlyTheThreeProtectedGrants_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void directoryDaclMustContainExactlyTheThreeProtectedGrants() {
        QFETCH(int, kind);
        Fixture f;
        auto acl = dacl(f.driver, true);
        if (kind == 0)
            acl += QStringLiteral("(A;OICI;GR;;;WD)");
        if (kind == 1) {
            // Identical ACEs are coalesced by Windows. These grants must stay
            // separate: root read access and inherited-only full access have
            // different scopes and masks for the same trustee.
            acl = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;FR;;;") + sidText(f.driver) +
                  QStringLiteral(")(A;OICIIO;FA;;;") + sidText(f.driver) + ')';
        }
        if (kind == 2)
            acl = dacl(f.driver, false);
        if (kind == 3)
            acl.replace(QStringLiteral("D:P"), QStringLiteral("D:"));
        if (kind == 4)
            acl.replace(QStringLiteral("(A;OICI;FA;;;BA)"), QString{});
        // Creation may normalize duplicate ACEs or inheritance controls. Set
        // the adversarial ACL on the owned fixture and inspect its actual SD.
        QVERIFY(f.initialize());
        const auto mutable_directory = mutableSecurity(f.directory, true);
        QVERIFY2(mutable_directory.valid(), qPrintable(QString::number(GetLastError())));
        QCOMPARE(replaceDacl(mutable_directory.get(), acl, kind != 3), static_cast<DWORD>(ERROR_SUCCESS));
        const auto facts = daclFacts(mutable_directory.get());
        QVERIFY(facts.valid);
        if (kind == 0) {
            QVERIFY(facts.count > 3);
            const auto world = sidBytes(L"S-1-1-0");
            QVERIFY(std::any_of(facts.aces.begin(), facts.aces.end(),
                                [&](const auto& ace) { return ace.trustee == world; }));
        }
        if (kind == 1) {
            QVERIFY(facts.count > 3);
            std::vector<DaclFacts::Ace> duplicates;
            for (const auto& ace : facts.aces)
                if (ace.trustee == f.driver)
                    duplicates.push_back(ace);
            QVERIFY(duplicates.size() >= 2);
            QVERIFY(std::any_of(duplicates.begin() + 1, duplicates.end(), [&](const auto& ace) {
                return ace.flags != duplicates.front().flags || ace.mask != duplicates.front().mask;
            }));
        }
        if (kind == 2) {
            QCOMPARE(facts.count, 3UL);
            QVERIFY(std::all_of(facts.flags.begin(), facts.flags.end(), [](auto flags) { return flags == 0; }));
        }
        if (kind == 3)
            QVERIFY(!facts.protected_dacl);
        else
            QVERIFY(facts.protected_dacl);
        if (kind == 4)
            QCOMPARE(facts.count, 2UL);
        const auto original = securityText(f.directory, true);
        Files files({f.directory, {}});
        QCOMPARE(files.open().status, Files::OpenStatus::Unsafe);
        QVERIFY(files.driverSid().isEmpty());
        QCOMPARE(securityText(f.directory, true), original);
    }
    void writesOnlyTheThreeFixedLeavesAndNeverOverwrites() {
        Fixture f;
        QVERIFY(f.initialize());
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        for (const auto slot : {Files::Slot::Request, Files::Slot::Ready, Files::Slot::Result}) {
            const auto content = QByteArray{"{\"slot\":"} + QByteArray::number(static_cast<int>(slot)) + '}';
            QCOMPARE(files.writeNew(slot, content).status, Files::WriteStatus::Written);
            QCOMPARE(readFile(f.file(slot)), content);
            const auto original = securityText(f.file(slot), false);
            const Security expected(QStringLiteral("O:") + sidText(f.driver) + dacl(f.driver, false));
            QVERIFY(expected.valid());
            QCOMPARE(original, descriptorText(expected.descriptor));
            QCOMPARE(files.writeNew(slot, QByteArray{"replacement"}).status, Files::WriteStatus::Exists);
            QCOMPARE(readFile(f.file(slot)), content);
            QCOMPARE(securityText(f.file(slot), false), original);
            const auto result = files.read(slot);
            QCOMPARE(result.status, Files::ReadStatus::Ready);
            QCOMPARE(result.bytes, content);
            QVERIFY(result.lease);
        }
        QCOMPARE(QDir(f.directory).entryList(QDir::Files).size(), 3);
    }
    void readsAcceptZeroThroughExactlySixteenKiB_data() {
        QTest::addColumn<int>("bytes");
        QTest::newRow("empty IO is left for the codec to reject") << 0;
        QTest::newRow("ordinary frame") << 23;
        QTest::newRow("upper bound") << static_cast<int>(Files::maximum_file_size);
    }
    void readsAcceptZeroThroughExactlySixteenKiB() {
        QFETCH(int, bytes);
        Fixture f;
        QVERIFY(f.initialize());
        const QByteArray content(bytes, 'x');
        QVERIFY(createBytes(f.file(Files::Slot::Request), f.driver, content));
        Files files({f.directory, {}});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        const auto result = files.read(Files::Slot::Request);
        QCOMPARE(result.status, Files::ReadStatus::Ready);
        QCOMPARE(result.bytes, content);
        QVERIFY(result.lease);
    }
    void boundedWritesAndReadsNeverPublishOversizedBytes() {
        Fixture f;
        QVERIFY(f.initialize());
        const QByteArray maximum(Files::maximum_file_size, 'm');
        const QByteArray excessive(Files::maximum_file_size + 1, 'e');
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        QCOMPARE(files.writeNew(Files::Slot::Ready, maximum).status, Files::WriteStatus::Written);
        QCOMPARE(readFile(f.file(Files::Slot::Ready)), maximum);
        QCOMPARE(files.writeNew(Files::Slot::Result, excessive).status, Files::WriteStatus::TooLarge);
        QVERIFY(!QFile::exists(f.file(Files::Slot::Result)));
        QVERIFY(createBytes(f.file(Files::Slot::Request), f.driver, excessive));
        const auto result = files.read(Files::Slot::Request);
        QCOMPARE(result.status, Files::ReadStatus::TooLarge);
        QVERIFY(result.bytes.isEmpty() && !result.lease);
        QCOMPARE(readFile(f.file(Files::Slot::Request)), excessive);
    }
    void busyWriterIsNotReadUntilItsExclusiveHandleCloses() {
        Fixture f;
        QVERIFY(f.initialize());
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        auto writer = createFile(f.file(Files::Slot::Request), f.driver, 0);
        QVERIFY(writeHandle(writer, QByteArray{"complete only after close"}));
        const auto busy = files.read(Files::Slot::Request);
        QCOMPARE(busy.status, Files::ReadStatus::Busy);
        QVERIFY(busy.bytes.isEmpty() && !busy.lease);
        writer = Handle{};
        const auto ready = files.read(Files::Slot::Request);
        QCOMPARE(ready.status, Files::ReadStatus::Ready);
        QCOMPARE(ready.bytes, QByteArray{"complete only after close"});
        QVERIFY(ready.lease);
    }
    void readLeasePreventsRewriteDeleteAndSwapEvenAfterTheReaderIsDestroyed() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto path = f.file(Files::Slot::Request);
        QVERIFY(createBytes(path, f.driver, QByteArray{"pinned bytes"}));
        auto files = std::make_unique<Files>(Files::Config{f.directory, f.driver});
        QCOMPARE(files->open().status, Files::OpenStatus::Opened);
        auto read = files->read(Files::Slot::Request);
        QCOMPARE(read.status, Files::ReadStatus::Ready);
        QVERIFY(read.lease);
        files.reset();
        const Handle writer(CreateFileW(nativePath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        QVERIFY(!writer.valid());
        QCOMPARE(GetLastError(), static_cast<DWORD>(ERROR_SHARING_VIOLATION));
        QVERIFY(!DeleteFileW(nativePath(path).c_str()));
        QCOMPARE(GetLastError(), static_cast<DWORD>(ERROR_SHARING_VIOLATION));
        const auto moved_directory = f.parent.filePath(QStringLiteral("swapped channel"));
        QVERIFY(!MoveFileExW(nativePath(f.directory).c_str(), nativePath(moved_directory).c_str(), 0));
        QCOMPARE(GetLastError(), static_cast<DWORD>(ERROR_SHARING_VIOLATION));
        QCOMPARE(read.bytes, QByteArray{"pinned bytes"});
        read.lease.reset();
        const auto renamed = f.parent.filePath(QStringLiteral("released request.json"));
        QVERIFY(QFile::rename(path, renamed));
        QCOMPARE(readFile(renamed), QByteArray{"pinned bytes"});
    }
    void rejectsHardlinksAndPreservesAllAliases() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto path = f.file(Files::Slot::Request);
        const auto alias = f.parent.filePath(QStringLiteral("alias.json"));
        QVERIFY(createBytes(path, f.driver, QByteArray{"two names, no authority"}));
        QVERIFY(CreateHardLinkW(nativePath(alias).c_str(), nativePath(path).c_str(), nullptr));
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        const auto result = files.read(Files::Slot::Request);
        QCOMPARE(result.status, Files::ReadStatus::Unsafe);
        QVERIFY(result.bytes.isEmpty() && !result.lease);
        QCOMPARE(readFile(path), QByteArray{"two names, no authority"});
        QCOMPARE(readFile(alias), QByteArray{"two names, no authority"});
    }
    void rejectsReparseDirectoriesAndAncestorsWithoutFollowingThem_data() {
        QTest::addColumn<bool>("ancestor");
        QTest::newRow("channel is junction") << false;
        QTest::newRow("ancestor is junction") << true;
    }
    void rejectsReparseDirectoriesAndAncestorsWithoutFollowingThem() {
        QFETCH(bool, ancestor);
        Fixture f;
        QVERIFY(f.initialize());
        const auto nested = f.directory + QStringLiteral("/nested");
        QVERIFY(createDirectory(nested, f.driver));
        const auto link = f.parent.filePath(QStringLiteral("owned junction"));
        QVERIFY(QDir{}.mkdir(link));
        QVERIFY(setJunction(link, f.directory));
        const auto remove_junction = qScopeGuard([&] { RemoveDirectoryW(nativePath(link).c_str()); });
        Files files({ancestor ? link + QStringLiteral("/nested") : link, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Unsafe);
        QVERIFY(files.driverSid().isEmpty());
        QVERIFY(QDir(nested).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }
    void rejectsAlteredFileDaclAndDirectorySlotsWithoutRepair_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void rejectsAlteredFileDaclAndDirectorySlotsWithoutRepair() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        const auto path = f.file(Files::Slot::Request);
        auto acl = dacl(f.driver, false);
        if (kind == 0)
            acl += QStringLiteral("(A;;GR;;;WD)");
        if (kind == 1)
            acl = dacl(f.driver, true);
        if (kind == 2)
            acl.replace(QStringLiteral("(A;;FA;;;BA)"), QString{});
        if (kind == 3)
            acl.replace(QStringLiteral("D:P"), QStringLiteral("D:"));
        if (kind == 4)
            QVERIFY(createDirectory(path, f.driver));
        else
            QVERIFY(createBytes(path, f.driver, QByteArray{"keep unknown bytes"}, acl));
        const auto original = securityText(path, kind == 4);
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        const auto result = files.read(Files::Slot::Request);
        QCOMPARE(result.status, Files::ReadStatus::Unsafe);
        QVERIFY(result.bytes.isEmpty() && !result.lease);
        QVERIFY(files.writeNew(Files::Slot::Request, QByteArray{"do not replace"}).status !=
                Files::WriteStatus::Written);
        QCOMPARE(securityText(path, kind == 4), original);
        if (kind != 4)
            QCOMPARE(readFile(path), QByteArray{"keep unknown bytes"});
    }
    void revalidatesTheOpenedDirectoryBeforeReadingOrWriting() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto path = f.file(Files::Slot::Request);
        QVERIFY(createBytes(path, f.driver, QByteArray{"keep"}));
        // Model mutation through authority retained before the reader's pins.
        const auto mutable_directory = mutableSecurity(f.directory, true);
        QVERIFY2(mutable_directory.valid(), qPrintable(QString::number(GetLastError())));
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        const auto altered = dacl(f.driver, true) + QStringLiteral("(A;OICI;GR;;;WD)");
        QCOMPARE(replaceDacl(mutable_directory.get(), altered), static_cast<DWORD>(ERROR_SUCCESS));
        const auto restore = qScopeGuard([&] { replaceDacl(mutable_directory.get(), dacl(f.driver, true)); });
        const auto facts = daclFacts(mutable_directory.get());
        QVERIFY(facts.valid && facts.protected_dacl);
        QVERIFY(facts.count > 3);
        const auto world = sidBytes(L"S-1-1-0");
        QVERIFY(
            std::any_of(facts.aces.begin(), facts.aces.end(), [&](const auto& ace) { return ace.trustee == world; }));
        const auto before = securityText(f.directory, true);
        QCOMPARE(files.read(Files::Slot::Request).status, Files::ReadStatus::Unsafe);
        QCOMPARE(files.writeNew(Files::Slot::Ready, QByteArray{"refuse"}).status, Files::WriteStatus::Unsafe);
        QCOMPARE(files.open().status, Files::OpenStatus::Unsafe);
        QCOMPARE(files.driverSid(), f.driver);
        QCOMPARE(securityText(f.directory, true), before);
        QCOMPARE(readFile(path), QByteArray{"keep"});
        QVERIFY(!QFile::exists(f.file(Files::Slot::Ready)));
    }
    void unknownSlotsPreserveUnrelatedFilesAndDirectoryAcl() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto unknown = f.directory + QStringLiteral("/do-not-repair.json");
        QVERIFY(createBytes(unknown, f.driver, QByteArray{"unrelated receipt"}));
        const auto before = securityText(f.directory, true);
        Files files({f.directory, f.driver});
        QCOMPARE(files.open().status, Files::OpenStatus::Opened);
        const auto slot = static_cast<Files::Slot>(99);
        const auto read = files.read(slot);
        QCOMPARE(read.status, Files::ReadStatus::Unsafe);
        QVERIFY(read.bytes.isEmpty() && !read.lease);
        QCOMPARE(files.writeNew(slot, QByteArray{"refuse"}).status, Files::WriteStatus::Unsafe);
        QCOMPARE(readFile(unknown), QByteArray{"unrelated receipt"});
        QCOMPARE(securityText(f.directory, true), before);
        QCOMPARE(QDir(f.directory).entryList(QDir::Files), QStringList{QStringLiteral("do-not-repair.json")});
    }
};

QTEST_GUILESS_MAIN(AppContainerObserverFilesTests)
#include "AppContainerObserverFilesTests.moc"
