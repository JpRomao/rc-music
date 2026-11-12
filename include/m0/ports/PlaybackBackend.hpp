#pragma once

#include "m0/domain/PlaybackTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>

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
};

} // namespace music_player::m0
