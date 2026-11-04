#include "AppContainerRegistryNative.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winternl.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

// SDK 22621 supplies the NT types but not these user-mode declarations. The
// documented Zw entry points have the same ABI as their Nt user-mode forms.
// KEY_INFORMATION_CLASS is an enum (32-bit int); KeyNameInformation is value 3.
enum class RegistryNativeInformationClass { Name = 3 };
extern "C" NTSYSAPI NTSTATUS NTAPI NtOpenKeyEx(PHANDLE key, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                               ULONG open_options);
extern "C" NTSYSAPI NTSTATUS NTAPI NtQueryKey(HANDLE key, RegistryNativeInformationClass information_class,
                                              PVOID information, ULONG length, PULONG result_length);

namespace music_player::sandbox_probe {
namespace {
using Registry = AppContainerStorageRegistry;
using Inventory = AppContainerStorageInventory;
using Security = AppContainerStorageSecurity;
using Handle = Registry::Handle;
constexpr REGSAM read_access = KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY | READ_CONTROL | KEY_WOW64_64KEY;
constexpr ULONG no_reparse = OBJ_DONT_REPARSE | OBJ_CASE_INSENSITIVE;
constexpr NTSTATUS buffer_too_small = static_cast<NTSTATUS>(0xc0000023UL);
constexpr NTSTATUS buffer_overflow = static_cast<NTSTATUS>(0x80000005UL);
constexpr NTSTATUS type_mismatch = static_cast<NTSTATUS>(0xc0000024UL);
constexpr NTSTATUS reparse_encountered = static_cast<NTSTATUS>(0xc000050bUL);
constexpr DWORD notification_filter = REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES |
                                      REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_SECURITY |
                                      REG_NOTIFY_THREAD_AGNOSTIC;
// Bookkeeping is bounded before any API acquisition. Exhaustion is an explicit
// incomplete read, never an untracked open. Includes short-lived rechecks.
constexpr std::size_t maximum_records = 2 * (Registry::maximum_anchors + Inventory::maximum_objects);
static_assert(sizeof(NTSTATUS) == sizeof(std::uint32_t));
static_assert(sizeof(wchar_t) == 2);
static_assert(sizeof(RegistryNativeInformationClass) == sizeof(ULONG));

std::uint32_t statusError(const NTSTATUS status) {
    const auto error = static_cast<std::uint32_t>(RtlNtStatusToDosError(status));
    return status == 0 ? ERROR_SUCCESS : error ? error : ERROR_GEN_FAILURE;
}
bool missing(const std::uint32_t error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
bool predefined(const HKEY key) {
    return key == HKEY_CLASSES_ROOT || key == HKEY_CURRENT_USER || key == HKEY_LOCAL_MACHINE || key == HKEY_USERS ||
           key == HKEY_PERFORMANCE_DATA || key == HKEY_CURRENT_CONFIG || key == HKEY_DYN_DATA ||
           key == HKEY_CURRENT_USER_LOCAL_SETTINGS || key == HKEY_PERFORMANCE_TEXT || key == HKEY_PERFORMANCE_NLSTEXT;
}
bool validKey(const HKEY key) { return key && key != reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE) && !predefined(key); }
bool component(const QString& name) {
    if (name.isEmpty() || name.size() > Inventory::maximum_name_units || !name.isValidUtf16() || name == "." ||
        name == "..")
        return false;
    for (const auto character : name)
        if (character.unicode() < 32 || character == u'/' || character == u'\\')
            return false;
    return true;
}
bool nativeNameValid(const QString& name) {
    return !name.isEmpty() && name.isValidUtf16() && !name.contains(QChar{0}) &&
           name.startsWith(QStringLiteral("\\REGISTRY\\"), Qt::CaseInsensitive);
}
struct NameResult final {
    std::uint32_t error{ERROR_INVALID_DATA}, ntstatus{Inventory::not_called};
    QString name{};
};
NameResult queryName(const HKEY key, const std::uint32_t maximum_units) {
    NameResult result;
    if (!maximum_units || maximum_units > Registry::maximum_native_name_units) {
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }
    ULONG required = 0;
    auto status = NtQueryKey(key, RegistryNativeInformationClass::Name, nullptr, 0, &required);
    result.ntstatus = static_cast<std::uint32_t>(status);
    if (status != buffer_too_small && status != buffer_overflow) {
        result.error = status ? statusError(status) : ERROR_INVALID_DATA;
        return result;
    }
    constexpr ULONG prefix = sizeof(ULONG); // offsetof(KEY_NAME_INFORMATION, Name), not sizeof(struct).
    const auto limit = prefix + maximum_units * sizeof(wchar_t);
    if (required < prefix + sizeof(wchar_t) || required > limit || required % sizeof(wchar_t)) {
        result.error = ERROR_INSUFFICIENT_BUFFER;
        return result;
    }
    std::vector<wchar_t> buffer(required / sizeof(wchar_t));
    ULONG used = 0;
    status = NtQueryKey(key, RegistryNativeInformationClass::Name, buffer.data(), required, &used);
    result.ntstatus = static_cast<std::uint32_t>(status);
    if (status != 0) {
        result.error = statusError(status);
        return result; // No retry or partial-buffer consumption on growth.
    }
    ULONG name_bytes = 0;
    if (used < prefix || used > required) {
        result.error = ERROR_INVALID_DATA;
        return result;
    }
    std::memcpy(&name_bytes, buffer.data(), sizeof(name_bytes));
    if (!name_bytes || name_bytes % sizeof(wchar_t) || name_bytes > used - prefix ||
        name_bytes / sizeof(wchar_t) > maximum_units) {
        result.error = ERROR_INVALID_DATA;
        return result;
    }
    result.name = QString::fromWCharArray(buffer.data() + prefix / sizeof(wchar_t),
                                          static_cast<qsizetype>(name_bytes / sizeof(wchar_t)));
    result.error = nativeNameValid(result.name) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    return result;
}
struct NativeOpen final {
    HKEY key{};
    NTSTATUS status{};
};
NativeOpen openNative(const HKEY parent, const QString& name) {
    // Callers have already bounded and validated this counted string. No
    // predefined pseudo-handle is ever passed as Native RootDirectory.
    const auto wide = name.toStdWString();
    UNICODE_STRING unicode{};
    unicode.Length = static_cast<USHORT>(wide.size() * sizeof(wchar_t));
    unicode.MaximumLength = unicode.Length;
    unicode.Buffer = const_cast<PWSTR>(wide.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &unicode;
    attributes.Attributes = no_reparse;
    HANDLE opened = nullptr;
    const auto status = NtOpenKeyEx(&opened, read_access, &attributes, 0);
    return {static_cast<HKEY>(opened), status};
}
enum class Origin { Win32Bootstrap, Win32Presence, Native };
struct Key final {
    HKEY raw{};
    Origin origin{Origin::Native};
    bool close_attempted{false}, watch_attempted{false};
    std::uint32_t close_error{Inventory::not_called};
};
struct Event final {
    HANDLE raw{};
    bool close_attempted{false};
    std::uint32_t close_error{Inventory::not_called};
};
struct NativeCalls final {
    std::array<Key, maximum_records> keys{};
    std::array<Event, maximum_records> events{};
    std::size_t key_count{}, event_count{};

    Key* key(const Handle raw) {
        for (std::size_t index = 0; index < key_count; ++index)
            if (reinterpret_cast<Handle>(keys[index].raw) == raw)
                return &keys[index];
        return nullptr;
    }
    Key* liveKey(const Handle raw) {
        auto* found = key(raw);
        return found && !found->close_attempted ? found : nullptr;
    }
    Event* event(const Handle raw) {
        for (std::size_t index = 0; index < event_count; ++index)
            if (reinterpret_cast<Handle>(events[index].raw) == raw)
                return &events[index];
        return nullptr;
    }
    bool own(Registry::OpenResult& result, const HKEY raw, const Origin origin) noexcept {
        if (!raw)
            return false;
        if (!validKey(raw) || key_count >= keys.size() || result.owned_count >= result.owned_keys.size()) {
            result.error = ERROR_INVALID_HANDLE;
            return false;
        }
        result.owned_keys[result.owned_count++] = reinterpret_cast<Handle>(raw);
        if (key(reinterpret_cast<Handle>(raw))) {
            result.error = ERROR_INVALID_HANDLE;
            return false; // Duplicate live handle is anomalous; never overwrite its origin.
        }
        keys[key_count++] = {raw, origin}; // Fixed storage; no post-open allocation.
        return true;
    }
    Registry::OpenResult software() noexcept {
        Registry::OpenResult result;
        try {
            if (keys.size() - key_count < 2) {
                result.error = ERROR_TOO_MANY_OPEN_FILES;
                return result;
            }
            HKEY bootstrap = nullptr;
            const auto error = static_cast<std::uint32_t>(RegOpenKeyExW(HKEY_CURRENT_USER_LOCAL_SETTINGS, L"Software",
                                                                        REG_OPTION_OPEN_LINK, read_access, &bootstrap));
            result.error = error;
            const bool retained = bootstrap && own(result, bootstrap, Origin::Win32Bootstrap);
            if (error != ERROR_SUCCESS || !retained) {
                if (!bootstrap && missing(error))
                    result.state = Registry::OpenState::Missing;
                else if (error == ERROR_SUCCESS)
                    result.error = ERROR_INVALID_HANDLE;
                return result;
            }
            const auto name = queryName(bootstrap, Registry::maximum_native_name_units);
            result.error = name.error;
            result.ntstatus = name.ntstatus;
            if (name.error)
                return result;
            const auto opened = openNative(nullptr, name.name);
            result.ntstatus = static_cast<std::uint32_t>(opened.status);
            result.error = statusError(opened.status);
            const bool owned = opened.key && own(result, opened.key, Origin::Native);
            if (opened.status == 0 && owned) {
                result.state = Registry::OpenState::Present;
                result.key = reinterpret_cast<Handle>(opened.key);
            } else if (!opened.key && (opened.status == type_mismatch || opened.status == reparse_encountered))
                result.state = Registry::OpenState::RejectedLink;
            else if (opened.status == 0)
                result.error = ERROR_INVALID_HANDLE;
            // The OPEN_LINK bootstrap existed. A later Native missing result
            // cannot establish absence of the original fixed Software binding.
        } catch (...) {
            result.state = Registry::OpenState::Unknown;
            result.key = 0;
            result.error = ERROR_GEN_FAILURE;
        }
        return result; // Includes all retained keys even on failure.
    }
    Registry::OpenResult child(const Handle parent, const QString& name) noexcept {
        Registry::OpenResult result;
        try {
            if (!component(name)) {
                result.error = ERROR_INVALID_NAME;
                return result;
            }
            const auto* held = liveKey(parent);
            if (!held || held->origin != Origin::Native) {
                result.error = ERROR_INVALID_HANDLE;
                return result;
            }
            if (keys.size() - key_count < 2) {
                result.error = ERROR_TOO_MANY_OPEN_FILES;
                return result;
            }
            const auto opened = openNative(held->raw, name);
            result.ntstatus = static_cast<std::uint32_t>(opened.status);
            result.error = statusError(opened.status);
            const bool owned = opened.key && own(result, opened.key, Origin::Native);
            if (opened.status == 0 && owned) {
                result.state = Registry::OpenState::Present;
                result.key = reinterpret_cast<Handle>(opened.key);
                return result;
            }
            if (opened.key)
                return result; // Unexpected output on failure is retained, never queried.
            if (opened.status == 0) {
                result.error = ERROR_INVALID_HANDLE;
                return result;
            }
            if (opened.status == type_mismatch || opened.status == reparse_encountered) {
                result.state = Registry::OpenState::RejectedLink;
                return result;
            }
            if (!missing(result.error))
                return result;
            // No-follow not-found alone can represent an unqualified dangling
            // link. Probe only this component with OPEN_LINK, never value data,
            // SD, children, a target path or a follow fallback.
            const auto wide = name.toStdWString();
            HKEY presence = nullptr;
            const auto probe = static_cast<std::uint32_t>(
                RegOpenKeyExW(held->raw, wide.c_str(), REG_OPTION_OPEN_LINK, read_access, &presence));
            if (presence)
                static_cast<void>(own(result, presence, Origin::Win32Presence));
            if (!presence && missing(probe))
                result.state = Registry::OpenState::Missing;
            // An existing/uncertain presence retains the original Native error
            // and has no usable work key. The reader owns its close obligation.
        } catch (...) {
            result.state = Registry::OpenState::Unknown;
            result.key = 0;
            result.error = ERROR_GEN_FAILURE;
        }
        return result;
    }
    Registry::KeyInfo information(const Handle raw, const std::uint32_t maximum_units) noexcept {
        Registry::KeyInfo result;
        try {
            if (!maximum_units || maximum_units > Registry::maximum_native_name_units) {
                result.error = ERROR_INVALID_PARAMETER;
                return result;
            }
            const auto* held = liveKey(raw);
            if (!held || held->origin == Origin::Win32Presence) {
                result.error = ERROR_INVALID_HANDLE;
                return result;
            }
            auto name = queryName(held->raw, maximum_units);
            result.error = name.error;
            if (name.error)
                return result;
            result.native_name = std::move(name.name);
            if (held->origin == Origin::Win32Bootstrap)
                return result; // Bootstrap witness: Native name only, never info/value/SD/children.
            DWORD subkeys = 0;
            FILETIME last_write{};
            result.error =
                static_cast<std::uint32_t>(RegQueryInfoKeyW(held->raw, nullptr, nullptr, nullptr, &subkeys, nullptr,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr, &last_write));
            if (result.error)
                return result;
            result.subkeys = subkeys;
            result.last_write = (std::uint64_t{last_write.dwHighDateTime} << 32) | last_write.dwLowDateTime;
            DWORD type = 0, bytes = 0;
            const auto error = static_cast<std::uint32_t>(
                RegQueryValueExW(held->raw, L"SymbolicLinkValue", nullptr, &type, nullptr, &bytes));
            if (error == ERROR_SUCCESS) {
                result.reserved = Registry::ReservedValue::Present;
                result.reserved_type = type;
                result.reserved_bytes = bytes;
            } else if (error == ERROR_FILE_NOT_FOUND)
                result.reserved = Registry::ReservedValue::Absent;
            else
                result.error = error;
            // Reserved absence is merely observed. It proves no ordinary-key
            // type, and a captured Native name is no object identity.
        } catch (...) {
            result.error = ERROR_GEN_FAILURE;
        }
        return result;
    }
    Registry::EnumResult enumerate(const Handle raw, const std::uint32_t index,
                                   const std::uint32_t maximum_units) noexcept {
        Registry::EnumResult result;
        try {
            if (!maximum_units || maximum_units > Inventory::maximum_name_units) {
                result.error = ERROR_INVALID_PARAMETER;
                return result;
            }
            const auto* held = liveKey(raw);
            if (!held || held->origin != Origin::Native) {
                result.error = ERROR_INVALID_HANDLE;
                return result;
            }
            std::array<wchar_t, Inventory::maximum_name_units + 1> buffer{};
            DWORD units = maximum_units + 1; // Input includes NUL; output excludes it.
            const auto error = static_cast<std::uint32_t>(
                RegEnumKeyExW(held->raw, index, buffer.data(), &units, nullptr, nullptr, nullptr, nullptr));
            if (error == ERROR_NO_MORE_ITEMS)
                return {Inventory::ChildState::End, 0, {}};
            if (error)
                return {Inventory::ChildState::Unknown, error, {}};
            if (!units || units > maximum_units || units >= buffer.size() || buffer[units] != L'\0')
                return {Inventory::ChildState::Unknown, ERROR_INVALID_DATA, {}};
            auto name = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(units));
            if (!component(name))
                return {Inventory::ChildState::Unknown, ERROR_INVALID_NAME, {}};
            return {Inventory::ChildState::Child, 0, std::move(name)};
        } catch (...) {
            result.error = ERROR_GEN_FAILURE;
        }
        return result;
    }
    Registry::WatchResult watch(const Handle raw) noexcept {
        auto* held = liveKey(raw);
        if (!held || held->origin == Origin::Win32Presence)
            return {ERROR_INVALID_HANDLE, 0};
        if (held->watch_attempted)
            return {ERROR_ALREADY_EXISTS, 0};
        held->watch_attempted = true;
        if (event_count >= events.size())
            return {ERROR_TOO_MANY_OPEN_FILES, 0};
        // Null SECURITY_ATTRIBUTES makes this unnamed event non-inheritable.
        const auto created = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!created) {
            const auto error = GetLastError();
            return {error ? error : DWORD{ERROR_GEN_FAILURE}, 0};
        }
        events[event_count++] = {created};
        const auto error =
            static_cast<std::uint32_t>(RegNotifyChangeKeyValue(held->raw, FALSE, notification_filter, created, TRUE));
        // Even a failed subscription transfers its event for explicit closure.
        // No ResetEvent, second registration, wait thread or destructor close.
        return {error, reinterpret_cast<Handle>(created)};
    }
    Registry::PollResult poll(const Handle raw) noexcept {
        const auto* held = event(raw);
        if (!held || held->close_attempted)
            return {Registry::EventState::Unknown, ERROR_INVALID_HANDLE};
        const auto wait = WaitForSingleObject(held->raw, 0);
        if (wait == WAIT_OBJECT_0)
            return {Registry::EventState::Changed, 0};
        if (wait == WAIT_TIMEOUT)
            return {Registry::EventState::Unchanged, 0};
        const auto error = wait == WAIT_FAILED ? GetLastError() : DWORD{ERROR_INVALID_DATA};
        return {Registry::EventState::Unknown, error ? error : DWORD{ERROR_GEN_FAILURE}};
    }
    std::uint32_t closeKey(const Handle raw) noexcept {
        auto* held = key(raw);
        if (!held)
            return ERROR_INVALID_HANDLE;
        if (held->close_attempted)
            return held->close_error;
        held->close_attempted = true;
        held->close_error = held->origin != Origin::Native ? static_cast<std::uint32_t>(RegCloseKey(held->raw))
                                                           : statusError(NtClose(held->raw));
        return held->close_error;
    }
    std::uint32_t closeEvent(const Handle raw) noexcept {
        auto* held = event(raw);
        if (!held)
            return ERROR_INVALID_HANDLE;
        if (held->close_attempted)
            return held->close_error;
        held->close_attempted = true;
        if (CloseHandle(held->raw))
            held->close_error = ERROR_SUCCESS;
        else {
            const auto error = GetLastError();
            held->close_error = error ? error : DWORD{ERROR_GEN_FAILURE};
        }
        return held->close_error;
    }
};
} // namespace

AppContainerStorageRegistry::Calls appContainerRegistryNativeCalls() {
    // Allocation and callback binding only. NativeCalls has no resource-owning
    // destructor. Abandoned/failed resources remain the explicit reader ledger's
    // responsibility; construction and destruction perform no native calls.
    const auto state = std::make_shared<NativeCalls>();
    const auto security = Security::systemCalls();
    Security::Calls guarded_security{
        .size_query =
            [state, security](const Security::Object object, const Handle raw) {
                if (object != Security::Object::Key)
                    return Security::NativeResult{ERROR_INVALID_PARAMETER, 0};
                const auto* held = state->liveKey(raw);
                if (!held || held->origin != Origin::Native)
                    return Security::NativeResult{ERROR_INVALID_HANDLE, 0};
                return security.size_query(object, raw);
            },
        .read =
            [state, security](const Security::Object object, const Handle raw, const std::span<std::uint8_t> buffer) {
                if (object != Security::Object::Key)
                    return Security::NativeResult{ERROR_INVALID_PARAMETER, 0};
                const auto* held = state->liveKey(raw);
                if (!held || held->origin != Origin::Native)
                    return Security::NativeResult{ERROR_INVALID_HANDLE, 0};
                if (buffer.size() > Security::maximum_sd_bytes)
                    return Security::NativeResult{ERROR_INSUFFICIENT_BUFFER, 0};
                return security.read(object, raw, buffer);
            }};
    return {
        .open_software = [state] { return state->software(); },
        .open_child = [state](const Handle parent, const QString& name) { return state->child(parent, name); },
        .query_info = [state](const Handle key, const std::uint32_t units) { return state->information(key, units); },
        .enum_child = [state](const Handle key, const std::uint32_t index,
                              const std::uint32_t units) { return state->enumerate(key, index, units); },
        .watch_once = [state](const Handle key) { return state->watch(key); },
        .poll_event = [state](const Handle event) { return state->poll(event); },
        .security = std::move(guarded_security),
        .close_key = [state](const Handle key) { return state->closeKey(key); },
        .close_event = [state](const Handle event) { return state->closeEvent(event); }};
}
} // namespace music_player::sandbox_probe
