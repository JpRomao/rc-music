#pragma once

#include "m0/domain/PlaybackTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace music_player::m0 {

using PlaybackEventCallback = std::function<void(const PlaybackEvent&)>;

class PlaybackBackend {
  public:
    virtual ~PlaybackBackend() = default;

    [[nodiscard]] virtual const PlaybackSnapshot& snapshot() const noexcept = 0;
    // True once removal of the source volume for this generation is known, even if its event is deferred.
    [[nodiscard]] virtual bool sourceVolumeRemovalObserved(std::uint64_t) const noexcept { return false; }
    virtual void setEventCallback(PlaybackEventCallback callback) = 0;
    virtual PlaybackCommandResult load(const std::filesystem::path& source_path) = 0;
    virtual PlaybackCommandResult play() = 0;
    virtual PlaybackCommandResult pause() = 0;
    virtual PlaybackCommandResult seekTo(std::int64_t position_ms) = 0;
    virtual PlaybackCommandResult setVolume(double volume) = 0;
    virtual PlaybackCommandResult setMuted(bool muted) = 0;
    virtual PlaybackCommandResult unload() = 0;
    virtual PlaybackCommandResult shutdown() = 0;
    // Playback speed, 1.0 = normal. A rate other than 1.0 must change the pitch with the speed: keeping
    // the pitch delays the sound and adds a gap at each change (S1), so a backend that cannot turn that
    // off answers Unsupported. The listening room uses it for drift correction (S9.1).
    virtual PlaybackCommandResult setPlaybackRate(double) { return PlaybackCommandResult::Unsupported; }
    // When the backend last saw the position of its current source change, in milliseconds of
    // std::chrono::steady_clock since its epoch; nullopt when unknown. Lets the room discard a position
    // that a stalled main thread kept from being updated.
    [[nodiscard]] virtual std::optional<std::int64_t> positionUpdatedAtMs() const noexcept { return std::nullopt; }
};

} // namespace music_player::m0
