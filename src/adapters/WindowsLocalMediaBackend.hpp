#pragma once

namespace music_player::m0 {
class WindowsMediaFoundationLaunch;
// Trusted C++ composition only. Media Foundation never falls back to a legacy
// executable when the selected worker is missing, unavailable or rejects media.
enum class WindowsLocalMediaBackend { FfmpegLegacy, MediaFoundation };
} // namespace music_player::m0
