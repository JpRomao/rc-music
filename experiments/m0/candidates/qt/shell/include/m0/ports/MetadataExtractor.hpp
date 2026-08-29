#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace music_player::m0 {

struct MetadataTags final {
    std::optional<std::string> title{};
    std::optional<std::string> artist{};
    std::optional<std::string> album{};
    std::optional<std::uint32_t> track_number{};
    std::optional<std::uint32_t> year{};
};

struct AudioStreamProperties final {
    std::optional<std::int64_t> duration_ms{};
    std::optional<std::uint32_t> sample_rate_hz{};
    std::optional<std::uint16_t> channels{};
    std::optional<std::uint16_t> bit_depth{};
    std::optional<std::uint32_t> bitrate_kbps{};
    std::string container{};
    std::string codec{};
};

enum class MetadataField { Title, Artist, Album };

enum class MetadataDiagnosticCode { FieldTooLarge };

struct MetadataDiagnostic final {
    MetadataDiagnosticCode code{MetadataDiagnosticCode::FieldTooLarge};
    MetadataField field{MetadataField::Title};
    std::size_t observed_bytes{0};
    std::size_t limit_bytes{0};
};

struct TrackMetadata final {
    std::filesystem::path source_path{};
    std::uintmax_t file_size_bytes{0};
    MetadataTags tags{};
    std::string display_title{};
    AudioStreamProperties audio{};
    std::vector<MetadataDiagnostic> diagnostics{};
};

enum class MetadataReadErrorCode {
    SourceUnavailable,
    UnsupportedFormat,
    InvalidData,
    ReadFailed,
};

struct MetadataReadError final {
    MetadataReadErrorCode code{MetadataReadErrorCode::ReadFailed};
    std::string message{};
};

using MetadataReadResult = std::variant<TrackMetadata, MetadataReadError>;

class MetadataExtractor {
  public:
    virtual ~MetadataExtractor() = default;

    [[nodiscard]] virtual MetadataReadResult read(const std::filesystem::path& source_path) const = 0;
};

} // namespace music_player::m0
