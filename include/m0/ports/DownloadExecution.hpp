#pragma once

#include "m0/ports/DownloadSubmission.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {

enum class DownloadPhase { Downloading, Converting, Publishing };
enum class DownloadFailure {
    None,
    Unavailable,
    Network,
    Expired,
    Restricted,
    UnsupportedMedia,
    NoAudio,
    LimitExceeded,
    Conversion,
    Publication,
    Import,
    Timeout,
    InvalidResult
};
enum class DownloadExecutionState { Running, Published, Failed, Cancelled };

struct DownloadExecutionUpdate final {
    std::string job_id{};
    DownloadExecutionState state{DownloadExecutionState::Running};
    DownloadPhase phase{DownloadPhase::Downloading};
    std::uint64_t completed_units{0};
    std::optional<std::uint64_t> total_units{};
    std::filesystem::path published_mp3{};
    DownloadFailure error{DownloadFailure::None};
};

// One active job; every method must return promptly. Network/decoding belongs to a worker.
// The adapter owns quotas, TLS/DNS/redirect checks, MP3 validation, temporary-file cleanup
// and publication without overwrite. Published means a validated final file, never a partial.
// Terminal snapshots remain observable until the next successful start. cancel is a request,
// not proof of stopped I/O; the owner must keep polling until a terminal acknowledgement.
// start false guarantees refusal with no active work/resources retained for that request;
// an exception is uncertain and must support cancellation/draining by the supplied job ID.
class DownloadExecution {
  public:
    virtual ~DownloadExecution() = default;
    [[nodiscard]] virtual bool start(const std::string& job_id, const DownloadItem& item) = 0;
    virtual void cancel(std::string_view job_id) = 0;
    [[nodiscard]] virtual DownloadExecutionUpdate poll(std::string_view job_id) = 0;
};

// Idempotent handoff keyed by job_id to the existing local import pipeline; never autoplay,
// clear the local queue, or accept an unfinished/remote path. False permits explicit retry.
class DownloadImport {
  public:
    virtual ~DownloadImport() = default;
    [[nodiscard]] virtual bool accept(const std::string& job_id, const std::filesystem::path& mp3_path) = 0;
};

} // namespace music_player::m0
