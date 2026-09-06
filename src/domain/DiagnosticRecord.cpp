#include "m0/domain/DiagnosticRecord.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <string_view>

namespace music_player::m0 {
namespace {

using namespace std::string_view_literals;

constexpr std::array severity_names{"info"sv, "warning"sv, "error"sv};
constexpr std::array event_names{"application_started"sv, "application_stopping"sv, "startup_failed"sv,
                                 "source_open_failed"sv,  "source_skipped"sv,       "seek_rejected"sv,
                                 "metadata_warning"sv,    "preferences_warning"sv,  "session_warning"sv,
                                 "platform_warning"sv,    "events_dropped"sv};
constexpr std::array code_names{"none"sv,
                                "source_unavailable"sv,
                                "invalid_media"sv,
                                "access_denied"sv,
                                "backend_failure"sv,
                                "audio_output_unavailable"sv,
                                "audio_output_changed"sv,
                                "not_seekable"sv,
                                "busy"sv,
                                "not_ready"sv,
                                "corrupt_state"sv,
                                "write_failed"sv,
                                "unsupported_version"sv,
                                "unavailable"sv,
                                "unknown"sv};

template <class Enum, std::size_t Size>
std::string_view closedName(const Enum value, const std::array<std::string_view, Size>& names) noexcept {
    const auto index = static_cast<std::uint64_t>(value);
    return index < Size ? names[static_cast<std::size_t>(index)] : "unknown"sv;
}

template <std::size_t Size>
constexpr std::size_t longestName(const std::array<std::string_view, Size>& names) noexcept {
    std::size_t result = "unknown"sv.size();
    for (const auto name : names) {
        if (name.size() > result)
            result = name.size();
    }
    return result;
}

constexpr auto longest_line_bytes = "utc_ms="sv.size() + 20 + " severity="sv.size() + longestName(severity_names) +
                                    " event="sv.size() + longestName(event_names) + " code="sv.size() +
                                    longestName(code_names) + " count="sv.size() + 20 + 1;
static_assert(longest_line_bytes <= maxDiagnosticRecordBytes);

template <class Number> void appendNumber(std::string& output, const Number value) {
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    output.append(buffer.data(), converted.ptr);
}

} // namespace

std::string formatDiagnosticRecord(const DiagnosticRecord& record) {
    std::string output;
    output.reserve(longest_line_bytes);
    output.append("utc_ms=");
    appendNumber(output, record.timestamp_utc_ms);
    output.append(" severity=").append(closedName(record.entry.severity, severity_names));
    output.append(" event=").append(closedName(record.entry.event, event_names));
    output.append(" code=").append(closedName(record.entry.code, code_names));
    output.append(" count=");
    appendNumber(output, record.entry.count);
    output.push_back('\n');
    return output;
}

bool validDiagnosticLogLimits(const DiagnosticLogLimits& limits) noexcept {
    constexpr DiagnosticLogLimits maximum;
    return limits.max_file_bytes > 0 && limits.max_file_bytes <= maximum.max_file_bytes && limits.max_files > 0 &&
           limits.max_files <= maximum.max_files && limits.max_age_ms > 0 && limits.max_age_ms <= maximum.max_age_ms;
}

bool canAppendDiagnosticRecord(const std::uint64_t file_bytes, const std::uint64_t record_bytes,
                               const std::uint64_t max_file_bytes) noexcept {
    return record_bytes > 0 && file_bytes <= max_file_bytes && record_bytes <= max_file_bytes - file_bytes;
}

bool diagnosticLogExpired(const std::int64_t created_ms, const std::int64_t now_ms,
                          const std::int64_t max_age_ms) noexcept {
    return created_ms < 0 || now_ms < 0 || max_age_ms <= 0 || created_ms > now_ms || now_ms - created_ms > max_age_ms;
}

std::uint64_t saturatingDiagnosticCount(const std::uint64_t count, const std::uint64_t added) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return added > maximum - count ? maximum : count + added;
}

} // namespace music_player::m0
