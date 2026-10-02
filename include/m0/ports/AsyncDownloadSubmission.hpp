#pragma once

#include "m0/ports/DownloadSubmission.hpp"

#include <optional>
#include <string_view>

namespace music_player::m0 {

enum class DownloadAdmissionState { Pending, Completed };

struct DownloadAdmissionResult final {
    DownloadAdmissionState state{DownloadAdmissionState::Completed};
    // Pending has the default NotAvailable/empty value, never a preview ACK.
    DownloadSubmissionResult result{};
};

// begin owns/adjudicates the batch promptly; Pending has no wire response.
// poll uses that session's validated UUID and never drives I/O or execution.
// Ownership and completion live independently of an observing browser session.
class AsyncDownloadSubmission {
  public:
    virtual ~AsyncDownloadSubmission() = default;
    [[nodiscard]] virtual DownloadAdmissionResult begin(const DownloadRequest& request) = 0;
    [[nodiscard]] virtual std::optional<DownloadSubmissionResult> poll(std::string_view request_id) = 0;
};

} // namespace music_player::m0
