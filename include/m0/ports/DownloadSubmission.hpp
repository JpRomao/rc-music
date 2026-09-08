#pragma once

#include <string>
#include <vector>

namespace music_player::m0 {

enum class DownloadMediaKind { Audio, Video };

struct DownloadItem final {
    std::string url{};
    std::string title{};
    DownloadMediaKind kind{DownloadMediaKind::Audio};
    bool operator==(const DownloadItem&) const = default;
};

struct DownloadRequest final {
    std::string request_id{};
    std::vector<DownloadItem> items{};
    bool operator==(const DownloadRequest&) const = default;
};

enum class DownloadSubmissionError { None, InvalidRequest, UnsupportedSource, Busy, NotAvailable, LimitExceeded };

struct DownloadSubmissionResult final {
    DownloadSubmissionError error{DownloadSubmissionError::NotAvailable};
    std::vector<std::string> job_ids{};
};

// Admission only: implementations must not download or convert synchronously.
// A successful result owns the complete batch independently of the browser connection.
// The application service must deduplicate request IDs across connections.
class DownloadSubmission {
  public:
    virtual ~DownloadSubmission() = default;
    [[nodiscard]] virtual DownloadSubmissionResult submit(const DownloadRequest& request) = 0;
};

} // namespace music_player::m0
