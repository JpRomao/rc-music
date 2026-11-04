#include "AppContainerObserverIdentity.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Identity = AppContainerObserverIdentity;
bool validSid(const std::span<const std::uint8_t> sid) {
    return sid.size() >= 8 && sid.size() <= Identity::maximum_sid_bytes && sid[0] == SID_REVISION && sid[1] <= 15 &&
           sid.size() == 8 + 4 * std::size_t{sid[1]};
}
bool completeIdentity(const Identity::Snapshot& value) {
    const bool known_elevation = value.elevation_type == Identity::ElevationType::Default ||
                                 value.elevation_type == Identity::ElevationType::Full ||
                                 value.elevation_type == Identity::ElevationType::Limited;
    return value.complete && value.error == ERROR_SUCCESS && validSid(value.user_sid) && known_elevation;
}
struct TokenReader final {
    HANDLE token;
    Identity::Snapshot& result;
    Identity::Bytes bytes{};
    bool fail(const DWORD error) {
        result.error = error;
        return false;
    }
    template <typename Value> bool fixed(const TOKEN_INFORMATION_CLASS kind, Value& value) {
        result.failed_information_class = static_cast<std::uint32_t>(kind);
        DWORD used{};
        if (!GetTokenInformation(token, kind, &value, static_cast<DWORD>(sizeof(value)), &used))
            return fail(GetLastError());
        return used == sizeof(value) || fail(ERROR_INVALID_DATA);
    }
    bool variable(const TOKEN_INFORMATION_CLASS kind, const std::size_t minimum) {
        result.failed_information_class = static_cast<std::uint32_t>(kind);
        DWORD needed{};
        if (GetTokenInformation(token, kind, nullptr, 0, &needed))
            return fail(ERROR_INVALID_DATA);
        const auto error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return fail(error);
        if (needed < minimum || needed > Identity::maximum_token_bytes)
            return fail(ERROR_BAD_LENGTH);
        bytes.assign(needed, 0);
        DWORD used{};
        // A token changed during the size/read pair fails; never reopen or retry.
        if (!GetTokenInformation(token, kind, bytes.data(), needed, &used))
            return fail(GetLastError());
        if (used < minimum || used > needed)
            return fail(ERROR_INVALID_DATA);
        bytes.resize(used);
        return true;
    }
    template <typename Value> bool read(const std::size_t offset, Value& value) {
        if (offset > bytes.size() || sizeof(value) > bytes.size() - offset)
            return fail(ERROR_INVALID_DATA);
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return true;
    }
    bool sid(const PSID pointer, Identity::Bytes& value) {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto start = reinterpret_cast<std::uintptr_t>(bytes.data());
        if (!pointer || address < start || address - start > bytes.size())
            return fail(ERROR_INVALID_DATA);
        const auto offset = address - start;
        if (bytes.size() - offset < 8)
            return fail(ERROR_INVALID_DATA);
        const auto count = bytes[offset + 1];
        const auto length = 8 + 4 * std::size_t{count};
        if (count > 15 || length > bytes.size() - offset)
            return fail(ERROR_INVALID_DATA);
        const std::span<const std::uint8_t> view{bytes.data() + offset, length};
        if (!validSid(view))
            return fail(ERROR_INVALID_DATA);
        value.assign(view.begin(), view.end());
        return true;
    }
    template <typename Consumer> bool groups(const TOKEN_INFORMATION_CLASS kind, Consumer&& consume) {
        if (!variable(kind, sizeof(DWORD)))
            return false;
        DWORD count{};
        if (!read(0, count))
            return false;
        constexpr auto offset = offsetof(TOKEN_GROUPS, Groups);
        if (count > Identity::maximum_groups ||
            (count && (offset > bytes.size() || count > (bytes.size() - offset) / sizeof(SID_AND_ATTRIBUTES))))
            return fail(ERROR_BAD_LENGTH);
        for (DWORD index = 0; index < count; ++index) {
            SID_AND_ATTRIBUTES entry{};
            Identity::Bytes group_sid;
            if (!read(offset + index * sizeof(entry), entry) || !sid(entry.Sid, group_sid) ||
                !consume(std::move(group_sid), entry.Attributes))
                return false;
        }
        return true;
    }
};
bool readPrivileges(TokenReader& reader) {
    if (!reader.variable(TokenPrivileges, sizeof(DWORD)))
        return false;
    DWORD count{};
    if (!reader.read(0, count))
        return false;
    constexpr auto offset = offsetof(TOKEN_PRIVILEGES, Privileges);
    if (count > Identity::maximum_privileges ||
        (count &&
         (offset > reader.bytes.size() || count > (reader.bytes.size() - offset) / sizeof(LUID_AND_ATTRIBUTES))))
        return reader.fail(ERROR_BAD_LENGTH);
    for (DWORD index = 0; index < count; ++index) {
        LUID_AND_ATTRIBUTES entry{};
        if (!reader.read(offset + index * sizeof(entry), entry))
            return false;
        if (!(entry.Attributes & SE_PRIVILEGE_ENABLED))
            continue;
        std::array<wchar_t, Identity::maximum_privilege_name_units + 1> name{};
        DWORD length = static_cast<DWORD>(name.size());
        if (!LookupPrivilegeNameW(nullptr, &entry.Luid, name.data(), &length))
            return reader.fail(GetLastError());
        if (!length || length > Identity::maximum_privilege_name_units || name[length] != L'\0' ||
            std::find(name.begin(), name.begin() + length, L'\0') != name.begin() + length)
            return reader.fail(ERROR_INVALID_DATA);
        reader.result.enabled_privileges.push_back({std::wstring{name.data(), length}, entry.Attributes});
    }
    return true;
}
bool readIdentity(TokenReader& reader) {
    auto& result = reader.result;
    TOKEN_TYPE type{};
    if (!reader.fixed(TokenType, type))
        return false;
    if (type != TokenPrimary)
        return reader.fail(ERROR_ACCESS_DENIED);
    TOKEN_USER user{};
    if (!reader.variable(TokenUser, sizeof(user)) || !reader.read(0, user) ||
        !reader.sid(user.User.Sid, result.user_sid))
        return false;
    DWORD session{}, ui_access{}, app_container{};
    TOKEN_ELEVATION elevation{};
    TOKEN_ELEVATION_TYPE elevation_type{};
    if (!reader.fixed(TokenSessionId, session) || !reader.fixed(TokenElevation, elevation) ||
        !reader.fixed(TokenElevationType, elevation_type))
        return false;
    result.session_id = session;
    result.elevated = elevation.TokenIsElevated != 0;
    switch (elevation_type) {
    case TokenElevationTypeDefault:
        result.elevation_type = Identity::ElevationType::Default;
        break;
    case TokenElevationTypeFull:
        result.elevation_type = Identity::ElevationType::Full;
        break;
    case TokenElevationTypeLimited:
        result.elevation_type = Identity::ElevationType::Limited;
        break;
    default:
        return reader.fail(ERROR_INVALID_DATA);
    }
    TOKEN_MANDATORY_LABEL label{};
    Identity::Bytes integrity;
    if (!reader.variable(TokenIntegrityLevel, sizeof(label)) || !reader.read(0, label) ||
        !reader.sid(label.Label.Sid, integrity))
        return false;
    if (integrity.size() != 12 || integrity[1] != 1 || integrity[7] != 16 ||
        !std::all_of(integrity.begin() + 2, integrity.begin() + 7, [](auto byte) { return byte == 0; }))
        return reader.fail(ERROR_INVALID_DATA);
    std::memcpy(&result.integrity_rid, integrity.data() + 8, sizeof(result.integrity_rid));
    if (!reader.fixed(TokenUIAccess, ui_access) || !reader.fixed(TokenIsAppContainer, app_container))
        return false;
    result.ui_access = ui_access != 0;
    result.app_container = app_container != 0;
    TOKEN_APPCONTAINER_INFORMATION container{};
    if (!reader.variable(TokenAppContainerSid, sizeof(container)) || !reader.read(0, container))
        return false;
    if (container.TokenAppContainer) {
        if (!reader.sid(container.TokenAppContainer, result.app_container_sid))
            return false;
    }
    if (result.app_container != !result.app_container_sid.empty())
        return reader.fail(ERROR_INVALID_DATA);
    if (!reader.groups(TokenCapabilities, [&](Identity::Bytes, DWORD) {
            ++result.capability_count;
            return true;
        }))
        return false;
    constexpr std::array<std::uint8_t, 16> administrators{1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 32, 2, 0, 0};
    if (!reader.groups(TokenGroups, [&](const Identity::Bytes& group, DWORD attributes) {
            if (group.size() == administrators.size() &&
                std::equal(group.begin(), group.end(), administrators.begin())) {
                if (result.administrators_group_present)
                    return reader.fail(ERROR_INVALID_DATA);
                result.administrators_group_present = true;
                result.administrators_group_attributes = attributes;
            }
            return true;
        }))
        return false;
    if (!reader.groups(TokenRestrictedSids, [&](Identity::Bytes group, DWORD attributes) {
            result.restricted_sids.push_back({std::move(group), attributes});
            return true;
        }))
        return false;
    return readPrivileges(reader);
}
} // namespace

AppContainerObserverIdentity::Snapshot
AppContainerObserverIdentity::captureProcessToken(const Handle process) noexcept {
    Snapshot result;
    if (!process) {
        result.error = ERROR_INVALID_HANDLE;
        return result;
    }
    try {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(reinterpret_cast<HANDLE>(process), TOKEN_QUERY, &raw)) {
            result.error = GetLastError();
            return result;
        }
        const std::unique_ptr<void, decltype(&CloseHandle)> token{raw, &CloseHandle};
        TokenReader reader{token.get(), result};
        if (readIdentity(reader)) {
            result.complete = true;
            result.error = ERROR_SUCCESS;
            result.failed_information_class = 0;
        }
    } catch (const std::bad_alloc&) {
        result.error = ERROR_NOT_ENOUGH_MEMORY;
    } catch (...) {
        result.error = ERROR_GEN_FAILURE;
    }
    return result;
}
AppContainerObserverIdentity::Snapshot AppContainerObserverIdentity::captureCurrentToken() noexcept {
    return captureProcessToken(reinterpret_cast<Handle>(GetCurrentProcess()));
}
bool AppContainerObserverIdentity::normalDriver(const Snapshot& value) noexcept {
    return completeIdentity(value) && value.session_id != 0 && !value.elevated &&
           (value.elevation_type == ElevationType::Default || value.elevation_type == ElevationType::Limited) &&
           value.integrity_rid == medium_integrity_rid && !value.app_container && !value.ui_access &&
           (!value.administrators_group_present || !(value.administrators_group_attributes & SE_GROUP_ENABLED));
}
bool AppContainerObserverIdentity::elevatedObserver(const Snapshot& value) noexcept {
    return completeIdentity(value) && value.session_id != 0 && value.elevated &&
           (value.elevation_type == ElevationType::Default || value.elevation_type == ElevationType::Full) &&
           value.integrity_rid >= high_integrity_rid && !value.app_container && !value.ui_access;
}
bool AppContainerObserverIdentity::matchesCaller(const Snapshot& driver, const std::span<const std::uint8_t> sid,
                                                 const std::uint32_t session) noexcept {
    return completeIdentity(driver) && validSid(sid) && driver.session_id == session &&
           driver.user_sid.size() == sid.size() &&
           std::equal(driver.user_sid.begin(), driver.user_sid.end(), sid.begin());
}
} // namespace music_player::sandbox_probe
