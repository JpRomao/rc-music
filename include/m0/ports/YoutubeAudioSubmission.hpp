#pragma once

#include "m0/ports/DownloadSubmission.hpp"
#include "m0/ports/YoutubeAudioItem.hpp"

#include <string>
#include <vector>

namespace music_player::m0 {
struct YoutubeAudioRequest final {
    std::string request_id{};
    std::vector<YoutubeAudioItem> items{};
    bool operator==(const YoutubeAudioRequest&) const = default;
};

// Separate admission capability for an authenticated YouTube client. The
// request is data, never caller authorization or arbitrary process arguments.
// A future trusted host must authenticate its caller before invoking this port.
// Admission returns promptly; it does not imply a completed download/import.
// This contract neither activates a runtime nor implements networking.
class YoutubeAudioSubmission {
  public:
    virtual ~YoutubeAudioSubmission() = default;
    [[nodiscard]] virtual DownloadSubmissionResult submit(const YoutubeAudioRequest& request) = 0;
};
} // namespace music_player::m0
