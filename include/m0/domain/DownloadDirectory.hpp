#pragma once

#include <string_view>

namespace music_player::m0 {

// The persisted form is a drive-qualified local Windows directory using '/'.
// Filesystem identity and access checks belong to the Windows worker.
[[nodiscard]] inline bool validDownloadDirectory(const std::string_view path) noexcept {
    constexpr std::size_t maximum_bytes = 1024;
    if (path.size() < 4 || path.size() > maximum_bytes || path[1] != ':' || path[2] != '/' ||
        !((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')))
        return false;

    auto remaining = path.substr(3);
    while (true) {
        const auto separator = remaining.find('/');
        const auto component = remaining.substr(0, separator);
        if (component.empty() || component.back() == '.' || component.back() == ' ')
            return false;
        for (const auto unit : component) {
            const auto byte = static_cast<unsigned char>(unit);
            if (byte < 0x20 || byte == 0x7f || std::string_view{"\\:<>\"|?*"}.find(unit) != std::string_view::npos)
                return false;
        }
        if (separator == std::string_view::npos)
            return true;
        remaining.remove_prefix(separator + 1);
    }
}

} // namespace music_player::m0
