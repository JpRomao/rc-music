#pragma once

#include <cstddef>
#include <cstdint>

namespace music_player::m0 {

inline constexpr std::uint32_t room_protocol_version = 1;
inline constexpr std::uint32_t minimum_room_listeners = 1;
inline constexpr std::uint32_t maximum_room_listeners = 10;
inline constexpr std::uint32_t default_room_listeners = 5;
inline constexpr std::size_t maximum_room_queue_items = 500;
inline constexpr std::size_t maximum_room_control_message_bytes = 256 * 1024;
inline constexpr std::size_t room_file_chunk_bytes = 64 * 1024;
inline constexpr std::uint64_t maximum_guest_upload_bytes = 50ull * 1024 * 1024;
inline constexpr std::uint64_t room_cache_quota_bytes = 1024ull * 1024 * 1024;
inline constexpr std::size_t maximum_room_lyrics_bytes = 64 * 1024;
inline constexpr std::size_t room_prefetch_items = 3;
// A guest asks again for a file whose transfer failed at most this many times while its item stays in
// the window (a new window entry starts over); then the item stays unavailable.
inline constexpr int room_file_retries = 2;
inline constexpr std::size_t maximum_nickname_code_points = 32;
inline constexpr std::size_t maximum_custom_avatar_input_bytes = 2 * 1024 * 1024;
inline constexpr std::uint32_t avatar_pixels = 256;
inline constexpr std::int32_t room_play_lead_ms = 300;
inline constexpr std::uint32_t room_clock_samples = 8;
inline constexpr std::int32_t room_clock_resample_ms = 5000;
inline constexpr std::int32_t room_reconnect_window_ms = 30000;
// A guest inside the reconnect window waits this long after a failed attempt, doubling up to the maximum.
inline constexpr std::int32_t room_reconnect_retry_initial_ms = 1000;
inline constexpr std::int32_t room_reconnect_retry_maximum_ms = 8000;
// The host keeps the playback anchor while the player stays within this distance of it. It equals
// room_drift_seek_ms: a host that drifts past it re-anchors, which a guest corrects with the same seek.
inline constexpr std::int64_t room_anchor_tolerance_ms = 100;
// A guest's drift correction (S1 "Limiares escolhidos", S9.1). Above room_drift_nudge_ms the guest plays
// at 1 +- room_drift_nudge_rate_delta until the drift falls to half of it or crosses zero, for at most
// room_drift_nudge_max_ms; above room_drift_seek_ms on two evaluations in a row it seeks. An evaluation
// whose position is older than room_drift_stale_position_ms (a stalled main thread) is discarded.
inline constexpr std::int64_t room_drift_nudge_ms = 20;
inline constexpr std::int64_t room_drift_seek_ms = 100;
inline constexpr double room_drift_nudge_rate_delta = 0.01;
inline constexpr std::int64_t room_drift_nudge_max_ms = 10000;
inline constexpr std::int64_t room_drift_stale_position_ms = 150;
inline constexpr std::int32_t room_drift_check_ms = 1000;
// Sync difference reports (S9.2). A guest sends the follower's latest drift with every clock ping,
// clamped to +-room_drift_report_limit_ms; the host ignores a report beyond it (an absurd value is no
// measurement: past the reconnect window the person is gone, or its clock is wrong). Same value as
// room_reconnect_window_ms; the codec accepts a wider +-60 s.
inline constexpr std::int64_t room_drift_report_limit_ms = 30000;
// While the backend is still loading a room file, the guest looks again after this long.
inline constexpr std::int32_t room_follow_retry_ms = 50;
// Title, artist and album of a room item, cut at a code point boundary.
inline constexpr std::size_t maximum_room_summary_text_bytes = 256;
// A guest link that sends no RoomHello within this time is closed.
inline constexpr std::int32_t room_hello_timeout_ms = 10000;
inline constexpr std::int32_t minimum_output_offset_ms = -500;
inline constexpr std::int32_t maximum_output_offset_ms = 500;

} // namespace music_player::m0
