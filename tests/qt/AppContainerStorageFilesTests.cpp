#include "../windows/sandbox/AppContainerStorageFiles.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using namespace Qt::StringLiterals;
using Files = music_player::sandbox_probe::AppContainerStorageFiles;
using Inventory = music_player::sandbox_probe::AppContainerStorageInventory;
using SecurityReader = music_player::sandbox_probe::AppContainerStorageSecurity;
namespace {
constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
struct Handle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() {
        if (valid())
            CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value != nullptr && value != INVALID_HANDLE_VALUE; }
};
std::wstring native(const QString& path) { return (u"\\\\?\\"_s + QDir::toNativeSeparators(path)).toStdWString(); }
QByteArray currentSid() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return {};
    const Handle token(raw);
    DWORD needed = 0;
    static_cast<void>(GetTokenInformation(raw, TokenUser, nullptr, 0, &needed));
    if (!needed || needed > 64 * 1024)
        return {};
    std::vector<unsigned char> bytes(needed);
    if (!GetTokenInformation(raw, TokenUser, bytes.data(), needed, &needed))
        return {};
    const auto sid = reinterpret_cast<const TOKEN_USER*>(bytes.data())->User.Sid;
    return sid && IsValidSid(sid) ? QByteArray{static_cast<const char*>(sid), static_cast<qsizetype>(GetLengthSid(sid))}
                                  : QByteArray{};
}
QString sidText(const QByteArray& value) {
    LPWSTR text = nullptr;
    if (value.isEmpty() || !ConvertSidToStringSidW(const_cast<char*>(value.constData()), &text))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}
struct Descriptor final {
    PSECURITY_DESCRIPTOR value{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    explicit Descriptor(const QString& sddl) {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &value,
                                                                 nullptr))
            attributes.lpSecurityDescriptor = value;
    }
    ~Descriptor() { LocalFree(value); }
};
QString acl(const QByteArray& user, const bool directory, const bool deny_data = false) {
    const auto text = sidText(user);
    const auto flags = directory ? u"OICI"_s : QString{};
    return u"O:"_s + text + u"D:P"_s + (deny_data ? u"(D;;0x1;;;"_s + text + ')' : QString{}) + u"(A;"_s + flags +
           u";FA;;;SY)(A;"_s + flags + u";FA;;;"_s + text + ')';
}
bool createDirectory(const QString& path, const QByteArray& user) {
    Descriptor descriptor(acl(user, true));
    return descriptor.value && CreateDirectoryW(native(path).c_str(), &descriptor.attributes);
}
bool createFile(const QString& path, const QByteArray& user, const bool deny_data = false) {
    Descriptor descriptor(acl(user, false, deny_data));
    // Fixture creation is allowed even when subsequent FILE_READ_DATA opens are denied.
    const Handle file(CreateFileW(native(path).c_str(), FILE_WRITE_DATA, share_all, &descriptor.attributes, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    const std::array<char, 4> content{'d', 'a', 't', 'a'};
    DWORD written = 0;
    return descriptor.value && file.valid() &&
           WriteFile(file.value, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
           written == content.size();
}
DWORD changeDacl(const QString& path, const QByteArray& user) {
    const Handle handle(CreateFileW(native(path).c_str(), READ_CONTROL | WRITE_DAC, share_all, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Descriptor descriptor(acl(user, false) + u"(A;;GR;;;WD)"_s);
    if (!handle.valid())
        return GetLastError();
    PACL dacl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    if (!descriptor.value || !GetSecurityDescriptorDacl(descriptor.value, &present, &dacl, &defaulted) || !present ||
        !dacl)
        return ERROR_INVALID_SECURITY_DESCR;
    return SetSecurityInfo(handle.value, SE_FILE_OBJECT,
                           DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl,
                           nullptr);
}
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
    std::vector<std::byte> data(sizeof(Header) + substitute_bytes + print_bytes + 2 * sizeof(wchar_t));
    auto* header = reinterpret_cast<Header*>(data.data());
    header->tag = IO_REPARSE_TAG_MOUNT_POINT;
    header->length = static_cast<WORD>(data.size() - 8);
    header->substitute_length = static_cast<WORD>(substitute_bytes);
    header->print_offset = static_cast<WORD>(substitute_bytes + sizeof(wchar_t));
    header->print_length = static_cast<WORD>(print_bytes);
    std::memcpy(data.data() + sizeof(Header), substitute.data(), substitute_bytes);
    std::memcpy(data.data() + sizeof(Header) + header->print_offset, print.data(), print_bytes);
    const Handle handle(CreateFileW(native(path).c_str(), GENERIC_WRITE, share_all, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    DWORD returned = 0;
    return handle.valid() && DeviceIoControl(handle.value, FSCTL_SET_REPARSE_POINT, data.data(),
                                             static_cast<DWORD>(data.size()), nullptr, 0, &returned, nullptr);
}
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + u"/RC Music Storage Files-"_s +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) + u"-XXXXXX"_s};
    QByteArray user{currentSid()};
    QString package{parent.filePath(u"pacote 日本"_s)}, ac{package + u"/AC"_s};
    QString file{package + u"/settings.bin"_s};
    bool initialize(const bool with_file = true) {
        return parent.isValid() && !user.isEmpty() && createDirectory(package, user) && createDirectory(ac, user) &&
               (!with_file || createFile(file, user));
    }
    Files::RootPaths roots() const { return {package, ac}; }
    void retain() {
        parent.setAutoRemove(false);
        qInfo().noquote() << "storage-files-retained-fixture:" + parent.path();
    }
};
struct Captured final {
    Inventory::RootResult package{}, ac{};
    std::vector<Inventory::ChildResult> children{};
};
Captured capture(Files& files) {
    Captured result;
    result.package = files.root(Inventory::Root::PackageRoot);
    result.ac = files.root(Inventory::Root::AC);
    if (result.package.presence != Inventory::Presence::Present || result.ac.presence != Inventory::Presence::Present)
        return result;
    static_cast<void>(files.readMetadata(result.package.ref, Inventory::maximum_sd_bytes + 68));
    static_cast<void>(files.readMetadata(result.ac.ref, Inventory::maximum_sd_bytes + 68));
    for (std::uint32_t index = 0; index <= 10; ++index) {
        auto child = files.nextChild(result.package.ref, index, 255);
        if (child.state != Inventory::ChildState::Child) {
            if (child.state != Inventory::ChildState::End)
                qInfo() << "storage-files-capture-child-error" << static_cast<int>(child.state) << child.error << index;
            break;
        }
        static_cast<void>(files.readMetadata(child.ref, Inventory::maximum_sd_bytes + 68));
        result.children.push_back(std::move(child));
    }
    static_cast<void>(files.nextChild(result.ac.ref, 0, 255));
    return result;
}
Inventory::NodeRef named(const Captured& value, const QString& name) {
    const auto found = std::find_if(value.children.begin(), value.children.end(),
                                    [&](const auto& child) { return child.name == name; });
    return found == value.children.end() ? Inventory::NodeRef{} : found->ref;
}
} // namespace

class AppContainerStorageFilesTests final : public QObject {
    Q_OBJECT
  private slots:
    void factoryAndConstructionBindOnlyAndCreateNothing() {
        const auto calls = Files::systemCalls();
        QVERIFY(calls.security.size_query && calls.security.read && calls.close_handle);
        Fixture f;
        QVERIFY(f.parent.isValid());
        unsigned closes = 0, queries = 0;
        auto injected = calls;
        injected.close_handle = [&](Files::Handle) {
            ++closes;
            return 0U;
        };
        injected.security.size_query = [&](SecurityReader::Object, SecurityReader::Handle) {
            ++queries;
            return SecurityReader::NativeResult{5, 0};
        };
        {
            Files files(f.roots(), injected);
        }
        QCOMPARE(closes, 0U);
        QCOMPARE(queries, 0U);
        QVERIFY(!QFile::exists(f.package));
    }
    void readsRealOwnersDaclsAndOnlyTheTwoFixedFilesystemRoots() {
        Fixture f;
        QVERIFY(f.initialize());
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto tree = capture(files);
        QCOMPARE(tree.package.presence, Inventory::Presence::Present);
        QCOMPARE(tree.ac.presence, Inventory::Presence::Present);
        QVERIFY(tree.package.ref.value && tree.ac.ref.value);
        const auto package = files.readMetadata(tree.package.ref, Inventory::maximum_sd_bytes + 68);
        QCOMPARE(package.error, 0U);
        QCOMPARE(package.type, Inventory::Type::Directory);
        QCOMPARE(package.owner_sid, f.user);
        QVERIFY(!package.raw_sd.isEmpty() && IsValidSecurityDescriptor(const_cast<char*>(package.raw_sd.constData())));
        QCOMPARE(package.dacl, Inventory::Dacl::Present);
        QVERIFY(package.dacl_protected && package.sacl_not_queried && !package.reparse);
        for (const auto root :
             {Inventory::Root::MappingsSID, Inventory::Root::StorageSID, static_cast<Inventory::Root>(99)}) {
            const auto unsupported = files.root(root);
            QCOMPARE(unsupported.presence, Inventory::Presence::Unknown);
            QVERIFY(unsupported.error != 0 && unsupported.error != Inventory::not_called && !unsupported.ref.value);
        }
        QCOMPARE(files.revalidate().state, Inventory::Validation::Unchanged);
    }
    void acRootAndPackageEnumerationCorrelateNativeIdentity() {
        Fixture f;
        QVERIFY(f.initialize());
        {
            const Handle control(CreateFileW(
                native(f.package).c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL, share_all,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            const auto open_error = control.valid() ? DWORD{0} : GetLastError();
            qInfo() << "storage-files-control-list-open" << open_error;
            QVERIFY2(control.valid(), qPrintable(QString::number(open_error)));
            alignas(std::max_align_t) std::array<std::byte, 64 * 1024> buffer{};
            const auto enumerated = GetFileInformationByHandleEx(control.value, FileIdExtdDirectoryRestartInfo,
                                                                 buffer.data(), static_cast<DWORD>(buffer.size()));
            const auto enum_error = enumerated ? DWORD{0} : GetLastError();
            qInfo() << "storage-files-control-list-query" << enum_error;
            QVERIFY2(enumerated, qPrintable(QString::number(enum_error)));
        }
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto tree = capture(files);
        QCOMPARE(tree.children.size(), std::size_t{2});
        const auto child_ref = named(tree, u"AC"_s);
        QVERIFY(child_ref.value);
        const auto child = files.readMetadata(child_ref, Inventory::maximum_sd_bytes + 68);
        const auto root = files.readMetadata(tree.ac.ref, Inventory::maximum_sd_bytes + 68);
        QCOMPARE(child.error, 0U);
        QCOMPARE(root.error, 0U);
        QVERIFY(child.identity == root.identity);
        QVERIFY(child.identity.domain && child.identity.object);
        const auto file = files.readMetadata(named(tree, u"settings.bin"_s), Inventory::maximum_sd_bytes + 68);
        QCOMPARE(file.type, Inventory::Type::File);
        QCOMPARE(file.link_count, 1U);
        QCOMPARE(file.owner_sid, f.user);
        QVERIFY(file.identity != root.identity);
    }
    void metadataReadsDoNotRequireOrOpenFileData() {
        Fixture f;
        QVERIFY(f.initialize(false));
        QVERIFY(createFile(f.file, f.user, true));
        {
            const Handle data(
                CreateFileW(native(f.file).c_str(), FILE_READ_DATA, share_all, nullptr, OPEN_EXISTING, 0, nullptr));
            QVERIFY(!data.valid());
            QCOMPARE(GetLastError(), static_cast<DWORD>(ERROR_ACCESS_DENIED));
        }
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto tree = capture(files);
        const auto ref = named(tree, u"settings.bin"_s);
        QVERIFY(ref.value);
        const auto metadata = files.readMetadata(ref, Inventory::maximum_sd_bytes + 68);
        QCOMPARE(metadata.error, 0U);
        QCOMPARE(metadata.type, Inventory::Type::File);
        QCOMPARE(metadata.owner_sid, f.user);
    }
    void missingPathsStayMissingAndNeverCreateOrAdoptAReplacement() {
        Fixture f;
        QVERIFY(f.parent.isValid());
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto first = files.root(Inventory::Root::PackageRoot);
        QCOMPARE(first.presence, Inventory::Presence::Missing);
        QVERIFY(first.error == ERROR_FILE_NOT_FOUND || first.error == ERROR_PATH_NOT_FOUND);
        QVERIFY(!first.ref.value && !QFile::exists(f.package));
        QVERIFY(f.initialize());
        const auto second = files.root(Inventory::Root::PackageRoot);
        QCOMPARE(second.presence, first.presence);
        QCOMPARE(second.error, first.error);
        QVERIFY(files.revalidate().state != Inventory::Validation::Unchanged);
    }
    void malformedRootPathsRefuseWithoutOpeningAmbientNames_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void malformedRootPathsRefuseWithoutOpeningAmbientNames() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        auto roots = f.roots();
        if (kind == 0)
            roots.package_root = "relative";
        if (kind == 1)
            roots.package_root += "/../pacote 日本";
        if (kind == 2)
            roots.package_root = u"\\\\?\\"_s + roots.package_root;
        if (kind == 3)
            roots.package_root += QChar{0};
        if (kind == 4)
            roots.ac = "relative/AC";
        Files files(roots);
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto result = files.root(Inventory::Root::PackageRoot);
        QCOMPARE(result.presence, Inventory::Presence::Unknown);
        QVERIFY(result.error != 0 && result.error != Inventory::not_called);
    }
    void boundedMetadataAndChildProtocolRejectInvalidArguments_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void boundedMetadataAndChildProtocolRejectInvalidArguments() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto root = files.root(Inventory::Root::PackageRoot);
        QVERIFY(root.ref.value);
        if (kind <= 1) {
            const auto result = files.readMetadata(kind == 0 ? Inventory::NodeRef{} : root.ref, kind == 0 ? 65536 : 1);
            QVERIFY(result.error != 0 && result.error != Inventory::not_called);
            QVERIFY(result.owner_sid.isEmpty() && result.raw_sd.isEmpty());
        } else {
            const auto result = files.nextChild(root.ref, kind == 2 ? 1U : 0U,
                                                kind == 3   ? 0U
                                                : kind == 4 ? 256U
                                                            : 255U);
            QCOMPARE(result.state, Inventory::ChildState::Unknown);
            QVERIFY(result.error != 0 && result.error != Inventory::not_called);
            QVERIFY(result.name.isEmpty() && !result.ref.value);
        }
    }
    void exactNameBoundReturnsNoTruncatedNameAndFileCannotBeEnumerated() {
        Fixture f;
        QVERIFY(f.initialize(false));
        const auto long_name = QString(255, 'n');
        QVERIFY(createFile(f.package + '/' + long_name, f.user));
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto tree = capture(files);
        const auto ref = named(tree, long_name);
        QVERIFY(ref.value);
        const auto child = files.nextChild(ref, 0, 255);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QVERIFY(child.error != 0 && child.error != Inventory::not_called);
        QVERIFY(child.name.isEmpty() && !child.ref.value);
    }
    void shareAllDoesNotManufactureWriteOrDeleteDenialAndChangesAreSticky_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 4; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void shareAllDoesNotManufactureWriteOrDeleteDenialAndChangesAreSticky() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto tree = capture(files);
        QVERIFY(named(tree, u"settings.bin"_s).value);
        QCOMPARE(files.revalidate().state, Inventory::Validation::Unchanged);
        if (kind == 0) {
            const Handle writer(CreateFileW(native(f.file).c_str(), FILE_WRITE_DATA | DELETE, share_all, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
            QVERIFY2(writer.valid(), qPrintable(QString::number(GetLastError())));
            QVERIFY(SetFileAttributesW(native(f.file).c_str(), FILE_ATTRIBUTE_HIDDEN));
        }
        if (kind == 1)
            QCOMPARE(changeDacl(f.file, f.user), static_cast<DWORD>(ERROR_SUCCESS));
        if (kind == 2) {
            QVERIFY(QFile::rename(f.file, f.file + ".previous"));
            QVERIFY(createFile(f.file, f.user));
        }
        if (kind == 3)
            QVERIFY(createFile(f.package + "/new-name", f.user));
        QCOMPARE(files.revalidate().state, Inventory::Validation::Changed);
        QVERIFY(SetFileAttributesW(native(f.file).c_str(), FILE_ATTRIBUTE_NORMAL));
        QCOMPARE(files.revalidate().state, Inventory::Validation::Changed);
    }
    void movedRootBindingIsNotRecoveredThroughItsStillLiveHandle() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto control_path = f.package + u".control"_s;
        const auto moved = MoveFileExW(native(f.package).c_str(), native(control_path).c_str(), 0);
        const auto move_error = moved ? DWORD{0} : GetLastError();
        qInfo() << "storage-files-control-move-before-handles" << move_error;
        QVERIFY2(moved, qPrintable(QString::number(move_error)));
        const auto restored = MoveFileExW(native(control_path).c_str(), native(f.package).c_str(), 0);
        const auto restore_error = restored ? DWORD{0} : GetLastError();
        qInfo() << "storage-files-control-restore-before-handles" << restore_error;
        QVERIFY2(restored, qPrintable(QString::number(restore_error)));
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto root = files.root(Inventory::Root::PackageRoot);
        QVERIFY(root.ref.value);
        QCOMPARE(files.readMetadata(root.ref, Inventory::maximum_sd_bytes + 68).error, 0U);
        const auto renamed = MoveFileExW(native(f.package).c_str(), native(f.package + ".moved").c_str(), 0);
        const auto rename_error = renamed ? DWORD{0} : GetLastError();
        qInfo() << "storage-files-root-rename-after-handles" << rename_error;
        QVERIFY2(renamed, qPrintable(QString::number(rename_error)));
        QVERIFY(createDirectory(f.package, f.user));
        QVERIFY(createDirectory(f.ac, f.user));
        QCOMPARE(files.revalidate().state, Inventory::Validation::Changed);
    }
    void ancestorSwapBeforeOpenNeverReadsSecurityFromTheJunctionTarget() {
        Fixture f;
        const auto ancestor = f.parent.filePath(u"origin"_s);
        const auto moved = ancestor + u".moved"_s;
        const auto outside = f.parent.filePath(u"outside"_s);
        f.package = ancestor + u"/package"_s;
        f.ac = f.package + u"/AC"_s;
        f.file = f.package + u"/settings.bin"_s;
        QVERIFY(f.parent.isValid() && !f.user.isEmpty());
        QVERIFY(createDirectory(ancestor, f.user));
        QVERIFY(f.initialize());
        QVERIFY(createDirectory(outside, f.user));
        const auto foreign_package = outside + u"/package"_s;
        QVERIFY(createDirectory(foreign_package, f.user));
        QVERIFY(createDirectory(foreign_package + u"/AC"_s, f.user));
        FILE_ID_INFO foreign_identity{};
        {
            const Handle foreign(CreateFileW(native(foreign_package).c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            QVERIFY(foreign.valid());
            QVERIFY(
                GetFileInformationByHandleEx(foreign.value, FileIdInfo, &foreign_identity, sizeof(foreign_identity)));
        }
        bool swapped = false, mutation_ok = false;
        DWORD mutation_error = 0;
        const auto remove_link = qScopeGuard([&] {
            if (swapped)
                RemoveDirectoryW(native(ancestor).c_str());
        });
        auto calls = Files::systemCalls();
        const auto native_query = calls.security.size_query;
        unsigned foreign_security_reads = 0;
        calls.security.size_query = [&](SecurityReader::Object type, SecurityReader::Handle handle) {
            FILE_ID_INFO observed{};
            if (GetFileInformationByHandleEx(reinterpret_cast<HANDLE>(handle), FileIdInfo, &observed,
                                             sizeof(observed)) &&
                observed.VolumeSerialNumber == foreign_identity.VolumeSerialNumber &&
                std::memcmp(observed.FileId.Identifier, foreign_identity.FileId.Identifier,
                            sizeof(observed.FileId.Identifier)) == 0)
                ++foreign_security_reads;
            return native_query(type, handle);
        };
        calls.before_open = [&](const QString& path) {
            if (swapped || path != f.package)
                return;
            if (!MoveFileExW(native(ancestor).c_str(), native(moved).c_str(), 0)) {
                mutation_error = GetLastError();
                return;
            }
            swapped = true;
            mutation_ok = createDirectory(ancestor, f.user) && setJunction(ancestor, outside);
            if (!mutation_ok)
                mutation_error = GetLastError();
        };
        Files files(f.roots(), calls);
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto root = files.root(Inventory::Root::PackageRoot);
        if (root.presence == Inventory::Presence::Present)
            static_cast<void>(files.readMetadata(root.ref, Inventory::maximum_sd_bytes + 68));
        QVERIFY2(swapped && mutation_ok, qPrintable(QString::number(mutation_error)));
        QCOMPARE(foreign_security_reads, 0U);
        QVERIFY(files.revalidate().state != Inventory::Validation::Unchanged);
    }
    void hardlinksAreReportedAsMetadataForTheCoreToRefuse() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto alias = f.parent.filePath(u"alias.bin"_s);
        QVERIFY(CreateHardLinkW(native(alias).c_str(), native(f.file).c_str(), nullptr));
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto tree = capture(files);
        const auto ref = named(tree, u"settings.bin"_s);
        QVERIFY(ref.value);
        const auto metadata = files.readMetadata(ref, Inventory::maximum_sd_bytes + 68);
        QCOMPARE(metadata.error, 0U);
        QCOMPARE(metadata.link_count, 2U);
        QVERIFY(!metadata.reparse);
        QVERIFY(QFile::exists(alias) && QFile::exists(f.file));
    }
    void leafReparseIsReportedAndNeverEnumerated_data() {
        QTest::addColumn<bool>("ancestor");
        QTest::newRow("root-leaf") << false;
        QTest::newRow("root-ancestor") << true;
    }
    void leafReparseIsReportedAndNeverEnumerated() {
        QFETCH(bool, ancestor);
        Fixture f;
        QVERIFY(f.initialize());
        const auto link = f.parent.filePath(u"owned junction"_s);
        QVERIFY(createDirectory(link, f.user));
        QVERIFY(setJunction(link, f.package));
        const auto remove_link = qScopeGuard([&] { RemoveDirectoryW(native(link).c_str()); });
        auto roots = f.roots();
        roots.package_root = ancestor ? link + "/AC" : link;
        roots.ac = roots.package_root + "/AC";
        Files files(roots);
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto root = files.root(Inventory::Root::PackageRoot);
        if (ancestor) {
            QCOMPARE(root.presence, Inventory::Presence::Unknown);
            QVERIFY(root.error != 0 && root.error != Inventory::not_called);
        } else {
            QCOMPARE(root.presence, Inventory::Presence::Present);
            const auto metadata = files.readMetadata(root.ref, Inventory::maximum_sd_bytes + 68);
            QVERIFY(metadata.reparse && metadata.reparse_tag == IO_REPARSE_TAG_MOUNT_POINT);
            const auto child = files.nextChild(root.ref, 0, 255);
            QCOMPARE(child.state, Inventory::ChildState::Unknown);
        }
        QVERIFY(QFile::exists(f.file));
    }
    void caseSensitiveNamesAreDistinctOrExplicitlyUnsupported() {
        Fixture f;
        QVERIFY(f.parent.isValid() && !f.user.isEmpty());
        QVERIFY(createDirectory(f.package, f.user));
        {
            const Handle directory(CreateFileW(native(f.package).c_str(), GENERIC_READ | GENERIC_WRITE, share_all,
                                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            QVERIFY(directory.valid());
            FILE_CASE_SENSITIVE_INFO info{FILE_CS_FLAG_CASE_SENSITIVE_DIR};
            if (!SetFileInformationByHandle(directory.value, FileCaseSensitiveInfo, &info, sizeof(info))) {
                const auto error = GetLastError();
                QSKIP(qPrintable(u"Owned TEMP case mode unavailable; Win32 error="_s + QString::number(error)));
            }
        }
        QVERIFY(createDirectory(f.ac, f.user));
        QVERIFY(createFile(f.package + "/a", f.user));
        QVERIFY(createFile(f.package + "/A", f.user));
        Files files(f.roots());
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto root = files.root(Inventory::Root::PackageRoot);
        if (root.presence != Inventory::Presence::Present) {
            QCOMPARE(root.presence, Inventory::Presence::Unknown);
            QCOMPARE(root.error, static_cast<std::uint32_t>(ERROR_NOT_SUPPORTED));
            return;
        }
        const auto tree = capture(files);
        const auto lower = named(tree, "a"), upper = named(tree, "A");
        QVERIFY(lower.value && upper.value);
        const auto lower_metadata = files.readMetadata(lower, Inventory::maximum_sd_bytes + 68);
        const auto upper_metadata = files.readMetadata(upper, Inventory::maximum_sd_bytes + 68);
        QCOMPARE(lower_metadata.error, 0U);
        QCOMPARE(upper_metadata.error, 0U);
        QVERIFY(lower_metadata.identity != upper_metadata.identity);
    }
    void closeAcknowledgesOnlyExplicitSuccessfulNativeClosesAndIsOneShot() {
        Fixture f;
        QVERIFY(f.initialize());
        auto calls = Files::systemCalls();
        const auto native_close = calls.close_handle;
        unsigned closes = 0;
        calls.close_handle = [&](Files::Handle handle) {
            ++closes;
            return native_close(handle);
        };
        Files files(f.roots(), calls);
        const auto tree = capture(files);
        QVERIFY(tree.package.ref.value);
        QCOMPARE(files.closeResult().state, Inventory::CloseState::NotAttempted);
        const auto closed = files.close();
        QCOMPARE(closed.state, Inventory::CloseState::Closed);
        QCOMPARE(closed.error, 0U);
        QVERIFY(closes > 0);
        const auto count = closes;
        QCOMPARE(files.close().state, Inventory::CloseState::Closed);
        QCOMPARE(closes, count);
        QCOMPARE(files.revalidate().state, Inventory::Validation::Unknown);
        QVERIFY(files.readMetadata(tree.package.ref, 65536).error != 0);
        QVERIFY(files.root(Inventory::Root::PackageRoot).error != 0);
    }
    void closeDuringMetadataReadRefusesBusyWithoutClosingAnyHandle() {
        Fixture f;
        QVERIFY(f.initialize());
        auto calls = Files::systemCalls();
        const auto native_query = calls.security.size_query;
        const auto native_close = calls.close_handle;
        Files* active = nullptr;
        Inventory::CloseResult nested;
        unsigned closes = 0;
        calls.close_handle = [&](Files::Handle handle) {
            ++closes;
            return native_close(handle);
        };
        calls.security.size_query = [&](SecurityReader::Object type, SecurityReader::Handle handle) {
            nested = active->close();
            return native_query(type, handle);
        };
        Files files(f.roots(), calls);
        active = &files;
        const auto cleanup = qScopeGuard([&] { static_cast<void>(files.close()); });
        const auto root = files.root(Inventory::Root::PackageRoot);
        QVERIFY(root.ref.value);
        const auto metadata = files.readMetadata(root.ref, Inventory::maximum_sd_bytes + 68);
        QCOMPARE(metadata.error, 0U);
        QCOMPARE(nested.state, Inventory::CloseState::NotAttempted);
        QCOMPARE(nested.error, static_cast<std::uint32_t>(ERROR_BUSY));
        QCOMPARE(closes, 0U);
    }
    void uncertainCloseAndAbandonmentRetainHandlesWithoutDestructorRetry_data() {
        QTest::addColumn<bool>("abandon");
        QTest::newRow("close-error") << false;
        QTest::newRow("abandoned") << true;
    }
    void uncertainCloseAndAbandonmentRetainHandlesWithoutDestructorRetry() {
        QFETCH(bool, abandon);
        Fixture f;
        QVERIFY(f.initialize());
        f.retain();
        auto calls = Files::systemCalls();
        const auto native_query = calls.security.size_query;
        struct Retained final {
            unsigned closes{};
            Files::Handle witness{};
        };
        const auto retained = std::make_shared<Retained>();
        calls.security.size_query = [retained, native_query](SecurityReader::Object type,
                                                             SecurityReader::Handle handle) {
            retained->witness = handle;
            return native_query(type, handle);
        };
        calls.close_handle = [retained](Files::Handle handle) {
            ++retained->closes;
            retained->witness = handle;
            return static_cast<std::uint32_t>(ERROR_ACCESS_DENIED);
        };
        {
            Files files(f.roots(), calls);
            const auto root = files.root(Inventory::Root::PackageRoot);
            QVERIFY(root.ref.value);
            QCOMPARE(files.readMetadata(root.ref, Inventory::maximum_sd_bytes + 68).error, 0U);
            if (!abandon) {
                const auto closed = files.close();
                QCOMPARE(closed.state, Inventory::CloseState::Unknown);
                QCOMPARE(closed.error, static_cast<std::uint32_t>(ERROR_ACCESS_DENIED));
                QCOMPARE(files.close().state, Inventory::CloseState::Unknown);
                QCOMPARE(retained->closes, 1U);
            }
        }
        QCOMPARE(retained->closes, abandon ? 0U : 1U);
        QVERIFY(retained->witness != 0);
        FILE_ID_INFO identity{};
        QVERIFY(GetFileInformationByHandleEx(reinterpret_cast<HANDLE>(retained->witness), FileIdInfo, &identity,
                                             sizeof(identity)));
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageFilesTests)
#include "AppContainerStorageFilesTests.moc"
