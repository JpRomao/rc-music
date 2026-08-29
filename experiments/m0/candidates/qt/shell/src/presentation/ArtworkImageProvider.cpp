#include "presentation/ArtworkImageProvider.hpp"

#include "m0/domain/ArtworkLimits.hpp"

#include <QByteArray>
#include <QCryptographicHash>

#include <limits>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

using PixelStorage = std::shared_ptr<const std::vector<std::uint8_t>>;

void releasePixelStorage(void* storage) { delete static_cast<PixelStorage*>(storage); }

} // namespace

void ArtworkImageCache::beginGeneration(const MetadataGeneration generation) {
    const std::scoped_lock lock{mutex_};
    current_generation_ = generation;
    current_id_ = generation.value == 0 ? QString{} : opaqueId(generation);
    artwork_.reset();
}

bool ArtworkImageCache::publish(const MetadataGeneration generation, const ArtworkThumbnail& artwork) {
    if (!valid(artwork))
        return false;
    const std::scoped_lock lock{mutex_};
    if (generation.value == 0 || generation != current_generation_)
        return false;
    artwork_ = artwork;
    return true;
}

QUrl ArtworkImageCache::currentSource() const {
    const std::scoped_lock lock{mutex_};
    if (!artwork_.has_value() || current_id_.isEmpty())
        return {};
    return QUrl{QStringLiteral("image://") + ArtworkImageProvider::providerId() + QLatin1Char('/') + current_id_};
}

std::optional<ArtworkThumbnail> ArtworkImageCache::find(const QString& opaque_id) const {
    const std::scoped_lock lock{mutex_};
    if (!artwork_.has_value() || opaque_id != current_id_)
        return std::nullopt;
    return artwork_;
}

bool ArtworkImageCache::valid(const ArtworkThumbnail& artwork) noexcept {
    if (artwork.width_pixels == 0 || artwork.height_pixels == 0 || artwork.rgba8888 == nullptr ||
        artwork.width_pixels > ArtworkLimits::thumbnail_max_axis_pixels ||
        artwork.height_pixels > ArtworkLimits::thumbnail_max_axis_pixels ||
        artwork.width_pixels > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4) ||
        artwork.height_pixels > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const auto width = static_cast<std::size_t>(artwork.width_pixels);
    const auto height = static_cast<std::size_t>(artwork.height_pixels);
    if (width > std::numeric_limits<std::size_t>::max() / height)
        return false;
    const auto pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4)
        return false;
    return artwork.rgba8888->size() == pixels * 4;
}

QString ArtworkImageCache::opaqueId(const MetadataGeneration generation) {
    QByteArray key = QByteArrayLiteral("music-player-artwork-generation-v1:");
    key.append(QByteArray::number(generation.value));
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(24));
}

ArtworkImageProvider::ArtworkImageProvider(std::shared_ptr<ArtworkImageCache> cache)
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading),
      cache_(std::move(cache)) {
    if (cache_ == nullptr)
        throw std::invalid_argument{"artwork image cache is required"};
}

QString ArtworkImageProvider::providerId() { return QStringLiteral("embedded-artwork"); }

QImage ArtworkImageProvider::requestImage(const QString& id, QSize* size, const QSize&) {
    const auto artwork = cache_->find(id);
    if (!artwork.has_value()) {
        if (size != nullptr)
            *size = {};
        return {};
    }

    auto* retained_pixels = new PixelStorage{artwork->rgba8888};
    QImage image{artwork->rgba8888->data(),
                 static_cast<int>(artwork->width_pixels),
                 static_cast<int>(artwork->height_pixels),
                 static_cast<int>(artwork->width_pixels * 4),
                 QImage::Format_RGBA8888,
                 releasePixelStorage,
                 retained_pixels};
    if (size != nullptr)
        *size = image.size();
    return image;
}

} // namespace music_player::m0
