#include "m0/domain/ArtworkLimits.hpp"

#include <catch2/catch_test_macros.hpp>

namespace music_player::m0::test {

TEST_CASE("artwork byte limit policy accepts values below and exactly at the boundary",
          "[m0][artwork][limits][policy]") {
    CHECK(validateArtworkLimits(ArtworkLimits::max_encoded_bytes - 1, 1, 1) == ArtworkLimitViolation::None);
    CHECK(validateArtworkLimits(ArtworkLimits::max_encoded_bytes, 1, 1) == ArtworkLimitViolation::None);
}

TEST_CASE("artwork byte limit policy rejects a value above the boundary", "[m0][artwork][limits][policy]") {
    CHECK(validateArtworkLimits(ArtworkLimits::max_encoded_bytes + 1, 1, 1) == ArtworkLimitViolation::EncodedBytes);
}

TEST_CASE("artwork dimension policy accepts values below and exactly at its boundaries",
          "[m0][artwork][limits][policy]") {
    CHECK(validateArtworkLimits(1, ArtworkLimits::max_axis_pixels - 1, 1) == ArtworkLimitViolation::None);
    CHECK(validateArtworkLimits(1, ArtworkLimits::max_axis_pixels, 1) == ArtworkLimitViolation::None);
    CHECK(validateArtworkLimits(1, 4'000, 3'999) == ArtworkLimitViolation::None);
    CHECK(validateArtworkLimits(1, 4'000, 4'000) == ArtworkLimitViolation::None);
    CHECK(ArtworkLimits::thumbnail_max_axis_pixels == 1'024);
}

TEST_CASE("artwork dimension policy rejects invalid and above-boundary values", "[m0][artwork][limits][policy]") {
    CHECK(validateArtworkLimits(1, 0, 1) == ArtworkLimitViolation::InvalidDimensions);
    CHECK(validateArtworkLimits(1, 1, 0) == ArtworkLimitViolation::InvalidDimensions);
    CHECK(validateArtworkLimits(1, ArtworkLimits::max_axis_pixels + 1, 1) == ArtworkLimitViolation::AxisPixels);
    CHECK(validateArtworkLimits(1, 1, ArtworkLimits::max_axis_pixels + 1) == ArtworkLimitViolation::AxisPixels);
    CHECK(validateArtworkLimits(1, 4'001, 4'000) == ArtworkLimitViolation::PixelCount);
}

} // namespace music_player::m0::test
