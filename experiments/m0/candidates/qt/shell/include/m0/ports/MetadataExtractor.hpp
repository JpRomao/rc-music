#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
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

enum class MetadataField { Title, Artist, Album, TrackNumber, Year };

enum class MetadataDiagnosticCode { FieldTooLarge, RetainedMetadataTooLarge, TextSanitized, InvalidValue };

struct MetadataDiagnostic final {
    MetadataDiagnosticCode code{MetadataDiagnosticCode::FieldTooLarge};
    MetadataField field{MetadataField::Title};
    std::size_t observed_bytes{0};
    std::size_t limit_bytes{0};
};

class MetadataRetentionBudget final {
  public:
    static constexpr std::size_t field_limit_bytes = 16U * 1024U;
    static constexpr std::size_t total_limit_bytes = 256U * 1024U;

    [[nodiscard]] std::optional<MetadataDiagnostic> tryRetain(const MetadataField field,
                                                              const std::size_t field_bytes) noexcept {
        if (field_bytes > field_limit_bytes) {
            return MetadataDiagnostic{.code = MetadataDiagnosticCode::FieldTooLarge,
                                      .field = field,
                                      .observed_bytes = field_bytes,
                                      .limit_bytes = field_limit_bytes};
        }
        if (field_bytes > total_limit_bytes - retained_bytes_) {
            return MetadataDiagnostic{.code = MetadataDiagnosticCode::RetainedMetadataTooLarge,
                                      .field = field,
                                      .observed_bytes = retained_bytes_ + field_bytes,
                                      .limit_bytes = total_limit_bytes};
        }
        retained_bytes_ += field_bytes;
        return std::nullopt;
    }

    [[nodiscard]] std::size_t retainedBytes() const noexcept { return retained_bytes_; }

  private:
    std::size_t retained_bytes_{0};
};

struct ArtworkThumbnail final {
    std::uint32_t width_pixels{0};
    std::uint32_t height_pixels{0};
    std::shared_ptr<const std::vector<std::uint8_t>> rgba8888{};
};

enum class ArtworkDiagnosticCode { InvalidData, EncodedBytesExceeded, AxisPixelsExceeded, PixelCountExceeded };

struct ArtworkDiagnostic final {
    ArtworkDiagnosticCode code{ArtworkDiagnosticCode::InvalidData};
    std::size_t encoded_bytes{0};
    std::uint32_t width_pixels{0};
    std::uint32_t height_pixels{0};
};

struct TrackMetadata final {
    std::filesystem::path source_path{};
    std::uintmax_t file_size_bytes{0};
    std::optional<std::filesystem::file_time_type> last_modified_time{};
    MetadataTags tags{};
    std::string display_title{};
    AudioStreamProperties audio{};
    std::vector<MetadataDiagnostic> diagnostics{};
    std::optional<ArtworkThumbnail> artwork{};
    std::vector<ArtworkDiagnostic> artwork_diagnostics{};
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
