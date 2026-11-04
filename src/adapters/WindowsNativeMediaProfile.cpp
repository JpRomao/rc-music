#include "adapters/WindowsNativeMediaProfile.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QDir>
#include <QScopeGuard>

#include <Sddl.h>
#include <ShlObj.h>
#include <userenv.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
using Native = WindowsNativeMediaProfile;
using Absence = WindowsMediaProfileAbsence;
using download_artifacts::Handle;
using download_artifacts::nativePath;

bool validName(const QString& name) {
    const auto prefix = QStringLiteral("RCMusic.Media.");
    return name.isValidUtf16() && name.size() == prefix.size() + 36 && name.startsWith(prefix) &&
           QString::fromStdString(downloadMediaProfileName(name.mid(prefix.size()).toStdString())) == name;
}
bool canonicalSid(const QByteArray& sid) { return WindowsDownloadAppContainer::fromTrustedSid(sid) != nullptr; }
qint32 code(const HRESULT value) { return static_cast<qint32>(value); }
bool userSid(const QByteArray& sid) {
    if (sid.size() < 8 || sid.size() > SECURITY_MAX_SID_SIZE || sid[0] != SID_REVISION)
        return false;
    const auto count = static_cast<unsigned char>(sid[1]);
    return count <= SID_MAX_SUB_AUTHORITIES && sid.size() == 8 + 4 * count;
}
QString profileSidText(const QByteArray& sid) {
    if (!canonicalSid(sid))
        return {};
    QString text = QStringLiteral("S-1-15");
    for (qsizetype index = 0; index < 8; ++index) {
        quint32 value = 0;
        for (qsizetype byte = 0; byte < 4; ++byte)
            value |= quint32{static_cast<unsigned char>(sid[8 + 4 * index + byte])} << (8 * byte);
        text += QLatin1Char('-') + QString::number(value);
    }
    return text;
}
bool storagePath(const QString& original, QString& normalized) {
    if (original.size() < 4 || original.size() > 32767 || !original.isValidUtf16() || original[1] != QLatin1Char(':') ||
        (original[2] != QLatin1Char('/') && original[2] != QLatin1Char('\\')) ||
        !((original[0] >= QLatin1Char('A') && original[0] <= QLatin1Char('Z')) ||
          (original[0] >= QLatin1Char('a') && original[0] <= QLatin1Char('z'))))
        return false;
    normalized = QDir::fromNativeSeparators(original);
    const auto components = normalized.mid(3).split(QLatin1Char('/'));
    if (components.size() > 256)
        return false;
    for (const auto& component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") ||
            component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
            return false;
        for (const auto unit : component)
            if (unit.category() == QChar::Other_Control || QStringLiteral(":<>\"|?*").contains(unit))
                return false;
    }
    return true;
}
bool validLocation(const Native::StorageLocation& location, const QString& name, const QByteArray& sid) {
    if (location.name != name || !validName(location.name) || location.sid != sid || !canonicalSid(location.sid) ||
        !userSid(location.creator_user_sid))
        return false;
    QString package, ac;
    if (!storagePath(location.package_root, package) || !storagePath(location.ac_path, ac) ||
        !package.endsWith(QStringLiteral("/Packages/") + name, Qt::CaseInsensitive) ||
        ac.compare(package + QStringLiteral("/AC"), Qt::CaseInsensitive) != 0)
        return false;
    const auto base = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
    const auto text = profileSidText(sid);
    return !text.isEmpty() && location.mappings_key == base + QStringLiteral("Mappings\\") + text &&
           location.storage_key == base + QStringLiteral("Storage\\") + text;
}
Native::StorageLocation ownedLocation(const Native::StorageLocation& value) {
    const auto string = [](const QString& text) { return QString(text.constData(), text.size()); };
    const auto bytes = [](const QByteArray& data) { return QByteArray(data.constData(), data.size()); };
    return {string(value.name),         bytes(value.sid),      bytes(value.creator_user_sid),
            string(value.package_root), string(value.ac_path), string(value.mappings_key),
            string(value.storage_key)};
}

bool missing(const DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }

bool currentDesktopUser() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || !session)
        return false;
    HANDLE raw = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw)) {
        const Handle token(raw);
        return false;
    }
    return GetLastError() == ERROR_NO_TOKEN;
}
QByteArray currentDesktopUserSid() {
    if (!currentDesktopUser())
        return {};
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return {};
    const Handle token(raw);
    std::array<unsigned char, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> storage{};
    DWORD used = 0;
    if (!GetTokenInformation(token.get(), TokenUser, storage.data(), static_cast<DWORD>(storage.size()), &used) ||
        used < sizeof(TOKEN_USER) || used > storage.size())
        return {};
    TOKEN_USER user{};
    std::memcpy(&user, storage.data(), sizeof(user));
    const auto base = reinterpret_cast<std::uintptr_t>(storage.data());
    const auto address = reinterpret_cast<std::uintptr_t>(user.User.Sid);
    if (!user.User.Sid || address < base || address - base > used || used - (address - base) < 8)
        return {};
    const auto offset = address - base;
    const auto count = storage[offset + 1];
    const auto length = 8 + 4 * std::size_t{count};
    if (count > SID_MAX_SUB_AUTHORITIES || length > used - offset)
        return {};
    const QByteArray sid(reinterpret_cast<const char*>(storage.data() + offset), static_cast<qsizetype>(length));
    return userSid(sid) ? sid : QByteArray{};
}

// Query each component without traversing a reparse point. Ancestors stay
// pinned until this observation ends; any existing leaf is a residual artifact.
Absence packagePresence(const QString& path) {
    QString normalized;
    if (!download_artifacts::validPath(path, normalized))
        return Absence::Unknown;
    const auto components = normalized.mid(3).split(QLatin1Char('/'));
    QString current = normalized.left(3);
    std::vector<Handle> pins;
    for (qsizetype index = 0; index <= components.size(); ++index) {
        if (index) {
            if (!current.endsWith(QLatin1Char('/')))
                current += QLatin1Char('/');
            current += components[index - 1];
        }
        Handle handle(CreateFileW(nativePath(current).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle.valid())
            return missing(GetLastError()) ? Absence::Absent : Absence::Unknown;
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)))
            return Absence::Unknown;
        if (index == components.size())
            return Absence::Present;
        if (!(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Absence::Unknown;
        pins.push_back(std::move(handle));
    }
    return Absence::Unknown;
}

class RegistryKey final {
  public:
    ~RegistryKey() { reset(); }
    RegistryKey() = default;
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    HKEY get() const { return value_; }
    void reset(HKEY replacement = nullptr) {
        if (value_)
            RegCloseKey(value_);
        value_ = replacement;
    }

  private:
    HKEY value_{nullptr};
};

bool ordinaryRegistryKey(HKEY key) {
    DWORD type = 0, bytes = 0;
    const auto result = RegQueryValueExW(key, L"SymbolicLinkValue", nullptr, &type, nullptr, &bytes);
    // A link (or any unexpected value at this reserved name) cannot prove
    // absence. Never reopen without REG_OPTION_OPEN_LINK to follow its target.
    return result == ERROR_FILE_NOT_FOUND;
}

Absence registryPresence(const QString& path) {
    constexpr REGSAM access = KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY;
    // Enter through the documented OS key for Classes\Local Settings. Do not
    // traverse the OS-managed Classes alias or permit links below this anchor.
    HKEY parent = HKEY_CURRENT_USER_LOCAL_SETTINGS;
    RegistryKey current;
    const auto components = path.split(QLatin1Char('\\'));
    if (components.isEmpty())
        return Absence::Unknown;
    for (const auto& component : components) {
        if (component.isEmpty())
            return Absence::Unknown;
        HKEY raw = nullptr;
        const auto result = RegOpenKeyExW(parent, component.toStdWString().c_str(), REG_OPTION_OPEN_LINK, access, &raw);
        if (result != ERROR_SUCCESS)
            return missing(static_cast<DWORD>(result)) ? Absence::Absent : Absence::Unknown;
        current.reset(raw);
        if (!ordinaryRegistryKey(current.get()))
            return Absence::Unknown;
        parent = current.get();
    }
    return Absence::Present;
}

using ProfileLocator = Native::StorageLocation;
struct SystemState final {
    std::optional<ProfileLocator> locator{};
};

std::optional<ProfileLocator> locateCreatedProfile(const QString& name, const QByteArray& sid,
                                                   const QByteArray& creator_sid) {
    if (!validName(name) || !canonicalSid(sid) || !userSid(creator_sid))
        return {};
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(const_cast<char*>(sid.constData()), &sid_text))
        return {};
    const auto free_sid_text = qScopeGuard([&] { LocalFree(sid_text); });
    PWSTR base = nullptr, folder = nullptr;
    const auto free_paths = qScopeGuard([&] {
        CoTaskMemFree(base);
        CoTaskMemFree(folder);
    });
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &base)) || !base ||
        FAILED(GetAppContainerFolderPath(sid_text, &folder)) || !folder)
        return {};
    QString normalized_base, normalized_folder;
    if (!download_artifacts::validPath(QString::fromWCharArray(base), normalized_base) ||
        !download_artifacts::validPath(QString::fromWCharArray(folder), normalized_folder))
        return {};
    ProfileLocator result;
    result.name = name;
    result.sid = sid;
    result.creator_user_sid = creator_sid;
    // Preserve the exact OS result independently of the derived package parent.
    result.ac_path = QString::fromWCharArray(folder);
    result.package_root = normalized_base + QStringLiteral("/Packages/") + name;
    if (normalized_folder.compare(result.package_root + QStringLiteral("/AC"), Qt::CaseInsensitive) != 0)
        return {};
    // Preserve the actual OS-returned spelling for the later observation,
    // including on a directory configured for case-sensitive lookup.
    result.package_root = normalized_folder.chopped(3);
    std::vector<Handle> pins;
    if (!download_artifacts::pinAncestors(normalized_folder + QStringLiteral("/placeholder"), pins))
        return {};
    const auto registry_base = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
    const auto text = QString::fromWCharArray(sid_text);
    result.mappings_key = registry_base + QStringLiteral("Mappings\\") + text;
    result.storage_key = registry_base + QStringLiteral("Storage\\") + text;
    if (registryPresence(result.mappings_key) == Absence::Unknown ||
        registryPresence(result.storage_key) == Absence::Unknown)
        return {};
    return result;
}

Native::Result createSystemProfile(const QString& name, SystemState& state) {
    Native::Result result;
    const auto creator_sid = currentDesktopUserSid();
    if (creator_sid.isEmpty()) {
        result.hresult = code(E_ACCESSDENIED);
        return result;
    }
    PSID raw = nullptr;
    result.hresult = code(CreateAppContainerProfile(name.toStdWString().c_str(), L"RC Music media worker",
                                                    L"Private media conversion worker profile", nullptr, 0, &raw));
    const auto free_sid = qScopeGuard([&] {
        if (raw)
            FreeSid(raw);
    });
    try {
        if (result.hresult == code(S_OK) && raw && IsValidSid(raw) && GetLengthSid(raw) == 40) {
            const QByteArray sid(static_cast<const char*>(raw), 40);
            state.locator = locateCreatedProfile(name, sid, creator_sid);
            if (state.locator)
                result.sid = sid;
        }
    } catch (...) {
        state.locator.reset();
        result.sid.clear();
    }
    if (raw)
        result.sid_released = FreeSid(std::exchange(raw, nullptr)) == nullptr;
    if (!result.sid_released) {
        state.locator.reset();
        result.sid.clear();
    }
    return result;
}

Absence observeCreatedProfile(const QString& name, const QByteArray& sid, const SystemState& state) {
    if (!currentDesktopUser() || !state.locator || state.locator->name != name || state.locator->sid != sid)
        return Absence::Unknown;
    // Use only the locations captured while this exact S_OK-created profile
    // existed. Re-reading KnownFolder after Delete could observe a new base.
    const auto& locator = *state.locator;
    const std::array observations{packagePresence(locator.package_root), registryPresence(locator.mappings_key),
                                  registryPresence(locator.storage_key)};
    bool present = false;
    for (const auto observation : observations) {
        if (observation == Absence::Unknown)
            return Absence::Unknown;
        present |= observation == Absence::Present;
    }
    return present ? Absence::Present : Absence::Absent;
}
} // namespace

struct WindowsNativeMediaProfile::Impl final {
    Calls calls;
    Report report{};
    bool create_entered{false};
    bool owns_pair{false};
    bool locating{false};
    explicit Impl(Calls value) : calls(std::move(value)) {}
};

WindowsNativeMediaProfile::WindowsNativeMediaProfile(Calls calls) : impl_(std::make_unique<Impl>(std::move(calls))) {}
WindowsNativeMediaProfile::~WindowsNativeMediaProfile() = default;
std::unique_ptr<WindowsNativeMediaProfile> WindowsNativeMediaProfile::system() {
    const auto state = std::make_shared<SystemState>();
    Calls calls{.create = [state](const QString& name) { return createSystemProfile(name, *state); },
                .remove =
                    [state](const QString& name) {
                        if (!currentDesktopUser() || !state->locator || state->locator->name != name)
                            return code(E_INVALIDARG);
                        return code(DeleteAppContainerProfile(name.toStdWString().c_str()));
                    },
                .observe = [state](const QString& name,
                                   const QByteArray& sid) { return observeCreatedProfile(name, sid, *state); },
                .location = [state](const QString& name, const QByteArray& sid) -> std::optional<StorageLocation> {
                    if (!state->locator || state->locator->name != name || state->locator->sid != sid ||
                        currentDesktopUserSid() != state->locator->creator_user_sid)
                        return {};
                    // Read only the locator captured by this S_OK creation;
                    // never query KnownFolder, the profile API or Registry.
                    return state->locator;
                }};
    return std::make_unique<WindowsNativeMediaProfile>(std::move(calls));
}
WindowsNativeMediaProfile::Report WindowsNativeMediaProfile::report() const { return impl_->report; }
std::optional<WindowsNativeMediaProfile::StorageLocation>
WindowsNativeMediaProfile::capturedStorageLocation() const noexcept {
    auto& value = *impl_;
    const auto eligible = [&] {
        return value.owns_pair && value.report.create_attempted && value.report.created && value.report.sid_released &&
               value.report.create_hresult == code(S_OK) && !value.report.remove_attempted;
    };
    if (!eligible() || value.locating || !value.calls.location)
        return {};
    value.locating = true;
    const auto release = qScopeGuard([&] { value.locating = false; });
    try {
        const QString name(value.report.name.constData(), value.report.name.size());
        const QByteArray sid(value.report.sid.constData(), value.report.sid.size());
        const auto captured = value.calls.location(name, sid);
        if (!captured || !eligible() || value.report.name != name || value.report.sid != sid ||
            !validLocation(*captured, name, sid))
            return {};
        return ownedLocation(*captured);
    } catch (...) {
        return {};
    }
}
WindowsMediaProfileCreateResult WindowsNativeMediaProfile::create(const QString& derived_name) {
    auto& value = *impl_;
    if (value.create_entered)
        return {};
    value.create_entered = true;
    try {
        if (!validName(derived_name) || !value.calls.create || !value.calls.remove || !value.calls.observe)
            return {};
        value.report.name = derived_name;
        value.report.create_attempted = true;
        const auto result = value.calls.create(derived_name);
        value.report.create_hresult = result.hresult;
        value.report.created = result.hresult == code(S_OK);
        value.report.sid_released = result.sid_released;
        if (result.hresult == code(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)))
            return {.status = WindowsMediaProfileCreateStatus::AlreadyExists};
        if (!value.report.created || !result.sid_released || !canonicalSid(result.sid))
            return {};
        value.report.sid = result.sid;
        value.owns_pair = true;
        return {.status = WindowsMediaProfileCreateStatus::Created, .sid = result.sid};
    } catch (...) {
        return {};
    }
}
WindowsMediaProfileRemovalResult WindowsNativeMediaProfile::remove(const QString& derived_name,
                                                                   const QByteArray& live_created_sid) {
    auto& value = *impl_;
    if (!value.owns_pair || value.report.remove_attempted || derived_name != value.report.name ||
        live_created_sid != value.report.sid)
        return {};
    value.report.remove_attempted = true;
    try {
        value.report.remove_hresult = value.calls.remove(derived_name);
    } catch (...) {
        return {};
    }
    if (*value.report.remove_hresult != code(S_OK))
        return {};
    try {
        const auto observed = value.calls.observe(derived_name, live_created_sid);
        if (observed == Absence::Absent || observed == Absence::Present || observed == Absence::Unknown)
            value.report.absence = observed;
    } catch (...) {
        value.report.absence = Absence::Unknown;
    }
    return {.api_succeeded = true, .absence = value.report.absence};
}
} // namespace music_player::m0
