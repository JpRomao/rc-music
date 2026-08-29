#pragma once

#include "m0/ports/MetadataReader.hpp"

#include <QImage>
#include <QQuickImageProvider>
#include <QString>
#include <QUrl>

#include <memory>
#include <mutex>
#include <optional>

namespace music_player::m0 {

class ArtworkImageProvider;

class ArtworkImageCache final {
  public:
    void beginGeneration(MetadataGeneration generation);
    [[nodiscard]] bool publish(MetadataGeneration generation, const ArtworkThumbnail& artwork);
    [[nodiscard]] QUrl currentSource() const;

  private:
    friend class ArtworkImageProvider;
    [[nodiscard]] std::optional<ArtworkThumbnail> find(const QString& opaque_id) const;
    [[nodiscard]] static bool valid(const ArtworkThumbnail& artwork) noexcept;
    [[nodiscard]] static QString opaqueId(MetadataGeneration generation);

    mutable std::mutex mutex_;
    MetadataGeneration current_generation_{};
    QString current_id_{};
    std::optional<ArtworkThumbnail> artwork_{};
};

class ArtworkImageProvider final : public QQuickImageProvider {
  public:
    explicit ArtworkImageProvider(std::shared_ptr<ArtworkImageCache> cache);

    [[nodiscard]] static QString providerId();
    [[nodiscard]] QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;

  private:
    std::shared_ptr<ArtworkImageCache> cache_;
};

} // namespace music_player::m0
