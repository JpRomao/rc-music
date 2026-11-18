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
inline constexpr std::size_t maximum_nickname_code_points = 32;
inline constexpr std::size_t maximum_custom_avatar_input_bytes = 2 * 1024 * 1024;
inline constexpr std::uint32_t avatar_pixels = 256;
inline constexpr std::int32_t room_play_lead_ms = 300;
inline constexpr std::uint32_t room_clock_samples = 8;
inline constexpr std::int32_t room_clock_resample_ms = 5000;
inline constexpr std::int32_t room_reconnect_window_ms = 30000;
// The host keeps the playback anchor while the player stays within this distance of it.
inline constexpr std::int64_t room_anchor_tolerance_ms = 100;
// Title, artist and album of a room item, cut at a code point boundary.
inline constexpr std::size_t maximum_room_summary_text_bytes = 256;
// A guest link that sends no RoomHello within this time is closed.
inline constexpr std::int32_t room_hello_timeout_ms = 10000;
inline constexpr std::int32_t minimum_output_offset_ms = -500;
inline constexpr std::int32_t maximum_output_offset_ms = 500;

} // namespace music_player::m0
