#include "AppContainerRegistryLocationQuery.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace music_player::sandbox_probe {
namespace {
using Query = AppContainerRegistryLocationQuery;
constexpr std::uint32_t buffer_too_small = 0xc0000023U, buffer_overflow = 0x80000005U;
bool canonicalSid(const std::span<const std::uint8_t> sid) noexcept {
    constexpr std::uint8_t prefix[]{1, 8, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0};
    return sid.size() == 40 && std::equal(std::begin(prefix), std::end(prefix), sid.begin());
}
bool admitted(const Query::Token& token, const std::span<const std::uint8_t> sid) noexcept {
    return token.complete && token.primary && token.app_container && !token.elevated && !token.ui_access &&
           token.token_query_called && token.token_error == 0 && token.failed_information_class == 0 &&
           token.token_close_called && token.token_close_error == 0 && token.thread_called &&
           token.thread_error == Query::no_thread_token && !token.thread_close_called && token.capability_count == 0 &&
           token.integrity_rid == Query::low_integrity_rid && token.package_sid.size() == sid.size() &&
           std::equal(token.package_sid.begin(), token.package_sid.end(), sid.begin());
}
std::uint32_t u32(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
        value |= std::uint32_t{bytes[index]} << (8 * index);
    return value;
}
char16_t unit(const std::span<const std::uint8_t> bytes, const std::size_t index) noexcept {
    return static_cast<char16_t>(std::uint16_t{bytes[2 * index]} | (std::uint16_t{bytes[2 * index + 1]} << 8));
}
bool validUtf16(const std::span<const std::uint8_t> bytes) noexcept {
    const auto count = bytes.size() / 2;
    for (std::size_t index = 0; index < count; ++index) {
        const auto value = unit(bytes, index);
        if (!value)
            return false;
        if (value >= 0xd800 && value <= 0xdbff) {
            if (++index == count)
                return false;
            const auto low = unit(bytes, index);
            if (low < 0xdc00 || low > 0xdfff)
                return false;
        } else if (value >= 0xdc00 && value <= 0xdfff)
            return false;
    }
    return true;
}
void queryName(Query::Result& result, const Query::Calls& calls) {
    result.reason = Query::Reason::NameSize;
    result.size_called = true;
    result.size_query = calls.query_name(result.location.owned_key, {});
    result.size_returned = true;
    if (result.size_query.ntstatus != buffer_too_small && result.size_query.ntstatus != buffer_overflow)
        return;
    const auto required = result.size_query.bytes;
    if (required < 6 || required > Query::maximum_query_bytes || required % 2) {
        result.reason = Query::Reason::Bounds;
        return;
    }
    // Bound the size before allocation, then read KEY_NAME_INFORMATION once.
    std::vector<std::uint8_t> bytes(required);
    result.reason = Query::Reason::NameRead;
    result.read_called = true;
    result.read_query = calls.query_name(result.location.owned_key, bytes);
    result.read_returned = true;
    if (result.read_query.ntstatus != 0 || result.read_query.win32_error != 0)
        return;
    const auto used = result.read_query.bytes;
    if (used < 4 || used > bytes.size() || used % 2) {
        result.reason = Query::Reason::Bounds;
        return;
    }
    result.reason = Query::Reason::NameFormat;
    const auto name_bytes = u32(bytes);
    if (!name_bytes || name_bytes % 2 || name_bytes != used - 4 || name_bytes / 2 > Query::maximum_name_units)
        return;
    const auto name = std::span<const std::uint8_t>(bytes).subspan(4, name_bytes);
    if (!validUtf16(name))
        return;
    result.native_name.resize(name_bytes / 2);
    for (std::size_t index = 0; index < result.native_name.size(); ++index)
        result.native_name[index] = unit(name, index);
    result.reason = Query::Reason::None;
}
} // namespace
AppContainerRegistryLocationQuery::Result
AppContainerRegistryLocationQuery::execute(const std::span<const std::uint8_t> expected_sid,
                                           const Calls& calls) noexcept {
    Result result;
    if (!calls.capture_token || !calls.get_location || !calls.query_name || !calls.close_key)
        return result;
    if (!canonicalSid(expected_sid)) {
        result.reason = Reason::ExpectedSid;
        return result;
    }
    // The expected identity must not change beneath a trusted port callback.
    std::array<std::uint8_t, 40> expected_copy{};
    std::copy(expected_sid.begin(), expected_sid.end(), expected_copy.begin());
    Handle owned = 0;
    try {
        result.reason = Reason::Token;
        result.token_called = true;
        result.token = calls.capture_token();
        result.token_returned = true;
        if (!admitted(result.token, expected_copy))
            return result;
        result.reason = Reason::Location;
        result.location_called = true;
        result.location = calls.get_location(key_read);
        result.location_returned = true;
        if (result.location.uncertain_output || (result.location.hresult != 0 && result.location.owned_key)) {
            result.location.uncertain_output = true;
            result.reason = Reason::UncertainOutput;
            result.outcome = Outcome::CloseUnknown;
            return result;
        }
        result.outcome = Outcome::NativeApiError;
        if (result.location.hresult != 0 || !result.location.owned_key)
            return result;
        owned = result.location.owned_key;
        queryName(result, calls);
    } catch (...) {
        result.reason = Reason::Exception;
        if (result.location_called && !result.location_returned) {
            result.location.uncertain_output = true;
            result.outcome = Outcome::CloseUnknown;
        }
    }
    if (owned) {
        result.close_called = true;
        try {
            result.close_error = calls.close_key(owned);
            result.close_returned = true;
        } catch (...) {
            result.reason = Reason::Exception;
        }
        if (!result.close_returned || result.close_error != 0) {
            if (result.close_returned)
                result.reason = Reason::Close;
            result.outcome = Outcome::CloseUnknown;
        } else if (result.reason == Reason::None)
            result.outcome = Outcome::CapturedName;
    }
    return result;
}
} // namespace music_player::sandbox_probe
