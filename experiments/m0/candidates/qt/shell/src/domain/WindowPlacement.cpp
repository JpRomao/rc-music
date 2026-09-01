#include "m0/domain/WindowPlacement.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace music_player::m0 {
namespace {

struct WindowBounds final {
    std::int64_t left{0};
    std::int64_t top{0};
    std::int64_t right{0};
    std::int64_t bottom{0};
};

[[nodiscard]] std::optional<WindowBounds> checkedBounds(const int x, const int y, const int width,
                                                        const int height) noexcept {
    if (width <= 0 || height <= 0)
        return std::nullopt;

    const auto left = static_cast<std::int64_t>(x);
    const auto top = static_cast<std::int64_t>(y);
    const auto right = left + static_cast<std::int64_t>(width);
    const auto bottom = top + static_cast<std::int64_t>(height);
    constexpr auto maximum_coordinate = static_cast<std::int64_t>(std::numeric_limits<int>::max());
    if (right > maximum_coordinate || bottom > maximum_coordinate)
        return std::nullopt;

    return WindowBounds{left, top, right, bottom};
}

} // namespace

std::optional<ResolvedWindowPlacement> resolveWindowPlacement(const WindowPlacement saved,
                                                              const std::span<const WindowWorkArea> work_areas,
                                                              const WindowSize minimum) noexcept {
    if (work_areas.empty() || saved.width <= 0 || saved.height <= 0 || minimum.width <= 0 || minimum.height <= 0)
        return std::nullopt;

    const WindowPlacement normalized{saved.x, saved.y, std::max(saved.width, minimum.width),
                                     std::max(saved.height, minimum.height)};
    const auto placement_bounds = checkedBounds(normalized.x, normalized.y, normalized.width, normalized.height);
    if (!placement_bounds)
        return std::nullopt;

    for (std::size_t index = 0; index < work_areas.size(); ++index) {
        const auto& work_area = work_areas[index];
        const auto work_area_bounds = checkedBounds(work_area.x, work_area.y, work_area.width, work_area.height);
        if (!work_area_bounds)
            continue;

        if (placement_bounds->left >= work_area_bounds->left && placement_bounds->top >= work_area_bounds->top &&
            placement_bounds->right <= work_area_bounds->right &&
            placement_bounds->bottom <= work_area_bounds->bottom) {
            return ResolvedWindowPlacement{normalized, index};
        }
    }

    return std::nullopt;
}

} // namespace music_player::m0
