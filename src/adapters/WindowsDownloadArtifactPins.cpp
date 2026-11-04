#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QCryptographicHash>
#include <QDir>

#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

namespace music_player::m0::download_artifacts {
namespace {
class ContainerSecurity final {
  public:
    explicit ContainerSecurity(const QByteArray& container_sid) : container_sid_(container_sid) {
        // Bound every SID read before passing the bytes to Win32. This is the
        // ordinary package SID shape only, not All Application Packages/LPAC.
        if (container_sid_.size() != 40)
            return;
        const auto* bytes = reinterpret_cast<const BYTE*>(container_sid_.constData());
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_APP_PACKAGE_AUTHORITY;
        if (bytes[0] != SID_REVISION || bytes[1] != 8 || !IsValidSid(containerSid()) ||
            std::memcmp(GetSidIdentifierAuthority(containerSid()), &authority, sizeof(authority)) != 0 ||
            *GetSidSubAuthority(containerSid(), 0) != SECURITY_APP_PACKAGE_BASE_RID)
            return;
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return;
        Handle token(raw);
        DWORD size = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
        if (size == 0 || size > 64 * 1024)
            return;
        user_.resize(size);
        if (!GetTokenInformation(token.get(), TokenUser, user_.data(), size, &size) || !IsValidSid(userSid()))
            return;
        DWORD system_size = static_cast<DWORD>(system_.size());
        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_.data(), &system_size) ||
            EqualSid(userSid(), system_.data()))
            return;
        LPWSTR user_text = nullptr, container_text = nullptr;
        if (!ConvertSidToStringSidW(userSid(), &user_text))
            return;
        const std::unique_ptr<wchar_t, decltype(&LocalFree)> user_owner(user_text, &LocalFree);
        if (!ConvertSidToStringSidW(containerSid(), &container_text))
            return;
        const std::unique_ptr<wchar_t, decltype(&LocalFree)> container_owner(container_text, &LocalFree);
        const auto user_string = QString::fromWCharArray(user_text);
        const auto text = QStringLiteral("O:") + user_string + QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") +
                          user_string + QStringLiteral(")(A;;0x1200a9;;;") + QString::fromWCharArray(container_text) +
                          ')';
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1,
                                                                 &descriptor, nullptr))
            descriptor_.reset(descriptor);
    }

    bool valid() const noexcept { return descriptor_ != nullptr; }
    SECURITY_ATTRIBUTES attributes() const { return {sizeof(SECURITY_ATTRIBUTES), descriptor_.get(), FALSE}; }
    bool matches(HANDLE handle) const {
        if (!valid())
            return false;
        PSECURITY_DESCRIPTOR raw = nullptr;
        PSID owner = nullptr;
        PACL dacl = nullptr;
        if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                            nullptr, &dacl, nullptr, &raw) != ERROR_SUCCESS)
            return false;
        const std::unique_ptr<void, decltype(&LocalFree)> security(raw, &LocalFree);
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision = 0;
        if (!raw || !owner || !IsValidSid(owner) || !EqualSid(owner, userSid()) || !dacl || !IsValidAcl(dacl) ||
            dacl->AceCount != 3 || !GetSecurityDescriptorControl(raw, &control, &revision) ||
            !(control & SE_DACL_PROTECTED))
            return false;
        bool found_user = false, found_system = false, found_container = false;
        for (DWORD index = 0; index < dacl->AceCount; ++index) {
            void* raw_ace = nullptr;
            if (!GetAce(dacl, index, &raw_ace))
                return false;
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
            constexpr auto sid_offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
            if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 ||
                ace->Header.AceSize < sid_offset + 8)
                return false;
            auto* sid = const_cast<DWORD*>(&ace->SidStart);
            const auto count = reinterpret_cast<const BYTE*>(sid)[1];
            if (count > SID_MAX_SUB_AUTHORITIES || ace->Header.AceSize != sid_offset + GetSidLengthRequired(count) ||
                !IsValidSid(sid))
                return false;
            if (EqualSid(sid, userSid()) && !found_user) {
                if (ace->Mask != FILE_ALL_ACCESS)
                    return false;
                found_user = true;
            } else if (EqualSid(sid, const_cast<std::byte*>(system_.data())) && !found_system) {
                if (ace->Mask != FILE_ALL_ACCESS)
                    return false;
                found_system = true;
            } else if (EqualSid(sid, containerSid()) && !found_container) {
                if (ace->Mask != (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE))
                    return false;
                found_container = true;
            } else
                return false;
        }
        return found_user && found_system && found_container;
    }

  private:
    PSID containerSid() const { return const_cast<char*>(container_sid_.constData()); }
    PSID userSid() const { return reinterpret_cast<const TOKEN_USER*>(user_.data())->User.Sid; }
    QByteArray container_sid_;
    std::vector<std::byte> user_;
    std::array<std::byte, SECURITY_MAX_SID_SIZE> system_{};
    std::unique_ptr<void, decltype(&LocalFree)> descriptor_{nullptr, &LocalFree};
};
} // namespace

bool createPrivateDirectory(const QString& path) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (size == 0 || size > 64 * 1024)
        return false;
    std::vector<std::byte> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(bytes.data());
    LPWSTR sid = nullptr;
    if (!IsValidSid(user->User.Sid) || !ConvertSidToStringSidW(user->User.Sid, &sid))
        return false;
    const std::unique_ptr<wchar_t, decltype(&LocalFree)> sid_owner(sid, &LocalFree);
    const auto text = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;") + QString::fromWCharArray(sid) + ')';
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const std::unique_ptr<void, decltype(&LocalFree)> descriptor_owner(descriptor, &LocalFree);
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(nativePath(path).c_str(), &attributes) != FALSE;
}

namespace {
bool copyPinnedWithSecurity(const Artifact& source, Artifact& destination, const ContainerSecurity* security) {
    SECURITY_ATTRIBUTES attributes{};
    if (security)
        attributes = security->attributes();
    Handle output(CreateFileW(nativePath(destination.path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                              security ? &attributes : nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!output.valid() || !information(output.get(), false, destination.identity) ||
        (security && !security->matches(output.get())))
        return false;
    destination.final_path = finalPath(output.get());
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(source.handle.get(), zero, nullptr, FILE_BEGIN))
        return false;
    std::array<char, 64 * 1024> block{};
    quint64 copied = 0;
    while (copied < source.bytes) {
        const auto wanted = static_cast<DWORD>(std::min<quint64>(block.size(), source.bytes - copied));
        DWORD read = 0, written = 0;
        if (!ReadFile(source.handle.get(), block.data(), wanted, &read, nullptr) || read == 0 || read > wanted ||
            !WriteFile(output.get(), block.data(), read, &written, nullptr) || written != read)
            return false;
        copied += read;
    }
    return FlushFileBuffers(output.get()) && unchanged(source);
}
} // namespace

bool copyPinned(const Artifact& source, Artifact& destination) {
    return copyPinnedWithSecurity(source, destination, nullptr);
}
bool createPrivateDirectory(const QString& path, const QByteArray& app_container_sid) {
    const ContainerSecurity security(app_container_sid);
    if (!security.valid())
        return false;
    auto attributes = security.attributes();
    return CreateDirectoryW(nativePath(path).c_str(), &attributes) != FALSE;
}
bool copyPinned(const Artifact& source, Artifact& destination, const QByteArray& app_container_sid) {
    const ContainerSecurity security(app_container_sid);
    return security.valid() && copyPinnedWithSecurity(source, destination, &security);
}
bool runtimeDaclMatches(HANDLE handle, const QByteArray& app_container_sid) {
    return ContainerSecurity(app_container_sid).matches(handle);
}

bool removeOwned(const QString& path, const bool directory, const FILE_ID_INFO& expected, const QString& final_path) {
    Handle handle(CreateFileW(nativePath(path).c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    FILE_ID_INFO actual{};
    if (!handle.valid() || !information(handle.get(), directory, actual) || !sameIdentity(expected, actual) ||
        finalPath(handle.get()) != final_path)
        return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(handle.get(), FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
}

std::wstring nativePath(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}

bool validPath(const QString& original, QString& normalized) {
    // Qt strips Windows extended-path prefixes during separator normalization.
    // Reject namespaces/UNC/root-relative inputs BEFORE any such rewrite.
    if (original.size() < 4 || original.at(1) != QLatin1Char(':') ||
        (original.at(2) != QLatin1Char('/') && original.at(2) != QLatin1Char('\\')) ||
        !((original.at(0) >= QLatin1Char('A') && original.at(0) <= QLatin1Char('Z')) ||
          (original.at(0) >= QLatin1Char('a') && original.at(0) <= QLatin1Char('z'))))
        return false;
    normalized = QDir::fromNativeSeparators(original);
    if (normalized.size() < 4 || normalized.size() > 30000 || normalized.at(1) != QLatin1Char(':') ||
        normalized.at(2) != QLatin1Char('/') ||
        !((normalized.at(0) >= QLatin1Char('A') && normalized.at(0) <= QLatin1Char('Z')) ||
          (normalized.at(0) >= QLatin1Char('a') && normalized.at(0) <= QLatin1Char('z'))) ||
        QString::fromUtf8(normalized.toUtf8()) != normalized)
        return false;
    const auto components = normalized.mid(3).split(QLatin1Char('/'));
    if (components.size() > 256)
        return false;
    for (const auto& component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") ||
            component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
            return false;
        for (const auto character : component) {
            if (character.category() == QChar::Other_Control || QStringLiteral(":<>\"|?*").contains(character))
                return false;
        }
    }
    const auto drive = QDir::toNativeSeparators(normalized.left(3)).toStdWString();
    return GetDriveTypeW(drive.c_str()) == DRIVE_FIXED;
}

bool validPin(const QByteArray& pin) {
    return pin.size() == 64 && std::all_of(pin.cbegin(), pin.cend(), [](const char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

QString finalPath(HANDLE handle) {
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (required == 0 || required > 32768)
        return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
    const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0 || written >= buffer.size())
        return {};
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
}

bool information(HANDLE handle, bool directory, FILE_ID_INFO& identity, quint64* size) {
    BY_HANDLE_FILE_INFORMATION basic{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &basic) ||
        (basic.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((basic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        !GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)))
        return false;
    if (size) {
        LARGE_INTEGER bytes{};
        if (!GetFileSizeEx(handle, &bytes) || bytes.QuadPart <= 0 ||
            static_cast<quint64>(bytes.QuadPart) > max_artifact_bytes)
            return false;
        *size = static_cast<quint64>(bytes.QuadPart);
    }
    return true;
}

bool sameIdentity(const FILE_ID_INFO& left, const FILE_ID_INFO& right) noexcept {
    return left.VolumeSerialNumber == right.VolumeSerialNumber &&
           std::memcmp(left.FileId.Identifier, right.FileId.Identifier, sizeof(left.FileId.Identifier)) == 0;
}

bool pinAncestors(const QString& path, std::vector<Handle>& pins) {
    QString prefix = path.left(3);
    const auto components = path.mid(3).split(QLatin1Char('/'));
    for (qsizetype index = 0; index < components.size(); ++index) {
        // index 0 pins the drive; subsequent iterations pin ancestors only.
        if (index != 0) {
            if (!prefix.endsWith(QLatin1Char('/')))
                prefix += QLatin1Char('/');
            prefix += components.at(index - 1);
        }
        Handle handle(CreateFileW(nativePath(prefix).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FILE_ID_INFO identity{};
        if (!handle.valid() || !information(handle.get(), true, identity))
            return false;
        pins.push_back(std::move(handle));
    }
    return true;
}

bool unchanged(const Artifact& artifact) {
    FILE_ID_INFO identity{};
    quint64 bytes = 0;
    return artifact.handle.valid() && information(artifact.handle.get(), false, identity, &bytes) &&
           sameIdentity(identity, artifact.identity) && bytes == artifact.bytes &&
           finalPath(artifact.handle.get()) == artifact.final_path;
}

Error pinAndHash(Artifact& artifact, std::vector<Handle>& ancestors) {
    if (!pinAncestors(artifact.path, ancestors))
        return Error::FileAccess;
    artifact.handle =
        Handle(CreateFileW(nativePath(artifact.path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!artifact.handle.valid() || !information(artifact.handle.get(), false, artifact.identity, &artifact.bytes))
        return Error::FileAccess;
    artifact.final_path = finalPath(artifact.handle.get());
    if (artifact.final_path.isEmpty())
        return Error::FileAccess;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> block{};
    quint64 consumed = 0;
    while (consumed < artifact.bytes) {
        DWORD count = 0;
        const auto wanted = static_cast<DWORD>(std::min<quint64>(block.size(), artifact.bytes - consumed));
        if (!ReadFile(artifact.handle.get(), block.data(), wanted, &count, nullptr) || count == 0 || count > wanted)
            return Error::FileAccess;
        hash.addData(QByteArrayView{block.data(), static_cast<qsizetype>(count)});
        consumed += count;
    }
    if (!unchanged(artifact))
        return Error::FileAccess;
    return hash.result().toHex() == artifact.sha256 ? Error::None : Error::HashMismatch;
}

bool moduleMatches(const void* address, const Artifact& expected) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module))
        return false;
    // UNCHANGED_REFCOUNT: never FreeLibrary this handle. Core/Network are linked
    // dependencies and the plugin's QPluginLoader retains its own lifetime.
    std::array<wchar_t, 32768> buffer{};
    const DWORD written = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size())
        return false;
    auto path = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
    if (path.startsWith(QStringLiteral("\\\\?\\")))
        path.remove(0, 4);
    QString normalized;
    if (!validPath(path, normalized))
        return false;
    Handle actual(CreateFileW(nativePath(normalized).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    FILE_ID_INFO identity{};
    quint64 bytes = 0;
    return actual.valid() && information(actual.get(), false, identity, &bytes) &&
           sameIdentity(identity, expected.identity) && bytes == expected.bytes &&
           finalPath(actual.get()) == expected.final_path && unchanged(expected);
}

} // namespace music_player::m0::download_artifacts
