#pragma once

#include "m0/domain/ListeningRoom.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace music_player::m0 {

// What a host track looks like in the listening room (ADR-0022, S7.1).
struct RoomTrackDescription final {
    // origin HostTrack, content_sha256 (64 lowercase hex of the file bytes), size_bytes and a summary
    // whose texts sanitizeMetadataText leaves unchanged. id, owner_fingerprint and added_by are left
    // for the host to fill.
    RoomItem item{};
    // The lyrics the host chose for this track (LyricsVariantsStore), valid UTF-8 of at most
    // maximum_room_lyrics_bytes; empty when there are none or they do not fit.
    std::string lyrics_lrc{};
};

// Describes local files for the room. Qt-free port; src/adapters/QtRoomTrackCatalog implements it.
//
// Deviation from the plan's signature (done(std::optional<RoomItem>)): a RoomItem has no room for
// the lyrics the adapter must attach, so done receives a RoomTrackDescription.
class RoomTrackCatalog {
  public:
    virtual ~RoomTrackCatalog() = default;
    // Hashes and reads the file away from the calling thread, then calls done exactly once on the
    // calling thread: nullopt when the file cannot be read. Destroying the catalog drops pending
    // callbacks.
    virtual void describe(std::filesystem::path source,
                          std::function<void(std::optional<RoomTrackDescription>)> done) = 0;
};

// S12.1: checks a file a guest uploaded with the rules of a local import (LocalAudioFilePolicy and the
// metadata probe). The file has no name to go by (the host's upload cache names it by its hash), so the
// container the probe finds stands for the extension. Qt-free port; QtRoomTrackCatalog implements it.
class RoomUploadProbe {
  public:
    virtual ~RoomUploadProbe() = default;
    // Reads the file away from the calling thread, then calls done exactly once on the calling thread:
    // the track's summary (tags passed through sanitizeMetadataText, never a file name), or nullopt when
    // it is not valid audio of a format a local import accepts. Destroying the probe drops pending callbacks.
    virtual void probe(std::filesystem::path file, std::function<void(std::optional<RoomTrackSummary>)> done) = 0;
};

} // namespace music_player::m0
