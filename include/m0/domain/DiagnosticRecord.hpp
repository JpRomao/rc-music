#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace music_player::m0 {

enum class DiagnosticSeverity { Info, Warning, Error };

enum class DiagnosticEvent {
    ApplicationStarted,
    ApplicationStopping,
    StartupFailed,
    SourceOpenFailed,
    SourceSkipped,
    SeekRejected,
    MetadataWarning,
    PreferencesWarning,
    SessionWarning,
    PlatformWarning,
    EventsDropped,
};

enum class DiagnosticCode {
    None,
    SourceUnavailable,
    InvalidMedia,
    AccessDenied,
    BackendFailure,
    AudioOutputUnavailable,
    AudioOutputChanged,
    NotSeekable,
    Busy,
    NotReady,
    CorruptState,
    WriteFailed,
    UnsupportedVersion,
    Unavailable,
    Unknown,
    SourceVolumeRemoved,
};

struct DiagnosticEntry final {
    DiagnosticSeverity severity{DiagnosticSeverity::Info};
    DiagnosticEvent event{DiagnosticEvent::ApplicationStarted};
    DiagnosticCode code{DiagnosticCode::None};
    std::uint64_t count{1};

    friend bool operator==(const DiagnosticEntry&, const DiagnosticEntry&) = default;
};

struct DiagnosticRecord final {
    std::int64_t timestamp_utc_ms{0};
    DiagnosticEntry entry{};

    friend bool operator==(const DiagnosticRecord&, const DiagnosticRecord&) = default;
};

inline constexpr std::size_t maxDiagnosticRecordBytes = 256;

struct DiagnosticLogLimits final {
    std::uint64_t max_file_bytes{256 * 1024};
    std::size_t max_files{3};
    std::int64_t max_age_ms{7LL * 24 * 60 * 60 * 1000};
};

// Only closed names and decimal numbers are emitted, followed by one LF.
// The complete line, including LF, never exceeds maxDiagnosticRecordBytes.
[[nodiscard]] std::string formatDiagnosticRecord(const DiagnosticRecord& record);
[[nodiscard]] bool validDiagnosticLogLimits(const DiagnosticLogLimits& limits) noexcept;
[[nodiscard]] bool canAppendDiagnosticRecord(std::uint64_t file_bytes, std::uint64_t record_bytes,
                                             std::uint64_t max_file_bytes) noexcept;
// The exact age limit is retained. Negative/future timestamps fail closed.
[[nodiscard]] bool diagnosticLogExpired(std::int64_t created_ms, std::int64_t now_ms, std::int64_t max_age_ms) noexcept;
[[nodiscard]] std::uint64_t saturatingDiagnosticCount(std::uint64_t count, std::uint64_t added = 1) noexcept;

} // namespace music_player::m0
