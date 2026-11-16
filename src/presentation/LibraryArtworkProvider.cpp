#include "LibraryArtworkProvider.hpp"

#include <QCache>
#include <QImage>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QThreadPool>

#include <mutex>
#include <variant>

namespace music_player::m0 {
namespace {
QImage transparentPixel() {
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    return image;
}
std::optional<std::uint64_t> recordId(const QString& text) {
    bool ok = false;
    const auto value = text.toULongLong(&ok);
    if (!ok || value == 0 || text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('0')))
        return std::nullopt;
    return value;
}
QImage thumbnailImage(const ArtworkThumbnail& artwork) {
    const auto width = static_cast<int>(artwork.width_pixels), height = static_cast<int>(artwork.height_pixels);
    if (!artwork.rgba8888 || width <= 0 || height <= 0 ||
        artwork.rgba8888->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U)
        return {};
    // Deep copy: the thumbnail buffer belongs to the extractor result.
    return QImage(artwork.rgba8888->data(), width, height, width * 4, QImage::Format_RGBA8888).copy();
}
} // namespace

struct LibraryArtworkProvider::State final {
    std::shared_ptr<const LibraryArtworkIndex> index;
    std::shared_ptr<const MetadataExtractor> extractor;
    std::mutex mutex;
    QCache<QString, QImage> covers;
    QThreadPool pool;

    std::optional<QImage> cached(const QString& key) {
        std::lock_guard lock(mutex);
        if (const auto* image = covers.object(key))
            return *image;
        return std::nullopt;
    }
    void remember(const QString& key, const QImage& image) {
        std::lock_guard lock(mutex);
        covers.insert(key, new QImage(image));
    }
};

namespace {
class CoverResponse final : public QQuickImageResponse, public QRunnable {
  public:
    CoverResponse(std::shared_ptr<LibraryArtworkProvider::State> state, QString id)
        : state_(std::move(state)), id_(std::move(id)) {
        setAutoDelete(false);
    }
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    void run() override {
        image_ = cover();
        emit finished();
    }

  private:
    QImage cover() {
        const auto id = recordId(id_);
        const auto path = id && state_->index ? state_->index->find(*id) : std::nullopt;
        if (!path || !state_->extractor)
            return transparentPixel();
        const auto key = id_ + QLatin1Char('\n') + QString::fromStdWString(path->wstring());
        if (auto image = state_->cached(key))
            return *image;
        QImage image;
        try {
            const auto result = state_->extractor->read(*path);
            if (const auto* metadata = std::get_if<TrackMetadata>(&result); metadata && metadata->artwork)
                image = thumbnailImage(*metadata->artwork);
        } catch (...) {
            image = {};
        }
        if (image.isNull())
            image = transparentPixel();
        state_->remember(key, image);
        return image;
    }

    std::shared_ptr<LibraryArtworkProvider::State> state_;
    QString id_;
    QImage image_;
};
} // namespace

LibraryArtworkProvider::LibraryArtworkProvider(std::shared_ptr<const LibraryArtworkIndex> index,
                                               std::shared_ptr<const MetadataExtractor> extractor,
                                               const int cached_covers)
    : state_(std::make_shared<State>()) {
    state_->index = std::move(index);
    state_->extractor = std::move(extractor);
    state_->covers.setMaxCost(std::max(1, cached_covers));
    state_->pool.setMaxThreadCount(2);
}

LibraryArtworkProvider::~LibraryArtworkProvider() { state_->pool.waitForDone(); }

QString LibraryArtworkProvider::providerId() { return QStringLiteral("libraryart"); }
QString LibraryArtworkProvider::sourceBase() { return QStringLiteral("image://libraryart/"); }

QQuickImageResponse* LibraryArtworkProvider::requestImageResponse(const QString& id, const QSize&) {
    auto* response = new CoverResponse(state_, id);
    state_->pool.start(response);
    return response;
}
} // namespace music_player::m0
