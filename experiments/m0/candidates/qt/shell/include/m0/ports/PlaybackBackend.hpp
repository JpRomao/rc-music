#pragma once

#include <cstdint>

namespace music_player::m0 {

class PlaybackBackend {
  public:
    virtual ~PlaybackBackend() = default;

    virtual void seekTo(std::int64_t position_ms) = 0;
    virtual void setVolume(double volume) = 0;
    virtual void setMuted(bool muted) = 0;
};

} // namespace music_player::m0
