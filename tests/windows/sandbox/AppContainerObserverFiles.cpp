#include "AppContainerObserverFiles.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QScopeGuard>

#include <aclapi.h>
#include <sddl.h>

#include <cstddef>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace music_player::sandbox_probe {
namespace {
using Files = AppContainerObserverFiles;
using music_player::m0::download_artifacts::Handle;
namespace artifacts = music_player::m0::download_artifacts;

QString leaf(const Files::Slot slot) {
    switch (slot) {
    case Files::Slot::Request:
        return QStringLiteral("wfp-request.json");
    case Files::Slot::Ready:
        return QStringLiteral("wfp-ready.json");
    case Files::Slot::Result:
        return QStringLiteral("wfp-result.json");
    }
    return {};
}
bool validSid(const QByteArray& bytes) {
    if (bytes.size() < 8 || bytes.size() > SECURITY_MAX_SID_SIZE)
        return false;
    const auto* raw = reinterpret_cast<const unsigned char*>(bytes.constData());
    return raw[0] == SID_REVISION && raw[1] <= SID_MAX_SUB_AUTHORITIES && bytes.size() == 8 + 4 * qsizetype{raw[1]} &&
           IsValidSid(const_cast<char*>(bytes.constData()));
}
bool wellKnown(const QByteArray& bytes, const WELL_KNOWN_SID_TYPE type) {
    return validSid(bytes) && IsWellKnownSid(const_cast<char*>(bytes.constData()), type);
}
bool validDriverSid(const QByteArray& bytes) {
    return validSid(bytes) && !wellKnown(bytes, WinLocalSystemSid) && !wellKnown(bytes, WinBuiltinAdministratorsSid);
}
QByteArray copiedSid(PSID sid) {
    if (!sid || !IsValidSid(sid))
        return {};
    const auto length = GetLengthSid(sid);
    return length >= 8 && length <= SECURITY_MAX_SID_SIZE
               ? QByteArray{static_cast<const char*>(sid), static_cast<qsizetype>(length)}
               : QByteArray{};
}
bool exactSecurity(const HANDLE handle, const bool directory, const QByteArray& driver,
                   QByteArray* found_owner = nullptr) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    const auto owner_bytes = copiedSid(owner);
    const auto expected_driver = directory && driver.isEmpty() ? owner_bytes : driver;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!validDriverSid(expected_driver) || !validSid(owner_bytes) || !dacl || !IsValidAcl(dacl) ||
        dacl->AceCount != 3 || !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        !(control & SE_DACL_PROTECTED) ||
        (directory ? owner_bytes != expected_driver
                   : owner_bytes != expected_driver && !wellKnown(owner_bytes, WinLocalSystemSid) &&
                         !wellKnown(owner_bytes, WinBuiltinAdministratorsSid)))
        return false;
    bool found_driver = false, found_system = false, found_administrators = false;
    constexpr auto sid_offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
    const auto expected_flags = static_cast<BYTE>(directory ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0);
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw) || !raw)
            return false;
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != expected_flags ||
            ace->Mask != FILE_ALL_ACCESS || ace->Header.AceSize < sid_offset + 8)
            return false;
        const auto* sid = reinterpret_cast<const unsigned char*>(&ace->SidStart);
        if (sid[0] != SID_REVISION || sid[1] > SID_MAX_SUB_AUTHORITIES ||
            ace->Header.AceSize != sid_offset + 8 + 4 * std::size_t{sid[1]})
            return false;
        const QByteArray trustee(reinterpret_cast<const char*>(sid),
                                 static_cast<qsizetype>(ace->Header.AceSize - sid_offset));
        if (!validSid(trustee))
            return false;
        if (trustee == expected_driver) {
            if (std::exchange(found_driver, true))
                return false;
        } else if (wellKnown(trustee, WinLocalSystemSid)) {
            if (std::exchange(found_system, true))
                return false;
        } else if (wellKnown(trustee, WinBuiltinAdministratorsSid)) {
            if (std::exchange(found_administrators, true))
                return false;
        } else
            return false;
    }
    if (!found_driver || !found_system || !found_administrators)
        return false;
    if (found_owner)
        *found_owner = owner_bytes;
    return true;
}
bool tokenInformation(const HANDLE token, const TOKEN_INFORMATION_CLASS kind, std::vector<unsigned char>& bytes) {
    DWORD needed = 0;
    static_cast<void>(GetTokenInformation(token, kind, nullptr, 0, &needed));
    if (!needed || needed > 64 * 1024)
        return false;
    bytes.resize(needed);
    if (!GetTokenInformation(token, kind, bytes.data(), needed, &needed) || needed > bytes.size())
        return false;
    bytes.resize(needed);
    return true;
}
QByteArray tokenSid(PSID sid, const std::vector<unsigned char>& bytes) {
    const auto begin = reinterpret_cast<std::uintptr_t>(bytes.data());
    const auto address = reinterpret_cast<std::uintptr_t>(sid);
    if (address < begin || address - begin > bytes.size() || bytes.size() - (address - begin) < 8)
        return {};
    const auto* raw = static_cast<const unsigned char*>(sid);
    if (raw[0] != SID_REVISION || raw[1] > SID_MAX_SUB_AUTHORITIES)
        return {};
    const auto length = 8 + 4 * std::size_t{raw[1]};
    if (length > bytes.size() - (address - begin))
        return {};
    const QByteArray result(reinterpret_cast<const char*>(raw), static_cast<qsizetype>(length));
    return validSid(result) ? result : QByteArray{};
}
QByteArray eligibleOwner(const QByteArray& driver) {
    // Do not apply the process token's authority while a caller impersonates.
    HANDLE raw = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw)) {
        const Handle thread_token(raw);
        return {};
    }
    if (GetLastError() != ERROR_NO_TOKEN || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return {};
    const Handle token(raw);
    std::vector<unsigned char> user;
    if (!tokenInformation(token.get(), TokenUser, user) || user.size() < sizeof(TOKEN_USER))
        return {};
    const auto user_sid = tokenSid(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid, user);
    if (user_sid == driver || wellKnown(user_sid, WinLocalSystemSid))
        return user_sid;
    std::vector<unsigned char> groups;
    if (!tokenInformation(token.get(), TokenGroups, groups) || groups.size() < offsetof(TOKEN_GROUPS, Groups))
        return {};
    const auto* membership = reinterpret_cast<const TOKEN_GROUPS*>(groups.data());
    if (membership->GroupCount > (groups.size() - offsetof(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES))
        return {};
    for (DWORD index = 0; index < membership->GroupCount; ++index) {
        const auto& group = membership->Groups[index];
        const auto sid = tokenSid(group.Sid, groups);
        constexpr DWORD required = SE_GROUP_ENABLED | SE_GROUP_OWNER;
        if (wellKnown(sid, WinBuiltinAdministratorsSid) && (group.Attributes & required) == required &&
            !(group.Attributes & SE_GROUP_USE_FOR_DENY_ONLY))
            return sid;
    }
    return {};
}
QString sidText(const QByteArray& sid) {
    LPWSTR text = nullptr;
    if (!validSid(sid) || !ConvertSidToStringSidW(const_cast<char*>(sid.constData()), &text))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}
struct FileSecurity final {
    PSECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    FileSecurity(const QByteArray& driver, const QByteArray& owner) {
        const auto owner_text = sidText(owner);
        const auto driver_text = sidText(driver);
        if (owner_text.isEmpty() || driver_text.isEmpty())
            return;
        const auto sddl = QStringLiteral("O:") + owner_text + QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;") +
                          driver_text + ')';
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1,
                                                                 &descriptor, nullptr))
            attributes.lpSecurityDescriptor = descriptor;
    }
    ~FileSecurity() { LocalFree(descriptor); }
};
struct WriteHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~WriteHandle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    bool close() { return CloseHandle(std::exchange(value, INVALID_HANDLE_VALUE)) != FALSE; }
};
bool fileInformation(const HANDLE handle, FILE_ID_INFO& identity, quint64& size) {
    BY_HANDLE_FILE_INFORMATION basic{};
    LARGE_INTEGER bytes{};
    if (!artifacts::information(handle, false, identity) || !GetFileInformationByHandle(handle, &basic) ||
        basic.nNumberOfLinks != 1 || !GetFileSizeEx(handle, &bytes) || bytes.QuadPart < 0)
        return false;
    size = static_cast<quint64>(bytes.QuadPart);
    return true;
}
bool samePath(const QString& left, const QString& right) {
    return !left.isEmpty() && !right.isEmpty() && left.compare(right, Qt::CaseInsensitive) == 0;
}
} // namespace

struct AppContainerObserverFiles::Impl final {
    Config config;
    QByteArray driver_sid{};
    QString directory{}, canonical{};
    std::vector<Handle> ancestors{};
    Handle pin{};
    FILE_ID_INFO identity{};
    mutable std::mutex mutex;
    explicit Impl(Config value) : config(std::move(value)) {}
    bool unchanged() const {
        FILE_ID_INFO current{};
        return pin.valid() && artifacts::information(pin.get(), true, current) &&
               artifacts::sameIdentity(identity, current) && samePath(artifacts::finalPath(pin.get()), canonical) &&
               exactSecurity(pin.get(), true, driver_sid);
    }
    QString child(const QString& name) const { return directory + '/' + name; }
    bool fileMatches(const HANDLE file, const QString& name, const FILE_ID_INFO& id) const {
        return id.VolumeSerialNumber == identity.VolumeSerialNumber &&
               samePath(artifacts::finalPath(file), canonical + '\\' + name) && exactSecurity(file, false, driver_sid);
    }
};
struct AppContainerObserverFiles::ReadLease::Impl final {
    std::shared_ptr<void> directory;
    Handle file;
};
AppContainerObserverFiles::ReadLease::ReadLease(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AppContainerObserverFiles::ReadLease::~ReadLease() = default;
AppContainerObserverFiles::AppContainerObserverFiles(Config config)
    : impl_(std::make_shared<Impl>(std::move(config))) {}
AppContainerObserverFiles::~AppContainerObserverFiles() = default;
AppContainerObserverFiles::OpenResult AppContainerObserverFiles::open() {
    const std::lock_guard lock(impl_->mutex);
    auto& value = *impl_;
    if (value.pin.valid())
        return value.unchanged() ? OpenResult{OpenStatus::Opened, ERROR_SUCCESS}
                                 : OpenResult{OpenStatus::Unsafe, ERROR_INVALID_DATA};
    QString directory;
    if ((!value.config.driver_sid.isEmpty() && !validDriverSid(value.config.driver_sid)) ||
        !artifacts::validPath(value.config.directory, directory))
        return {OpenStatus::Unsafe, ERROR_INVALID_PARAMETER};
    std::vector<Handle> ancestors;
    SetLastError(ERROR_SUCCESS);
    if (!artifacts::pinAncestors(directory, ancestors)) {
        const auto error = GetLastError();
        return {error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? OpenStatus::IoError
                                                                               : OpenStatus::Unsafe,
                error ? error : ERROR_INVALID_DATA};
    }
    Handle pin(CreateFileW(artifacts::nativePath(directory).c_str(),
                           FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | READ_CONTROL, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!pin.valid())
        return {OpenStatus::IoError, GetLastError()};
    FILE_ID_INFO identity{};
    QByteArray owner;
    if (!artifacts::information(pin.get(), true, identity) ||
        !exactSecurity(pin.get(), true, value.config.driver_sid, &owner))
        return {OpenStatus::Unsafe, ERROR_INVALID_DATA};
    const auto canonical = artifacts::finalPath(pin.get());
    if (canonical.isEmpty())
        return {OpenStatus::IoError, ERROR_INVALID_DATA};
    value.directory = std::move(directory);
    value.canonical = canonical;
    value.identity = identity;
    value.ancestors = std::move(ancestors);
    value.pin = std::move(pin);
    value.driver_sid = std::move(owner);
    return {OpenStatus::Opened, ERROR_SUCCESS};
}
const QByteArray& AppContainerObserverFiles::driverSid() const noexcept { return impl_->driver_sid; }
AppContainerObserverFiles::ReadResult AppContainerObserverFiles::read(const Slot slot) const {
    const std::lock_guard lock(impl_->mutex);
    const auto& value = *impl_;
    const auto name = leaf(slot);
    if (name.isEmpty() || !value.unchanged())
        return {ReadStatus::Unsafe, {}, {}, ERROR_INVALID_DATA};
    Handle file(CreateFileW(
        artifacts::nativePath(value.child(name)).c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.valid()) {
        const auto error = GetLastError();
        const auto status = error == ERROR_FILE_NOT_FOUND                                       ? ReadStatus::Missing
                            : error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ? ReadStatus::Busy
                                                                                                : ReadStatus::IoError;
        return {status, {}, {}, error};
    }
    FILE_ID_INFO identity{};
    quint64 size = 0;
    if (!fileInformation(file.get(), identity, size) || !value.fileMatches(file.get(), name, identity))
        return {ReadStatus::Unsafe, {}, {}, ERROR_INVALID_DATA};
    if (size > static_cast<quint64>(maximum_file_size))
        return {ReadStatus::TooLarge, {}, {}, ERROR_FILE_TOO_LARGE};
    QByteArray bytes(static_cast<qsizetype>(size), '\0');
    DWORD offset = 0;
    while (offset < size) {
        DWORD received = 0;
        if (!ReadFile(file.get(), bytes.data() + offset, static_cast<DWORD>(size - offset), &received, nullptr))
            return {ReadStatus::IoError, {}, {}, GetLastError()};
        if (!received)
            return {ReadStatus::IoError, {}, {}, ERROR_HANDLE_EOF};
        offset += received;
    }
    FILE_ID_INFO after{};
    quint64 after_size = 0;
    if (!fileInformation(file.get(), after, after_size) || !artifacts::sameIdentity(identity, after) ||
        after_size != size || !value.fileMatches(file.get(), name, after) || !value.unchanged())
        return {ReadStatus::Unsafe, {}, {}, ERROR_INVALID_DATA};
    auto held = std::make_unique<ReadLease::Impl>();
    held->directory = impl_;
    held->file = std::move(file);
    auto lease = std::shared_ptr<const ReadLease>(new ReadLease(std::move(held)));
    return {ReadStatus::Ready, std::move(bytes), std::move(lease), ERROR_SUCCESS};
}
AppContainerObserverFiles::WriteResult AppContainerObserverFiles::writeNew(const Slot slot,
                                                                           const QByteArrayView bytes) const {
    const std::lock_guard lock(impl_->mutex);
    const auto& value = *impl_;
    const auto name = leaf(slot);
    if (name.isEmpty() || !value.unchanged())
        return {WriteStatus::Unsafe, ERROR_INVALID_DATA};
    if (bytes.size() > maximum_file_size)
        return {WriteStatus::TooLarge, ERROR_FILE_TOO_LARGE};
    const auto owner = eligibleOwner(value.driver_sid);
    if (owner.isEmpty())
        return {WriteStatus::Unsafe, ERROR_INVALID_OWNER};
    FileSecurity security(value.driver_sid, owner);
    if (!security.descriptor)
        return {WriteStatus::IoError, ERROR_INVALID_SECURITY_DESCR};
    WriteHandle file{
        CreateFileW(artifacts::nativePath(value.child(name)).c_str(),
                    GENERIC_WRITE | READ_CONTROL | FILE_READ_ATTRIBUTES, 0, &security.attributes, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return {error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? WriteStatus::Exists
                                                                            : WriteStatus::IoError,
                error};
    }
    FILE_ID_INFO identity{};
    quint64 size = 0;
    QByteArray actual_owner;
    if (!fileInformation(file.value, identity, size) || size != 0 || !value.fileMatches(file.value, name, identity) ||
        !exactSecurity(file.value, false, value.driver_sid, &actual_owner) || actual_owner != owner)
        return {WriteStatus::Unsafe, ERROR_INVALID_DATA};
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(file.value, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr))
            return {WriteStatus::IoError, GetLastError()};
        if (!written)
            return {WriteStatus::IoError, ERROR_WRITE_FAULT};
        offset += written;
    }
    if (!FlushFileBuffers(file.value))
        return {WriteStatus::IoError, GetLastError()};
    FILE_ID_INFO after{};
    quint64 after_size = 0;
    if (!fileInformation(file.value, after, after_size) || !artifacts::sameIdentity(identity, after) ||
        after_size != static_cast<quint64>(bytes.size()) || !value.fileMatches(file.value, name, after) ||
        !value.unchanged())
        return {WriteStatus::Unsafe, ERROR_INVALID_DATA};
    if (!file.close())
        return {WriteStatus::IoError, GetLastError()};
    return {WriteStatus::Written, ERROR_SUCCESS};
}
} // namespace music_player::sandbox_probe
