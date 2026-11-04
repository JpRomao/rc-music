#pragma once

#include "m0/ports/EmbeddedBrowserWindow.hpp"

#include <memory>

namespace music_player::m0 {
// Inert factory. No HWND, profile, COM apartment or engine until show(valid_owner).
[[nodiscard]] std::unique_ptr<EmbeddedBrowserWindow> makeWindowsWebView2Browser();
} // namespace music_player::m0
