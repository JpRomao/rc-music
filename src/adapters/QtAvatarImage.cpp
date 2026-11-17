#include "adapters/QtAvatarImage.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>

#include <algorithm>

namespace music_player::m0 {
namespace {
constexpr int allocation_limit_megabytes = 64;
}

std::optional<QByteArray> normalizeAvatarImage(const QString& path) {
    const QFileInfo information(path);
    if (!information.isFile() || information.size() < 0 ||
        static_cast<std::uint64_t>(information.size()) > maximum_custom_avatar_input_bytes)
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QImageReader reader(&file);
    reader.setAllocationLimit(allocation_limit_megabytes);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg")
        return std::nullopt;
    const auto image = reader.read();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return std::nullopt;
    const auto side = std::min(image.width(), image.height());
    const auto square = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
    const auto pixels = static_cast<int>(avatar_pixels);
    const auto scaled = square.scaled(pixels, pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_ARGB32);
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !scaled.save(&buffer, "PNG"))
        return std::nullopt;
    return png;
}

QString saveCustomAvatar(const QString& config_directory, const QByteArray& png) {
    if (png.isEmpty() || config_directory.isEmpty() ||
        !QDir{}.mkpath(QDir(config_directory).filePath(QStringLiteral("avatars"))))
        return {};
    QSaveFile file(QDir(config_directory).filePath(QStringLiteral("avatars/custom.png")));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.flush()) {
        file.cancelWriting();
        return {};
    }
    if (!file.commit())
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
}
} // namespace music_player::m0
