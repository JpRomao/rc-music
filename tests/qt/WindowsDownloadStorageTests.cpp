#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsPrivateDownloadDocument.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <AclAPI.h>
#include <Sddl.h>
#include <winioctl.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
using download_artifacts::Handle;
using download_artifacts::nativePath;

WindowsDownloadStoragePaths expected(const QString& parent) {
    const auto root = parent + QStringLiteral("/downloads");
    return {root,
            root + QStringLiteral("/state"),
            root + QStringLiteral("/network"),
            root + QStringLiteral("/media"),
            root + QStringLiteral("/youtube"),
            root + QStringLiteral("/staging"),
            root + QStringLiteral("/published")};
}

std::array<QString, 7> entries(const WindowsDownloadStoragePaths& paths) {
    return {paths.root, paths.state, paths.network, paths.media, paths.youtube, paths.staging, paths.published};
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

class Security final {
  public:
    Security() {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return;
        const Handle token{raw};
        DWORD size = 0;
        static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
        std::vector<unsigned char> bytes(size);
        if (bytes.empty() || !GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
            return;
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(bytes.data())->User.Sid, &text))
            return;
        user_sid = QString::fromWCharArray(text);
        LocalFree(text);
    }
    ~Security() { LocalFree(descriptor_); }
    SECURITY_ATTRIBUTES* attributes(const bool broad = false, const bool inheritance = false) {
        LocalFree(std::exchange(descriptor_, nullptr));
        const auto flags = inheritance ? QStringLiteral("OICI") : QString{};
        const auto sddl = QStringLiteral("D:P(A;") + flags + QStringLiteral(";FA;;;SY)(A;") + flags +
                          QStringLiteral(";FA;;;") + user_sid + QLatin1Char(')') +
                          (broad ? QStringLiteral("(A;;FR;;;WD)") : QString{});
        if (user_sid.isEmpty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(
                                      sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
            return nullptr;
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
        return &attributes_;
    }
    bool create(const QString& path, const bool broad = false, const bool inheritance = false) {
        auto* security = attributes(broad, inheritance);
        return security && CreateDirectoryW(nativePath(path).c_str(), security) != FALSE;
    }
    QString user_sid{};

  private:
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    SECURITY_ATTRIBUTES attributes_{};
};

QString securitySnapshot(const QString& path) {
    const Handle directory{CreateFileW(nativePath(path).c_str(), READ_CONTROL,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!directory.valid() ||
        GetSecurityInfo(directory.get(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, nullptr, nullptr, &descriptor) != ERROR_SUCCESS)
        return {};
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    LPWSTR text = nullptr;
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
            descriptor, SDDL_REVISION_1, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &text, nullptr))
        return {};
    const auto release_text = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}

bool exactlyPrivate(const QString& path, const QString& user_sid) {
    const Handle directory{CreateFileW(nativePath(path).c_str(), READ_CONTROL,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!directory.valid() ||
        GetSecurityInfo(directory.get(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                        nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    LPWSTR owner_text = nullptr;
    if (!owner || !ConvertSidToStringSidW(owner, &owner_text))
        return false;
    const auto release_owner = qScopeGuard([&] { LocalFree(owner_text); });
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (QString::fromWCharArray(owner_text) != user_sid || !dacl || dacl->AceCount != 2 ||
        !GetSecurityDescriptorControl(descriptor, &control, &revision) || !(control & SE_DACL_PROTECTED))
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

bool setOwnedJunction(const QString& directory, const QString& target) {
    struct Header final {
        DWORD tag;
        WORD data_length, reserved, substitute_offset, substitute_length, print_offset, print_length;
    };
    static_assert(sizeof(Header) == 16);
    // Mount-point substitute names use the NT prefix directly, whereas the
    // ordinary open helper prepends the Win32 extended-path prefix.
    const auto print = QDir::toNativeSeparators(target).toStdWString();
    const auto substitute = std::wstring{L"\\??\\"} + print;
    const auto substitute_bytes = substitute.size() * sizeof(wchar_t);
    const auto print_bytes = print.size() * sizeof(wchar_t);
    std::vector<std::byte> bytes(sizeof(Header) + substitute_bytes + print_bytes + 2 * sizeof(wchar_t));
    auto* header = reinterpret_cast<Header*>(bytes.data());
    header->tag = IO_REPARSE_TAG_MOUNT_POINT;
    header->data_length = static_cast<WORD>(bytes.size() - 8);
    header->substitute_length = static_cast<WORD>(substitute_bytes);
    header->print_offset = static_cast<WORD>(substitute_bytes + sizeof(wchar_t));
    header->print_length = static_cast<WORD>(print_bytes);
    std::memcpy(bytes.data() + sizeof(Header), substitute.data(), substitute_bytes);
    std::memcpy(bytes.data() + sizeof(Header) + header->print_offset, print.data(), print_bytes);
    const Handle handle{CreateFileW(nativePath(directory).c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle.valid())
        return false;
    DWORD returned = 0;
    return DeviceIoControl(handle.get(), FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                           nullptr, 0, &returned, nullptr) != FALSE;
}

struct JunctionGuard final {
    QTemporaryDir& owner;
    QString path;
    bool active{false};
    bool unlink() {
        if (!active)
            return true;
        if (!path.startsWith(owner.path() + QLatin1Char('/')) || !RemoveDirectoryW(nativePath(path).c_str())) {
            owner.setAutoRemove(false);
            return false;
        }
        active = false;
        return true;
    }
    ~JunctionGuard() {
        if (!unlink())
            qWarning("Preserved owned TEMP fixture after uncertain junction removal");
    }
};
} // namespace

class WindowsDownloadStorageTests final : public QObject {
    Q_OBJECT
  private slots:
    void layoutIsPureFixedConcatenation_data() {
        QTest::addColumn<QString>("parent");
        QTest::newRow("relative-unvalidated") << QStringLiteral("relative-not-created");
        QTest::newRow("empty-unvalidated") << QString{};
        QTest::newRow("raw-separators") << QStringLiteral("C:\\raw\\..\\config/");
    }
    void layoutIsPureFixedConcatenation() {
        QFETCH(QString, parent);
        const auto wanted = expected(parent);
        const auto actual = entries(windowsDownloadStoragePaths(parent));
        const auto wanted_entries = entries(wanted);
        for (std::size_t index = 0; index < actual.size(); ++index)
            QCOMPARE(actual[index], wanted_entries[index]);
    }
    void freshStorageHasExactSecurityAndBothRealStoresCanUseState() {
        QTemporaryDir config{QDir::tempPath() + QStringLiteral("/RC Music storage espaço-音-XXXXXX")};
        QVERIFY(config.isValid());
        Security security;
        const auto paths = expected(config.path());
        QVERIFY(prepareWindowsDownloadStorage(config.path()));
        QCOMPARE(QDir(paths.root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 (QStringList{"media", "network", "published", "staging", "state", "youtube"}));
        for (const auto& path : entries(paths)) {
            QVERIFY(QFileInfo(path).isDir());
            QVERIFY(exactlyPrivate(path, security.user_sid));
        }
        WindowsDownloadCheckpointStore checkpoint{paths.state};
        QCOMPARE(checkpoint.load().status, DownloadCheckpointLoadStatus::Missing);
        QCOMPARE(checkpoint.save({}), DownloadCheckpointSaveStatus::Saved);
        const auto loaded = checkpoint.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.checkpoint);
        WindowsDownloadOwnershipStore ownership{paths.state};
        QCOMPARE(ownership.load().status, DownloadCheckpointLoadStatus::Missing);
        DownloadOwnershipRecord record{.job_id = "storage-job", .workspace_id = "abcdefab-cdef-4abc-8def-000000000001"};
        record.staging_parent.volume_serial = 7;
        record.staging_parent.file_id[0] = 1;
        record.publish_parent.volume_serial = 7;
        record.publish_parent.file_id[0] = 2;
        QVERIFY(validDownloadOwnershipRecord(record));
        QCOMPARE(ownership.record(record), DownloadCheckpointSaveStatus::Saved);
        const auto journal = ownership.load();
        QCOMPARE(journal.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(journal.journal);
        QCOMPARE(journal.journal->records.size(), std::size_t{1});
        QVERIFY(journal.journal->records.front() == record);
    }
    void exactExistingDirectoriesAreIdempotentAndKeepFilesAndParentSecurity() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Security security;
        const auto paths = expected(config.path());
        for (const auto& path : entries(paths))
            QVERIFY(security.create(path));
        const auto parent_security = securitySnapshot(config.path());
        QVERIFY(!parent_security.isEmpty());
        std::vector<QString> before;
        for (const auto& path : entries(paths)) {
            before.push_back(securitySnapshot(path));
            QVERIFY(!before.back().isEmpty());
            QVERIFY(writeFile(path + QStringLiteral("/preserve.bin"), "owned previous data"));
        }
        QVERIFY(prepareWindowsDownloadStorage(config.path()));
        QVERIFY(prepareWindowsDownloadStorage(config.path()));
        QCOMPARE(securitySnapshot(config.path()), parent_security);
        std::size_t index = 0;
        for (const auto& path : entries(paths)) {
            QCOMPARE(securitySnapshot(path), before[index++]);
            QCOMPARE(readFile(path + QStringLiteral("/preserve.bin")), QByteArray{"owned previous data"});
        }
    }
    void broaderOrInheritableExistingSecurityIsRefusedWithoutRepair_data() {
        QTest::addColumn<bool>("child");
        QTest::addColumn<bool>("inheritance");
        QTest::newRow("root-broad") << false << false;
        QTest::newRow("state-broad") << true << false;
        QTest::newRow("root-OICI") << false << true;
        QTest::newRow("state-OICI") << true << true;
    }
    void broaderOrInheritableExistingSecurityIsRefusedWithoutRepair() {
        QFETCH(bool, child);
        QFETCH(bool, inheritance);
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Security security;
        const auto paths = expected(config.path());
        if (child)
            QVERIFY(security.create(paths.root));
        const auto target = child ? paths.state : paths.root;
        QVERIFY(security.create(target, !inheritance, inheritance));
        const auto before = securitySnapshot(target);
        QVERIFY(!before.isEmpty());
        QVERIFY(writeFile(target + QStringLiteral("/preserve.bin"), "existing ACL data"));
        QVERIFY(!prepareWindowsDownloadStorage(config.path()));
        QCOMPARE(securitySnapshot(target), before);
        QCOMPARE(readFile(target + QStringLiteral("/preserve.bin")), QByteArray{"existing ACL data"});
        QVERIFY(!QFile::exists(paths.network));
    }
    void fileCollisionsAreNeverReplaced_data() {
        QTest::addColumn<bool>("child");
        QTest::newRow("root-file") << false;
        QTest::newRow("state-file") << true;
    }
    void fileCollisionsAreNeverReplaced() {
        QFETCH(bool, child);
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Security security;
        const auto paths = expected(config.path());
        if (child)
            QVERIFY(security.create(paths.root));
        const auto target = child ? paths.state : paths.root;
        QVERIFY(writeFile(target, "collision must survive"));
        QVERIFY(!prepareWindowsDownloadStorage(config.path()));
        QCOMPARE(readFile(target), QByteArray{"collision must survive"});
        QVERIFY(!QFile::exists(paths.network));
    }
    void missingParentIsNotCreatedRecursively() {
        QTemporaryDir owned;
        QVERIFY(owned.isValid());
        const auto missing = owned.filePath(QStringLiteral("not-created/config"));
        QVERIFY(!prepareWindowsDownloadStorage(missing));
        QVERIFY(QDir(owned.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void partialProgressAndUnknownFilesSurviveALaterFailure() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Security security;
        const auto paths = expected(config.path());
        QVERIFY(security.create(paths.root));
        QVERIFY(writeFile(paths.media, "later collision"));
        QVERIFY(writeFile(paths.root + QStringLiteral("/unrelated.keep"), "preserve unrelated"));
        QVERIFY(!prepareWindowsDownloadStorage(config.path()));
        QVERIFY(exactlyPrivate(paths.state, security.user_sid));
        QVERIFY(exactlyPrivate(paths.network, security.user_sid));
        QCOMPARE(readFile(paths.media), QByteArray{"later collision"});
        QCOMPARE(readFile(paths.root + QStringLiteral("/unrelated.keep")), QByteArray{"preserve unrelated"});
        QVERIFY(!QFile::exists(paths.youtube));
    }
    void unsafeParentSyntaxIsRejected_data() {
        QTest::addColumn<QString>("parent");
        QTest::newRow("relative") << QStringLiteral("relative-storage");
        QTest::newRow("UNC") << QStringLiteral("//server.invalid/share/config");
        QTest::newRow("device") << QStringLiteral("//./NUL");
        QTest::newRow("reserved-component") << QStringLiteral("C:/NUL/config");
    }
    void unsafeParentSyntaxIsRejected() {
        QFETCH(QString, parent);
        QVERIFY(!prepareWindowsDownloadStorage(parent));
    }
    void ownedJunctionsAreRejectedAndTargetsPreserved_data() {
        QTest::addColumn<QString>("slot");
        QTest::newRow("parent") << QStringLiteral("parent");
        QTest::newRow("root") << QStringLiteral("root");
        QTest::newRow("state") << QStringLiteral("state");
    }
    void ownedJunctionsAreRejectedAndTargetsPreserved() {
        QFETCH(QString, slot);
        QTemporaryDir owned;
        QVERIFY(owned.isValid());
        Security security;
        const auto config = owned.filePath(QStringLiteral("config"));
        const auto target = owned.filePath(QStringLiteral("target"));
        QVERIFY(QDir{}.mkdir(config));
        QVERIFY(QDir{}.mkdir(target));
        QVERIFY(writeFile(target + QStringLiteral("/preserve.keep"), "junction target"));
        const auto paths = expected(config);
        const auto alias = slot == QStringLiteral("parent") ? owned.filePath(QStringLiteral("config-alias"))
                           : slot == QStringLiteral("root") ? paths.root
                                                            : paths.state;
        if (slot == QStringLiteral("state"))
            QVERIFY(security.create(paths.root));
        QVERIFY(QDir{}.mkdir(alias));
        JunctionGuard guard{owned, alias};
        guard.active = setOwnedJunction(alias, target);
        if (!guard.active)
            QSKIP("Host filesystem did not permit the owned TEMP junction fixture");
        const auto before = QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        QVERIFY(!prepareWindowsDownloadStorage(slot == QStringLiteral("parent") ? alias : config));
        QCOMPARE(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
        QCOMPARE(readFile(target + QStringLiteral("/preserve.keep")), QByteArray{"junction target"});
        QVERIFY(guard.unlink());
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadStorageTests)
#include "WindowsDownloadStorageTests.moc"
