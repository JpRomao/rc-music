#include "adapters/WindowsNativeMediaProfile.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QDir>
#include <QScopeGuard>

#include <Sddl.h>
#include <ShlObj.h>
#include <userenv.h>

#include <array>
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

struct ProfileLocator final {
    QString name{};
    QByteArray sid{};
    QString package_root{};
    QString mappings_key{};
    QString storage_key{};
};
struct SystemState final {
    std::optional<ProfileLocator> locator{};
};

std::optional<ProfileLocator> locateCreatedProfile(const QString& name, const QByteArray& sid) {
    if (!validName(name) || !canonicalSid(sid))
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
    if (!currentDesktopUser()) {
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
            state.locator = locateCreatedProfile(name, sid);
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
                                   const QByteArray& sid) { return observeCreatedProfile(name, sid, *state); }};
    return std::make_unique<WindowsNativeMediaProfile>(std::move(calls));
}
WindowsNativeMediaProfile::Report WindowsNativeMediaProfile::report() const { return impl_->report; }
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
