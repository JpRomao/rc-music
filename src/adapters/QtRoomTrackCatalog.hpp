#pragma once

#include "m0/ports/LyricsVariantsStore.hpp"
#include "m0/ports/MetadataExtractor.hpp"
#include "m0/ports/RoomTrackCatalog.hpp"

#include <QObject>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {

// RoomTrackCatalog on QtConcurrent (the app's background file-work mechanism, as in LyricsBinding and
// LibraryBinding). Each describe() runs on the global thread pool: SHA-256 of the file bytes
// (QCryptographicHash), tags through the injected MetadataExtractor and the lyrics through the
// injected LyricsVariantsStore; the result comes back through a QFutureWatcher on the thread that owns
// the catalog (it must run a Qt event loop).
//
// Privacy (ADR-0022): the summary carries only tags (title, artist, album, duration), each passed
// through sanitizeMetadataText; with no title tag the title stays empty, never the file name. Unreadable
// tags still describe the file with an empty summary; an unreadable file gives nullopt.
//
// Lyrics follow LyricsBinding's choice: Hidden and Source give none (Source lyrics live in the audio
// file, not in the variants store); a remembered Variant gives that variant; Automatic or a missing
// variant gives the first one. Lyrics larger than maximum_room_lyrics_bytes or not valid UTF-8 are
// left out, never truncated.
//
// Destroying the catalog drops pending callbacks; running jobs finish in the pool holding only their
// own shared dependencies.
class QtRoomTrackCatalog final : public RoomTrackCatalog {
  public:
    // lyrics may be null (no lyrics are attached).
    QtRoomTrackCatalog(std::shared_ptr<const MetadataExtractor> metadata, std::shared_ptr<LyricsVariantsStore> lyrics);
    ~QtRoomTrackCatalog() override;
    QtRoomTrackCatalog(const QtRoomTrackCatalog&) = delete;
    QtRoomTrackCatalog& operator=(const QtRoomTrackCatalog&) = delete;

    void describe(std::filesystem::path source, std::function<void(std::optional<RoomTrackDescription>)> done) override;

  private:
    std::shared_ptr<const MetadataExtractor> metadata_;
    std::shared_ptr<LyricsVariantsStore> lyrics_;
    QObject context_;
};

} // namespace music_player::m0
