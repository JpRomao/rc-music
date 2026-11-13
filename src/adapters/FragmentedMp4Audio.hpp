#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace music_player::m0 {
// Pure, bounded inspection. The callback reads the already selected local
// capability; no filename, URL, codec, process or writable media is acquired.
using Mp4AudioReadAt = std::function<bool(std::uint64_t, std::span<std::uint8_t>)>;
enum class Mp4AudioRemuxStatus { Passthrough, Ready, Invalid, Unsupported, LimitExceeded };
struct Mp4AudioReadRange final {
    std::uint64_t source_offset{0};
    std::uint64_t output_offset{0};
    std::uint32_t length{0};
};
struct Mp4AudioEdit final {
    std::int64_t media_time{0}; // -1 is an empty presentation edit (silence).
    std::uint64_t movie_duration{0};
};
struct Mp4AudioRemuxLimits final {
    std::uint64_t max_metadata_bytes{8 * 1024 * 1024};
    std::uint32_t max_samples{1'000'000};
    std::uint32_t max_boxes{100'000};
    std::uint32_t max_fragments{50'000};
};
struct Mp4AudioRemuxPlan final {
    Mp4AudioRemuxStatus status{Mp4AudioRemuxStatus::Passthrough};
    // Canonical ftyp/moov/mdat header. Packet bytes are NOT buffered or changed.
    // Original edit timing is represented explicitly below and materialized by
    // the PCM stage, not silently discarded or delegated twice to a reader.
    std::vector<std::uint8_t> header{};
    std::vector<Mp4AudioReadRange> packets{};
    std::uint64_t normalized_size{0};
    std::uint32_t movie_timescale{0};
    std::uint32_t media_timescale{0};
    std::vector<Mp4AudioEdit> edits{};
};
[[nodiscard]] Mp4AudioRemuxPlan inspectFragmentedMp4Audio(std::uint64_t size, const Mp4AudioReadAt& read,
                                                          Mp4AudioRemuxLimits limits = {});
} // namespace music_player::m0
