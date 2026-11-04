#pragma once

#include "adapters/MediaFoundationWorkerProtocol.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace music_player::m0 {
struct MediaFoundationCodecCallbacks final {
    std::function<bool(std::span<const std::uint8_t>)> write{};
    std::function<bool(std::uint64_t)> progress{};
};

// Blocking native codec. Call only inside the owned converter subprocess or a
// trusted-fixture test worker, never on the player/UI thread. Borrows the
// parent-selected local read handle without closing it or reopening a name.
// This is neither a sandbox nor permission to decode remote media.
[[nodiscard]] MediaFoundationWorkerExit runWindowsMediaFoundationCodec(const MediaFoundationWorkerRequest& request,
                                                                       const MediaFoundationCodecCallbacks& callbacks);
} // namespace music_player::m0
