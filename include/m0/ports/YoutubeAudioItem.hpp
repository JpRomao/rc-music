#pragma once

#include <string>

namespace music_player::m0 {

// Acquisition data only. Its presence never authorizes a remote source,
// process launch, caller identity or network capability.
struct YoutubeAudioItem final {
    std::string video_id{}, title{}, artist{}, lyrics_page{};
    bool fetch_lyrics{false};
    bool operator==(const YoutubeAudioItem&) const = default;
};

} // namespace music_player::m0
