#pragma once

#include "m0/domain/RoomMessages.hpp"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace music_player::m0 {

// Wire codec of the listening room (ADR-0022). decode* is a security boundary: the bytes come from a
// remote peer. Both directions apply the same validation, so encode* returns an empty QByteArray for any
// message the peer's decode* would drop: a sender never emits what a peer rejects.
//
// Control messages: compact JSON object {"v":1,"t":"<type>", ...fields}, at most
// maximum_room_control_message_bytes (checked before parsing). Like BrowserMediaSnapshotCodec, every object
// must carry exactly its keys: unknown, missing or duplicate keys, wrong JSON types, non-object roots and
// nesting deeper than the format needs are rejected. Types and fields (snake_case):
//   hello{protocol_version, nickname, avatar}          join_result{decision}
//   state{snapshot{version, members[], queue[], playback}, permissions}
//   intent{request_id, intent{kind, ...}}               intent_rejected{request_id, reason}
//   clock_ping{sent_local_ms}                           clock_pong{sent_local_ms, host_ms}
//   catalog{playlists[{id, name, entries[{id, summary}]}]}
//   file_request{sha256, offset}                        file_offer{sha256, size_bytes, transfer_id}
//   lyrics{item_id, lrc}                                bye{reason}            drift{drift_ms}
//   avatar{kind:"bundled"|"custom", index, sha256}      member{fingerprint, nickname, avatar, state}
//   item{id, origin, owner, sha256, size_bytes, summary, youtube_id, added_by}
//   summary{title, artist, album, duration_ms}          playback{current (null|id), playing, anchor_host_ms,
//                                                                anchor_position_ms}
// RoomIntent carries only the fields of its kind: seek{position_ms}, add_items{entries[], uploaded (null|item)},
// add_playlist{playlist_id}, remove_item{item_id}, move_item{item_id, destination}; the others only {kind}.
// Fields irrelevant to the kind are not transmitted and decode as defaults.
//
// Validation (controller rulings for S4.1):
//   - integers: integral JSON numbers with |x| <= 2^53 (no strings, no fractions); unsigned fields >= 0;
//     u32 fields <= UINT32_MAX; durations/positions >= 0; destination <= maximum_room_queue_items;
//     drift_ms within +-60000.
//   - enums travel as snake_case names; an unknown name is rejected. Permissions are an integer bitmask below
//     2^room_permission_count.
//   - nicknames must already be canonical (canonicalNickname leaves them unchanged); avatars follow
//     isValidAvatarChoice; titles, artist, album, playlist names (non-empty) and added_by must be left
//     unchanged by sanitizeMetadataText (empty allowed except for playlist names).
//   - lrc: valid UTF-8, at most maximum_room_lyrics_bytes, newlines allowed.
//   - fingerprints: 1..128 bytes of printable ASCII 0x21-0x7E (an item's owner may be empty).
//   - hashes: 64 lowercase hex. Host/guest tracks require one; a YouTube item has an empty hash or a valid one.
//   - youtube_id: exactly 11 of [A-Za-z0-9_-] for YoutubeVideo items, empty for every other origin.
//   - queue <= maximum_room_queue_items; add_items has either 1..maximum_room_queue_items catalog ids or one
//     uploaded item (same item validation as the queue), never both or neither. Members have no separate cap.
//   - protocol_version in hello is any u32: a mismatch is answered by the session (bye version_mismatch).
[[nodiscard]] QByteArray encodeRoomControl(const RoomControlMessage& message);
[[nodiscard]] std::optional<RoomControlMessage> decodeRoomControl(QByteArrayView bytes);

// Binary media frame: u32 transfer_id (LE) | u64 offset (LE) | payload of 0..room_file_chunk_bytes.
// encodeFileChunk returns empty for an oversized payload; decodeFileChunk rejects a header shorter than
// 12 bytes or an oversized payload.
[[nodiscard]] QByteArray encodeFileChunk(const RoomFileChunk& chunk);
[[nodiscard]] std::optional<RoomFileChunk> decodeFileChunk(QByteArrayView bytes);

} // namespace music_player::m0
