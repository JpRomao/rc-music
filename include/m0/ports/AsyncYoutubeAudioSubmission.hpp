#pragma once

#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "m0/ports/YoutubeAudioSubmission.hpp"

namespace music_player::m0 {
// Separate trusted YouTube admission. Pending has no wire acknowledgement;
// observer disconnection never cancels or retries the owner's admitted work.
class AsyncYoutubeAudioSubmission {
  public:
    virtual ~AsyncYoutubeAudioSubmission() = default;
    [[nodiscard]] virtual DownloadAdmissionResult begin(const YoutubeAudioRequest& request) = 0;
    [[nodiscard]] virtual std::optional<DownloadSubmissionResult> poll(std::string_view request_id) = 0;
};
} // namespace music_player::m0
