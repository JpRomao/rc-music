#pragma once

#include "LibraryArtworkIndex.hpp"
#include "m0/ports/MetadataExtractor.hpp"

#include <QQuickAsyncImageProvider>
#include <QString>

#include <memory>

namespace music_player::m0 {
// Covers for the library page: `image://libraryart/<record id>`. Embedded art
// is read off the UI thread (two readers at most) and the newest thumbnails
// stay in memory, keyed by record and path, so a relocated file is read again.
// A record without art, unknown or unavailable gets a transparent pixel, which
// leaves the themed placeholder visible.
class LibraryArtworkProvider final : public QQuickAsyncImageProvider {
  public:
    LibraryArtworkProvider(std::shared_ptr<const LibraryArtworkIndex> index,
                           std::shared_ptr<const MetadataExtractor> extractor, int cached_covers = 256);
    ~LibraryArtworkProvider() override;
    [[nodiscard]] static QString providerId();
    [[nodiscard]] static QString sourceBase();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requested_size) override;
    // Shared with in-flight responses, which may outlive a request's caller.
    struct State;

  private:
    std::shared_ptr<State> state_;
};
} // namespace music_player::m0
