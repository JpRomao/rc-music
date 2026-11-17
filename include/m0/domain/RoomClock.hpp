#pragma once

#include "m0/domain/ListeningRoom.hpp"

#include <cstdint>
#include <deque>
#include <optional>

namespace music_player::m0 {

struct ClockSample final {
    std::int64_t sent_local_ms{};
    std::int64_t host_ms{};
    std::int64_t received_local_ms{};
};

// NTP-style host clock estimate from the last room_clock_samples samples.
class RoomClockEstimator final {
  public:
    // Samples with received_local_ms < sent_local_ms are ignored.
    void add(const ClockSample& sample);
    // host - local, from the minimum-RTT sample (most recent wins ties):
    // host_ms - floor((sent + received) / 2); rounding is toward negative infinity.
    [[nodiscard]] std::optional<std::int64_t> offsetMs() const;
    [[nodiscard]] std::optional<std::int64_t> roundTripMs() const;

  private:
    [[nodiscard]] const ClockSample* best() const;
    std::deque<ClockSample> samples_{};
};

// Paused: anchor_position_ms. Playing: anchor_position_ms + (host_now - anchor_host_ms).
// Independent of current_item. Never negative.
[[nodiscard]] std::int64_t expectedPositionMs(const RoomPlayback& playback, std::int64_t host_now_ms) noexcept;

enum class DriftCorrection : std::uint8_t { None, Nudge, Seek };

// Uses |drift|: < nudge_ms -> None; < seek_ms -> Nudge; otherwise Seek.
[[nodiscard]] DriftCorrection classifyDrift(std::int64_t drift_ms, std::int64_t nudge_ms,
                                            std::int64_t seek_ms) noexcept;

} // namespace music_player::m0
