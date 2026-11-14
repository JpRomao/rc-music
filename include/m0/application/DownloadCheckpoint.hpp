#pragma once

#include "m0/ports/DownloadExecution.hpp"

#include <vector>

namespace music_player::m0 {

enum class DownloadJobState {
    Queued,
    Downloading,
    Converting,
    Publishing,
    Cancelling,
    Completed,
    Cancelled,
    Failed,
    ImportFailed,
    Interrupted,
    Importing,
    Imported
};

struct DownloadJobSnapshot final {
    std::string job_id{};
    std::string title{};
    DownloadJobState state{DownloadJobState::Queued};
    std::uint64_t completed_units{0};
    std::optional<std::uint64_t> total_units{};
    std::filesystem::path completed_mp3{};
    DownloadFailure error{DownloadFailure::None};
    bool operator==(const DownloadJobSnapshot&) const = default;
};

struct DownloadCheckpointReceipt final {
    std::string request_id{};
    // SHA-256 of a versioned, length-prefixed typed batch, never a raw URL.
    std::string fingerprint{};
    std::vector<std::string> job_ids{};
    // Original logical request bytes preserve admission accounting after restart.
    std::size_t retained_request_bytes{0};
    bool operator==(const DownloadCheckpointReceipt&) const = default;
};

// Retain the accepted response and fingerprint after the job rows are archived.
struct DownloadArchivedRequest final {
    std::string request_id{};
    std::string fingerprint{};
    std::vector<std::string> job_ids{};
    bool operator==(const DownloadArchivedRequest&) const = default;
};

struct DownloadResumeEntry final {
    std::string job_id{};
    DownloadItem item{};
    bool operator==(const DownloadResumeEntry&) const = default;
};

// Private bounded recovery evidence. A final path is only a candidate requiring
// separate ownership and media review. Sources belong to eligible queued rows
// and require private storage. No resolved YouTube URL or browser credentials
// are retained.
struct DownloadCheckpoint final {
    std::vector<DownloadCheckpointReceipt> receipts{};
    std::vector<DownloadJobSnapshot> jobs{};
    std::vector<DownloadArchivedRequest> archived_receipts{};
    std::vector<DownloadResumeEntry> resumable_jobs{};
    bool operator==(const DownloadCheckpoint&) const = default;
};

inline constexpr std::size_t download_checkpoint_max_jobs = 128;
inline constexpr std::size_t download_checkpoint_max_receipts = 64;
inline constexpr std::size_t download_checkpoint_max_archived_receipts = 256;
inline constexpr std::size_t download_checkpoint_max_bytes = 1024 * 1024;

[[nodiscard]] bool validDownloadCheckpoint(const DownloadCheckpoint& checkpoint);

} // namespace music_player::m0
