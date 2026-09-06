#pragma once

#include <cstddef>
#include <cstdint>

namespace music_player::m0 {

struct ArtworkLimits final {
    static constexpr std::size_t max_encoded_bytes = 10U * 1024U * 1024U;
    static constexpr std::uint32_t max_axis_pixels = 4'096;
    static constexpr std::uint64_t max_pixel_count = 16'000'000;
    static constexpr std::uint32_t thumbnail_max_axis_pixels = 1'024;
};

enum class ArtworkLimitViolation { None, EncodedBytes, InvalidDimensions, AxisPixels, PixelCount };

[[nodiscard]] constexpr ArtworkLimitViolation validateArtworkLimits(const std::size_t encoded_bytes,
                                                                    const std::uint32_t width_pixels,
                                                                    const std::uint32_t height_pixels) noexcept {
    if (encoded_bytes > ArtworkLimits::max_encoded_bytes) {
        return ArtworkLimitViolation::EncodedBytes;
    }
    if (width_pixels == 0 || height_pixels == 0) {
        return ArtworkLimitViolation::InvalidDimensions;
    }
    if (width_pixels > ArtworkLimits::max_axis_pixels || height_pixels > ArtworkLimits::max_axis_pixels) {
        return ArtworkLimitViolation::AxisPixels;
    }
    const auto pixel_count = static_cast<std::uint64_t>(width_pixels) * static_cast<std::uint64_t>(height_pixels);
    if (pixel_count > ArtworkLimits::max_pixel_count) {
        return ArtworkLimitViolation::PixelCount;
    }
    return ArtworkLimitViolation::None;
}

} // namespace music_player::m0
