#include "AppContainerStorageSecurity.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstddef>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Security = AppContainerStorageSecurity;
using Bytes = std::span<const std::uint8_t>;
constexpr SECURITY_INFORMATION information =
    OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
static_assert(sizeof(SECURITY_DESCRIPTOR_RELATIVE) == Security::minimum_sd_bytes);
bool extent(const std::size_t offset, const std::size_t bytes, const std::size_t available) {
    return offset <= available && bytes <= available - offset;
}
std::uint16_t word(const Bytes bytes, const std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (std::uint16_t{bytes[at + 1]} << 8));
}
std::uint32_t dword(const Bytes bytes, const std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 4; ++shift)
        value |= std::uint32_t{bytes[at + shift]} << (8 * shift);
    return value;
}
bool sid(const Bytes bytes, const std::uint32_t offset, std::size_t& length) {
    length = 0;
    if (!offset)
        return true;
    if (offset < Security::minimum_sd_bytes || offset % 4 != 0 || !extent(offset, 8, bytes.size()) ||
        bytes[offset] != SID_REVISION || bytes[offset + 1] > SID_MAX_SUB_AUTHORITIES)
        return false;
    length = 8 + 4 * std::size_t{bytes[offset + 1]};
    return length <= Security::maximum_sid_bytes && extent(offset, length, bytes.size());
}
bool acl(const Bytes bytes, const std::uint32_t offset) {
    if (offset < Security::minimum_sd_bytes || offset % 4 != 0 || !extent(offset, 8, bytes.size()))
        return false;
    const auto revision = bytes[offset];
    const auto length = word(bytes, offset + 2);
    const auto count = word(bytes, offset + 4);
    if ((revision != ACL_REVISION && revision != ACL_REVISION_DS) || bytes[offset + 1] != 0 ||
        word(bytes, offset + 6) != 0 || length < 8 || !extent(offset, length, bytes.size()) || count > (length - 8) / 4)
        return false;
    std::size_t cursor = std::size_t{offset} + 8;
    const auto end = std::size_t{offset} + length;
    for (std::uint16_t index = 0; index < count; ++index) {
        if (!extent(cursor, 4, end))
            return false;
        const auto ace_length = word(bytes, cursor + 2);
        if (ace_length < 4 || ace_length % 4 != 0 || !extent(cursor, ace_length, end))
            return false;
        // The payload and ACE type remain opaque. No access-policy claim is
        // derived from an ACE merely fitting inside this captured ACL.
        cursor += ace_length;
    }
    return true; // Unused ACL capacity, if any, is preserved in raw_sd.
}
struct Layout final {
    std::uint32_t owner_offset{};
    std::size_t owner_length{};
    Security::Dacl dacl{Security::Dacl::Unknown};
    bool dacl_protected{false};
};
bool layout(const Bytes bytes, Layout& result) {
    if (bytes.size() < Security::minimum_sd_bytes || bytes[0] != SECURITY_DESCRIPTOR_REVISION)
        return false;
    const auto control = word(bytes, 2);
    if (!(control & SE_SELF_RELATIVE) || (bytes[1] != 0 && !(control & SE_RM_CONTROL_VALID)) || dword(bytes, 12) != 0)
        return false;
    result.owner_offset = dword(bytes, 4);
    std::size_t group_length = 0;
    if (!sid(bytes, result.owner_offset, result.owner_length) || !sid(bytes, dword(bytes, 8), group_length))
        return false;
    const auto dacl_offset = dword(bytes, 16);
    if (!(control & SE_DACL_PRESENT)) {
        if (dacl_offset)
            return false;
        result.dacl = Security::Dacl::Absent;
    } else if (!dacl_offset)
        result.dacl = Security::Dacl::Null;
    else {
        if (!acl(bytes, dacl_offset))
            return false;
        result.dacl = Security::Dacl::Present;
    }
    // A PRESENT SACL bit with no SACL bytes is retained opaquely. This reader
    // never requested SACL information and makes no claim about its contents.
    result.dacl_protected = (control & SE_DACL_PROTECTED) != 0;
    return true;
}
Security::NativeResult nativeRead(const Security::Object object, const Security::Handle borrowed,
                                  const std::span<std::uint8_t> buffer) {
    if (buffer.size() > std::numeric_limits<DWORD>::max())
        return {ERROR_INVALID_PARAMETER, 0};
    DWORD bytes = static_cast<DWORD>(buffer.size());
    auto* output = buffer.empty() ? nullptr : static_cast<PSECURITY_DESCRIPTOR>(buffer.data());
    switch (object) {
    case Security::Object::File: {
        const auto success =
            GetKernelObjectSecurity(reinterpret_cast<HANDLE>(borrowed), information, output, bytes, &bytes);
        const auto error = success ? ERROR_SUCCESS : GetLastError();
        return {error, bytes};
    }
    case Security::Object::Key: {
        const auto error = RegGetKeySecurity(reinterpret_cast<HKEY>(borrowed), information, output, &bytes);
        return {static_cast<std::uint32_t>(error), bytes};
    }
    }
    return {ERROR_INVALID_PARAMETER, 0};
}
} // namespace

AppContainerStorageSecurity::Calls AppContainerStorageSecurity::systemCalls() {
    return {.size_query = [](const Object object, const Handle borrowed) { return nativeRead(object, borrowed, {}); },
            .read = [](const Object object, const Handle borrowed,
                       const std::span<std::uint8_t> buffer) { return nativeRead(object, borrowed, buffer); }};
}
AppContainerStorageSecurity::AppContainerStorageSecurity(Calls calls) : calls_(std::move(calls)) {}
AppContainerStorageSecurity::Result
AppContainerStorageSecurity::read(const Object object, const Handle borrowed,
                                  const std::uint64_t maximum_bytes) const noexcept {
    Result result;
    try {
        if (!borrowed || borrowed == std::numeric_limits<Handle>::max()) {
            result.error = Error::InvalidHandle;
            return result;
        }
        if (object != Object::File && object != Object::Key) {
            result.error = Error::InvalidObject;
            return result;
        }
        if (!calls_.size_query || !calls_.read) {
            result.error = Error::MissingCall;
            return result;
        }
        if (maximum_bytes < minimum_sd_bytes) {
            result.error = Error::Bounds;
            return result;
        }
        const auto size = calls_.size_query(object, borrowed);
        result.query_error = size.error;
        result.required_bytes = size.bytes;
        if (size.error != ERROR_INSUFFICIENT_BUFFER) {
            result.error = Error::SizeQuery;
            return result;
        }
        if (size.bytes < minimum_sd_bytes || size.bytes > maximum_sd_bytes || size.bytes > maximum_bytes) {
            result.error = Error::Bounds;
            return result;
        }
        QByteArray raw(static_cast<qsizetype>(size.bytes), '\0');
        const auto captured = calls_.read(
            object, borrowed, {reinterpret_cast<std::uint8_t*>(raw.data()), static_cast<std::size_t>(raw.size())});
        result.read_error = captured.error;
        result.used_bytes = captured.bytes;
        if (captured.error != ERROR_SUCCESS) {
            result.error = Error::Read;
            return result;
        }
        if (captured.bytes < minimum_sd_bytes || captured.bytes > size.bytes) {
            result.error = Error::InvalidSD;
            return result;
        }
        raw.truncate(static_cast<qsizetype>(captured.bytes));
        Layout parsed;
        if (!layout({reinterpret_cast<const std::uint8_t*>(raw.constData()), static_cast<std::size_t>(raw.size())},
                    parsed)) {
            result.error = Error::InvalidSD;
            return result;
        }
        // Include the reserved read allocation in this conservative budget,
        // even if the descriptor shrank between size query and capture.
        if (parsed.owner_length > maximum_bytes - size.bytes) {
            result.error = Error::Bounds;
            return result;
        }
        QByteArray copied_owner;
        if (parsed.owner_length)
            copied_owner =
                QByteArray{raw.constData() + parsed.owner_offset, static_cast<qsizetype>(parsed.owner_length)};
        result.raw_sd = std::move(raw);
        result.owner_sid = std::move(copied_owner);
        result.dacl = parsed.dacl;
        result.dacl_protected = parsed.dacl_protected;
        result.error = Error::None;
    } catch (...) {
        result.raw_sd.clear();
        result.owner_sid.clear();
        result.dacl = Dacl::Unknown;
        result.dacl_protected = false;
        result.error = Error::Exception;
    }
    return result;
}
} // namespace music_player::sandbox_probe
