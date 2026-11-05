#pragma once

#include "m0/ports/AsyncYoutubeAudioSubmission.hpp"

namespace music_player::m0 {
// Validates the entire separate request before handing ownership to the common
// durable admission port. No tools, network, file access or caller authentication.
class YoutubeAudioAdmission final : public AsyncYoutubeAudioSubmission {
  public:
    explicit YoutubeAudioAdmission(AsyncDownloadSubmission& destination);
    [[nodiscard]] DownloadAdmissionResult begin(const YoutubeAudioRequest& request) override;
    [[nodiscard]] std::optional<DownloadSubmissionResult> poll(std::string_view request_id) override;

  private:
    AsyncDownloadSubmission& destination_;
    bool operating_{};
};
} // namespace music_player::m0
