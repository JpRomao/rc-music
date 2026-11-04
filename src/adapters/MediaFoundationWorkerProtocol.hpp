#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace music_player::m0 {
enum class MediaFoundationWorkerMode : std::uint8_t { Convert = 1, Validate = 2 };

struct MediaFoundationWorkerRequest final {
    MediaFoundationWorkerMode mode{MediaFoundationWorkerMode::Convert};
    std::uint64_t input_handle{0};
    std::uint64_t max_input_bytes{512ULL * 1024 * 1024};
    std::uint64_t max_output_bytes{512ULL * 1024 * 1024};
    std::uint64_t max_duration_us{24ULL * 60 * 60 * 1'000'000};
};

enum class MediaFoundationWorkerExit : int {
    Success = 0,
    InvalidRequest = 2,
    Input = 3,
    NoAudio = 4,
    Unsupported = 5,
    LimitExceeded = 6,
    Conversion = 7,
    Validation = 8,
    Output = 9,
    Unavailable = 10
};

inline constexpr std::size_t media_foundation_worker_max_request_bytes = 42;

// Private, exact-EOF wire format: RCMMFWRQ (8 bytes), version 2 (uint8),
// mode (uint8), three uint64 LE limits, then the uint64 LE value of one
// parent-selected, explicitly inherited read capability. No path or URL exists
// in this protocol. The codec borrows this handle; the caller retains ownership.
[[nodiscard]] bool validMediaFoundationWorkerRequest(const MediaFoundationWorkerRequest& request);
[[nodiscard]] std::vector<std::uint8_t> encodeMediaFoundationWorkerRequest(const MediaFoundationWorkerRequest& request);
[[nodiscard]] std::optional<MediaFoundationWorkerRequest>
decodeMediaFoundationWorkerRequest(std::span<const std::uint8_t> bytes);
} // namespace music_player::m0
