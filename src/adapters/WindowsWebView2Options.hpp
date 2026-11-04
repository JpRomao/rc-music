#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// SDK interfaces require COM declarations before WebView2, ahead of Qt headers.
// clang-format off
#include <Windows.h>
#include <objbase.h>
#include <wrl/client.h>
#include <WebView2.h>
// clang-format on

namespace music_player::m0 {
// Inert WRL object: no apartment, environment, profile, engine or host changes.
// Empty means an option/interface could not be configured; no weaker fallback.
// The stable search mask does not authenticate or override host Registry/policy.
[[nodiscard]] Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions> makeWindowsWebView2Options() noexcept;
} // namespace music_player::m0
