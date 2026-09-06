#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace music_player::m0 {

struct WindowPlacement final {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    [[nodiscard]] bool operator==(const WindowPlacement&) const = default;
};

struct WindowWorkArea final {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    [[nodiscard]] bool operator==(const WindowWorkArea&) const = default;
};

struct WindowSize final {
    int width{0};
    int height{0};

    [[nodiscard]] bool operator==(const WindowSize&) const = default;
};

struct ResolvedWindowPlacement final {
    WindowPlacement placement{};
    std::size_t screen_index{0};

    [[nodiscard]] bool operator==(const ResolvedWindowPlacement&) const = default;
};

[[nodiscard]] std::optional<ResolvedWindowPlacement>
resolveWindowPlacement(WindowPlacement saved, std::span<const WindowWorkArea> work_areas, WindowSize minimum) noexcept;

} // namespace music_player::m0
