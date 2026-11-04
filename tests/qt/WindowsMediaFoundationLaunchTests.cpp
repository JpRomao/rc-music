#include "adapters/WindowsDownloadAppContainer.hpp"
#include "adapters/WindowsMediaFoundationLaunch.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <ShlObj.h>
#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace music_player::m0;
namespace {
const QByteArray synthetic_bytes{"trusted synthetic worker bytes, not an executable"};
// Canonical SID bytes for ACL-only tests. No profile is created or launched.
constexpr auto synthetic_container_sid = L"S-1-15-2-11-22-33-44-55-66-77";

QByteArray containerSid() {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(synthetic_container_sid, &sid))
        return {};
    const QByteArray bytes(static_cast<const char*>(sid), static_cast<qsizetype>(GetLengthSid(sid)));
    LocalFree(sid);
    return bytes;
}

QByteArray readFile(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QByteArray fileHash(const QString& path) {
    return QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex();
}
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music MF launch 日本-XXXXXX")};
    QString source{parent.filePath(QStringLiteral("source com espaços.bin"))};
    QString runtime{parent.filePath(QStringLiteral("private runtimes"))};
    bool initialize() { return parent.isValid() && QDir().mkdir(runtime) && writeFile(source, synthetic_bytes); }
    WindowsMediaFoundationLaunchConfig config() const { return {runtime, source, fileHash(source)}; }
    QStringList runtimes() const { return QDir(runtime).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
};
struct OwnedHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~OwnedHandle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};

QString securityText(const QString& path, const bool directory) {
    const OwnedHandle handle{CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(), READ_CONTROL,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                         directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
        return {};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle.value, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr,
                        nullptr, nullptr, nullptr, &descriptor) != ERROR_SUCCESS)
        return {};
    LPWSTR text = nullptr;
    const bool converted = ConvertSecurityDescriptorToStringSecurityDescriptorW(
                               descriptor, SDDL_REVISION_1, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                               &text, nullptr) != FALSE;
    const auto result = converted ? QString::fromWCharArray(text) : QString{};
    LocalFree(text);
    LocalFree(descriptor);
    return result;
}

bool hasExactContainerAcl(const QString& path, const bool directory, const QByteArray& sid) {
    const OwnedHandle handle{CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(), READ_CONTROL,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                         directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
        return false;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    PSID owner = nullptr;
    if (GetSecurityInfo(handle.value, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                        nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    struct Guard final {
        PSECURITY_DESCRIPTOR descriptor;
        ~Guard() { LocalFree(descriptor); }
    } guard{descriptor};
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!dacl || dacl->AceCount != 3 || !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        !(control & SE_DACL_PROTECTED))
        return false;
    bool found_owner = false, found_system = false, found_container = false;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw))
            return false;
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0)
            return false;
        auto* trustee = const_cast<DWORD*>(&ace->SidStart);
        if (EqualSid(trustee, const_cast<char*>(sid.constData()))) {
            if (found_container || ace->Mask != (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE))
                return false;
            found_container = true;
        } else if (IsWellKnownSid(trustee, WinLocalSystemSid)) {
            if (found_system || ace->Mask != FILE_ALL_ACCESS)
                return false;
            found_system = true;
        } else if (owner && EqualSid(trustee, owner)) {
            if (found_owner || ace->Mask != FILE_ALL_ACCESS)
                return false;
            found_owner = true;
        } else
            return false;
    }
    return found_owner && found_system && found_container;
}

bool replaceContainerGrant(const QString& path, const bool directory, const DWORD mask) {
    const OwnedHandle handle{CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(),
                                         READ_CONTROL | WRITE_DAC,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                         directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
        return false;
    PSECURITY_DESCRIPTOR original = nullptr;
    PSID owner = nullptr;
    if (GetSecurityInfo(handle.value, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr,
                        &original) != ERROR_SUCCESS)
        return false;
    LPWSTR owner_text = nullptr;
    const bool converted = ConvertSidToStringSidW(owner, &owner_text) != FALSE;
    const auto owner_sid = converted ? QString::fromWCharArray(owner_text) : QString{};
    LocalFree(owner_text);
    LocalFree(original);
    if (owner_sid.isEmpty())
        return false;
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + owner_sid + QStringLiteral(")(A;;0x") +
                      QString::number(mask, 16) + QStringLiteral(";;;") +
                      QString::fromWCharArray(synthetic_container_sid) + ')';
    PSECURITY_DESCRIPTOR replacement = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1,
                                                              &replacement, nullptr))
        return false;
    PACL dacl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    const bool success =
        GetSecurityDescriptorDacl(replacement, &present, &dacl, &defaulted) && present && dacl &&
        SetSecurityInfo(handle.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
    LocalFree(replacement);
    return success;
}

// This creates only an owned temporary directory junction. It neither requires
// developer-mode file symlinks nor invokes a shell or changes host settings.
bool setSyntheticJunction(const QString& directory, const QString& target) {
    struct Header final {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
    };
    static_assert(sizeof(Header) == 16);
    const auto print = QDir::toNativeSeparators(target).toStdWString();
    const auto substitute = std::wstring(L"\\??\\") + print;
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
    const OwnedHandle handle{CreateFileW(QDir::toNativeSeparators(directory).toStdWString().c_str(), GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;
    return DeviceIoControl(handle.value, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                           nullptr, 0, &returned, nullptr) != FALSE;
}
struct OwnedJunction final {
    QTemporaryDir& owner;
    const QString path;
    bool armed{false};
    ~OwnedJunction() { static_cast<void>(remove()); }
    void retain() {
        armed = true;
        owner.setAutoRemove(false);
    }
    bool remove() {
        if (!armed)
            return true;
        if (!QDir::isAbsolutePath(owner.path()) || !path.startsWith(owner.path() + '/'))
            return false;
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        const auto attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !RemoveDirectoryW(native.c_str()))
            return false;
        armed = false;
        owner.setAutoRemove(true);
        return true;
    }
};
} // namespace

class WindowsMediaFoundationLaunchTests final : public QObject {
    Q_OBJECT
  private slots:
    void requiredContainerWithoutCapabilityIsRejectedBeforeCreation() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.require_app_container = true;
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void containerRuntimeHasExactReadExecuteAclAndDoesNotChangeParentsOrSource() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto parent_acl = securityText(h.parent.path(), true);
        const auto runtime_parent_acl = securityText(h.runtime, true);
        const auto source_acl = securityText(h.source, false);
        QVERIFY(!parent_acl.isEmpty() && !runtime_parent_acl.isEmpty() && !source_acl.isEmpty());
        auto config = h.config();
        config.app_container = WindowsDownloadAppContainer::fromTrustedSid(containerSid());
        QVERIFY(config.app_container);
        auto lease = WindowsMediaFoundationLaunch::prepare(config);
        QVERIFY(lease);
        const auto process = lease->processConfig();
        QVERIFY(hasExactContainerAcl(lease->directory(), true, config.app_container->sidBytes()));
        QVERIFY(hasExactContainerAcl(process.executable, false, config.app_container->sidBytes()));
        QVERIFY(lease->unchanged());
        QCOMPARE(process.app_container, config.app_container);
        QVERIFY(process.require_app_container);
        QCOMPARE(securityText(h.parent.path(), true), parent_acl);
        QCOMPARE(securityText(h.runtime, true), runtime_parent_acl);
        QCOMPARE(securityText(h.source, false), source_acl);
        const auto directory = lease->directory();
        lease.reset();
        QVERIFY(!QDir(directory).exists());
    }
    void containerProcessEnvironmentUsesKnownLocalAppDataWithoutInheritingHostValues() {
        Fixture h;
        QVERIFY(h.initialize());
        PWSTR local_app_data = nullptr;
        const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local_app_data);
        const auto free_path = qScopeGuard([&] { CoTaskMemFree(local_app_data); });
        QVERIFY(SUCCEEDED(status));
        QVERIFY(local_app_data);
        const auto expected = QDir::fromNativeSeparators(QString::fromWCharArray(local_app_data));
        QVERIFY(!expected.isEmpty() && QDir::isAbsolutePath(expected));

        const auto previous = qgetenv("LOCALAPPDATA");
        const bool previously_set = qEnvironmentVariableIsSet("LOCALAPPDATA");
        const auto restore_environment = qScopeGuard([&] {
            if (previously_set)
                qputenv("LOCALAPPDATA", previous);
            else
                qunsetenv("LOCALAPPDATA");
        });
        const auto untrusted = h.parent.filePath(QStringLiteral("must-not-be-inherited"));
        QVERIFY(qputenv("LOCALAPPDATA", untrusted.toUtf8()));

        auto config = h.config();
        config.app_container = WindowsDownloadAppContainer::fromTrustedSid(containerSid());
        QVERIFY(config.app_container);
        auto lease = WindowsMediaFoundationLaunch::prepare(config);
        QVERIFY(lease);
        const auto process = lease->processConfig();
        QVERIFY(process.environment);
        QCOMPARE(process.environment->keys().size(), 3);
        QCOMPARE(process.environment->value(QStringLiteral("PATH")), QString{});
        QVERIFY(!process.environment->value(QStringLiteral("SystemRoot")).isEmpty());
        QCOMPARE(QDir::fromNativeSeparators(process.environment->value(QStringLiteral("LOCALAPPDATA"))), expected);
        QVERIFY(lease->unchanged());
    }
    void containerAclMutationInvalidatesHeldRuntime_data() {
        QTest::addColumn<bool>("directory");
        QTest::addColumn<quint32>("mask");
        QTest::newRow("directory overgrant") << true << quint32{FILE_ALL_ACCESS};
        QTest::newRow("executable overgrant") << false << quint32{FILE_ALL_ACCESS};
        QTest::newRow("directory loses execute") << true << quint32{FILE_GENERIC_READ};
        QTest::newRow("executable loses execute") << false << quint32{FILE_GENERIC_READ};
    }
    void containerAclMutationInvalidatesHeldRuntime() {
        QFETCH(bool, directory);
        QFETCH(quint32, mask);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.app_container = WindowsDownloadAppContainer::fromTrustedSid(containerSid());
        QVERIFY(config.app_container);
        auto lease = WindowsMediaFoundationLaunch::prepare(config);
        QVERIFY(lease);
        QVERIFY(lease->unchanged());
        const auto target = directory ? lease->directory() : lease->processConfig().executable;
        QVERIFY(replaceContainerGrant(target, directory, mask));
        QVERIFY(!lease->unchanged());
        QVERIFY(replaceContainerGrant(target, directory, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE));
        QVERIFY(lease->unchanged());
    }
    void refusesMissingAndMalformedTrust_data() {
        QTest::addColumn<int>("mutation");
        QTest::newRow("empty configuration") << 0;
        QTest::newRow("missing parent") << 1;
        QTest::newRow("missing source") << 2;
        QTest::newRow("missing hash") << 3;
        QTest::newRow("short hash") << 4;
        QTest::newRow("uppercase hash") << 5;
        QTest::newRow("relative parent") << 6;
        QTest::newRow("relative source") << 7;
        QTest::newRow("URL source") << 8;
        QTest::newRow("UNC parent") << 9;
    }
    void refusesMissingAndMalformedTrust() {
        QFETCH(int, mutation);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        switch (mutation) {
        case 0:
            config = {};
            break;
        case 1:
            config.runtime_parent.clear();
            break;
        case 2:
            config.source_executable.clear();
            break;
        case 3:
            config.source_sha256.clear();
            break;
        case 4:
            config.source_sha256 = QByteArray(63, 'a');
            break;
        case 5:
            config.source_sha256 = QByteArray(64, 'A');
            break;
        case 6:
            config.runtime_parent = QStringLiteral("relative/runtime");
            break;
        case 7:
            config.source_executable = QStringLiteral("relative/source.exe");
            break;
        case 8:
            config.source_executable = QStringLiteral("https://media.example.test/worker.exe");
            break;
        case 9:
            config.runtime_parent = QStringLiteral("//server/share/runtime");
            break;
        default:
            QFAIL("Unknown test mutation");
        }
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
        QCOMPARE(readFile(h.source), synthetic_bytes);
    }
    void refusesParentWithoutLaunchPathHeadroom() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.runtime_parent = QStringLiteral("C:/") + QString(8180, QLatin1Char('a'));
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void refusesMismatchBeforeCreatingPrivateDirectory() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.source_sha256 = QByteArray(64, '0');
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::HashMismatch);
        QVERIFY(h.runtimes().isEmpty());
        QCOMPARE(readFile(h.source), synthetic_bytes);
    }
    void refusesMissingSourceBeforeCreatingPrivateDirectory() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.source_executable = h.parent.filePath(QStringLiteral("missing.exe"));
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
    }
    void preparesOneAuthenticatedCopyWithFixedProcessConfiguration() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto config = h.config();
        WindowsMediaFoundationLaunchError error{};
        auto lease = WindowsMediaFoundationLaunch::prepare(config, &error);
        QVERIFY(lease);
        QCOMPARE(error, WindowsMediaFoundationLaunchError::None);
        QVERIFY(lease->unchanged());
        const auto directory = lease->directory();
        QVERIFY(directory.startsWith(h.runtime + '/'));
        const auto process = lease->processConfig();
        QVERIFY(!process.app_container);
        QVERIFY(!process.require_app_container);
        QCOMPARE(process.executable, directory + QStringLiteral("/RCMusicMediaWorker.exe"));
        QCOMPARE(process.working_directory, directory);
        QVERIFY(process.arguments.isEmpty());
        QVERIFY(process.standard_input.isEmpty());
        QVERIFY(process.environment);
        QCOMPARE(process.environment->keys().size(), 2);
        QVERIFY(process.environment->contains(QStringLiteral("PATH")));
        QCOMPARE(process.environment->value(QStringLiteral("PATH")), QString{});
        QVERIFY(!process.environment->value(QStringLiteral("SystemRoot")).isEmpty());
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("RCMusicMediaWorker.exe")});
        QCOMPARE(readFile(process.executable), synthetic_bytes);
        QCOMPARE(fileHash(process.executable), config.source_sha256);
        QCOMPARE(readFile(h.source), synthetic_bytes);
        lease.reset();
        QVERIFY(!QDir(directory).exists());
        QCOMPARE(readFile(h.source), synthetic_bytes);
        QVERIFY(h.runtimes().isEmpty());
    }
    void copiedRuntimeDoesNotReopenTheMutableSource() {
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto destination = lease->processConfig().executable;
        QFile source{h.source};
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray replacement{"new source bytes after authenticated copy"};
        QCOMPARE(source.write(replacement), replacement.size());
        source.close();
        QVERIFY(lease->unchanged());
        QCOMPARE(readFile(destination), synthetic_bytes);
        QCOMPARE(readFile(h.source), replacement);
    }
    void retainsReadOnlyDestinationAndDirectoryPins() {
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto executable = lease->processConfig().executable;
        QFile writer{executable};
        QVERIFY(!writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(!QFile::rename(executable, executable + QStringLiteral(".replaced")));
        QVERIFY(!QFile::remove(executable));
        QVERIFY(!QDir().rename(directory, directory + QStringLiteral(".replaced")));
        QVERIFY(lease->unchanged());
        QCOMPARE(readFile(executable), synthetic_bytes);
        lease.reset();
        QVERIFY(!QDir(directory).exists());
    }
    void refusesSourceWithAnExistingWriter() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto config = h.config();
        const OwnedHandle writer{CreateFileW(QDir::toNativeSeparators(h.source).toStdWString().c_str(), GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        QVERIFY(writer.value != INVALID_HANDLE_VALUE);
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
    }
    void refusesExtraLoaderControlAndPreservesUnknownFiles_data() {
        QTest::addColumn<QString>("leaf");
        QTest::newRow("DLL") << QStringLiteral("unexpected.dll");
        QTest::newRow("DLL redirection") << QStringLiteral("RCMusicMediaWorker.exe.local");
        QTest::newRow("external manifest") << QStringLiteral("RCMusicMediaWorker.exe.manifest");
        QTest::newRow("Qt configuration") << QStringLiteral("qt.conf");
    }
    void refusesExtraLoaderControlAndPreservesUnknownFiles() {
        QFETCH(QString, leaf);
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto extra = directory + '/' + leaf;
        const QByteArray marker{"unknown file must remain untouched"};
        QVERIFY(writeFile(extra, marker));
        QVERIFY(!lease->unchanged());
        lease.reset();
        QCOMPARE(readFile(extra), marker);
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{leaf});
        QCOMPARE(readFile(h.source), synthetic_bytes);
        QVERIFY(QFile::remove(extra));
        QVERIFY(QDir().rmdir(directory));
    }
    void refusesReparseSourceAncestorsAndRuntimeParents_data() {
        QTest::addColumn<bool>("source_alias");
        QTest::newRow("source ancestor") << true;
        QTest::newRow("runtime parent") << false;
    }
    void refusesReparseSourceAncestorsAndRuntimeParents() {
        QFETCH(bool, source_alias);
        Fixture h;
        QVERIFY(h.initialize());
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const auto target = outside.filePath(QStringLiteral("source.bin"));
        QVERIFY(writeFile(target, synthetic_bytes));
        const auto alias = h.parent.filePath(QStringLiteral("alias"));
        QVERIFY(QDir().mkdir(alias));
        OwnedJunction junction{h.parent, alias};
        if (!setSyntheticJunction(alias, outside.path()))
            QSKIP("Owned temporary junction creation is not available on this host");
        junction.retain();
        auto config = h.config();
        if (source_alias)
            config.source_executable = alias + QStringLiteral("/source.bin");
        else
            config.runtime_parent = alias;
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
        QCOMPARE(QDir(outside.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("source.bin")});
        QCOMPARE(readFile(target), synthetic_bytes);
        QVERIFY(junction.remove());
    }
    void cleanupPreservesAnUnknownJunctionWithoutTraversingIt() {
        Fixture h;
        QVERIFY(h.initialize());
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const auto target = outside.filePath(QStringLiteral("sentinel.bin"));
        const QByteArray sentinel{"outside fixture must not be traversed or removed"};
        QVERIFY(writeFile(target, sentinel));
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto alias = directory + QStringLiteral("/unknown-junction");
        QVERIFY(QDir().mkdir(alias));
        OwnedJunction junction{h.parent, alias};
        if (!setSyntheticJunction(alias, outside.path()))
            QSKIP("Owned temporary junction creation is not available on this host");
        junction.retain();
        QVERIFY(!lease->unchanged());
        lease.reset();
        QCOMPARE(readFile(target), sentinel);
        const auto attributes = GetFileAttributesW(QDir::toNativeSeparators(alias).toStdWString().c_str());
        QVERIFY(attributes != INVALID_FILE_ATTRIBUTES);
        QVERIFY(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("unknown-junction")});
        QVERIFY(junction.remove());
        QVERIFY(QDir().rmdir(directory));
    }
};
QTEST_GUILESS_MAIN(WindowsMediaFoundationLaunchTests)
#include "WindowsMediaFoundationLaunchTests.moc"
