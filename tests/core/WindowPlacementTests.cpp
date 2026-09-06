#include "m0/domain/WindowPlacement.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace music_player::m0::test {
namespace {

constexpr WindowSize minimum_window_size{720, 520};

} // namespace

TEST_CASE("window placement values have structural equality", "[m0][qt][core][window-placement]") {
    CHECK(WindowPlacement{-1, 2, 720, 520} == WindowPlacement{-1, 2, 720, 520});
    CHECK(WindowWorkArea{-1'920, 0, 1'920, 1'040} == WindowWorkArea{-1'920, 0, 1'920, 1'040});
    CHECK(WindowSize{720, 520} == WindowSize{720, 520});
    CHECK(ResolvedWindowPlacement{{-1, 2, 720, 520}, 3} == ResolvedWindowPlacement{{-1, 2, 720, 520}, 3});
}

TEST_CASE("window placement normalizes size and accepts an exact work-area boundary",
          "[m0][qt][core][window-placement]") {
    constexpr WindowPlacement saved{100, 50, 640, 480};
    constexpr std::array work_areas{WindowWorkArea{100, 50, 720, 520}};

    const auto resolved = resolveWindowPlacement(saved, work_areas, minimum_window_size);

    REQUIRE(resolved);
    CHECK(*resolved == ResolvedWindowPlacement{{100, 50, 720, 520}, 0});

    constexpr WindowPlacement one_pixel_beyond{101, 50, 640, 480};
    CHECK_FALSE(resolveWindowPlacement(one_pixel_beyond, work_areas, minimum_window_size));
}

TEST_CASE("window placement accepts negative coordinates and preserves the matched screen index",
          "[m0][qt][core][window-placement]") {
    constexpr std::array work_areas{WindowWorkArea{0, 0, 1'920, 1'040}, WindowWorkArea{-1'920, 0, 1'920, 1'040}};
    constexpr WindowPlacement saved{-1'800, 100, 960, 640};

    const auto resolved = resolveWindowPlacement(saved, work_areas, minimum_window_size);

    REQUIRE(resolved);
    CHECK(*resolved == ResolvedWindowPlacement{saved, 1});
}

TEST_CASE("window placement rejects a removed monitor and a work area reduced by DPI",
          "[m0][qt][core][window-placement]") {
    constexpr std::array primary_only{WindowWorkArea{0, 0, 1'920, 1'040}};
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{2'000, 100, 960, 640}, primary_only, minimum_window_size));

    constexpr std::array dpi_reduced{WindowWorkArea{0, 0, 800, 600}};
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{100, 100, 960, 640}, dpi_reduced, minimum_window_size));
}

TEST_CASE("window placement must fit one work area instead of their union", "[m0][qt][core][window-placement]") {
    constexpr WindowPlacement spanning{640, 100, 720, 520};
    constexpr std::array adjacent{WindowWorkArea{0, 0, 1'000, 900}, WindowWorkArea{1'000, 0, 1'000, 900}};
    CHECK_FALSE(resolveWindowPlacement(spanning, adjacent, minimum_window_size));

    constexpr WindowPlacement in_mixed_dpi_gap{1'050, 100, 720, 520};
    constexpr std::array separated{WindowWorkArea{0, 0, 1'000, 900}, WindowWorkArea{1'500, 0, 1'000, 900}};
    CHECK_FALSE(resolveWindowPlacement(in_mixed_dpi_gap, separated, minimum_window_size));
}

TEST_CASE("window placement ignores invalid work areas without changing later indices",
          "[m0][qt][core][window-placement]") {
    constexpr std::array work_areas{WindowWorkArea{0, 0, 0, 900}, WindowWorkArea{-1'920, 0, 1'920, 1'040}};
    constexpr WindowPlacement saved{-1'800, 100, 960, 640};

    const auto resolved = resolveWindowPlacement(saved, work_areas, minimum_window_size);

    REQUIRE(resolved);
    CHECK(*resolved == ResolvedWindowPlacement{saved, 1});
}

TEST_CASE("window placement rejects invalid saved geometry minimums and screen sets",
          "[m0][qt][core][window-placement]") {
    constexpr std::array work_areas{WindowWorkArea{0, 0, 1'920, 1'040}};

    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{0, 0, 0, 520}, work_areas, minimum_window_size));
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{0, 0, 720, -1}, work_areas, minimum_window_size));
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{0, 0, 720, 520}, work_areas, WindowSize{0, 520}));
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{0, 0, 720, 520}, work_areas, WindowSize{720, -1}));
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{0, 0, 720, 520}, {}, minimum_window_size));

    constexpr std::array invalid_work_areas{WindowWorkArea{0, 0, -1, 900}, WindowWorkArea{0, 0, 900, 0}};
    CHECK_FALSE(resolveWindowPlacement(WindowPlacement{0, 0, 720, 520}, invalid_work_areas, minimum_window_size));
}

TEST_CASE("window placement rejects coordinate arithmetic overflow", "[m0][qt][core][window-placement]") {
    constexpr auto maximum = std::numeric_limits<int>::max();
    constexpr WindowPlacement horizontal_overflow{maximum - 100, 0, 720, 520};
    constexpr std::array horizontal_area{WindowWorkArea{maximum - 100, 0, 720, 520}};
    CHECK_FALSE(resolveWindowPlacement(horizontal_overflow, horizontal_area, minimum_window_size));

    constexpr WindowPlacement vertical_overflow{0, maximum - 100, 720, 520};
    constexpr std::array vertical_area{WindowWorkArea{0, maximum - 100, 720, 520}};
    CHECK_FALSE(resolveWindowPlacement(vertical_overflow, vertical_area, minimum_window_size));
}

} // namespace music_player::m0::test
