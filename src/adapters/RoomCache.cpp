#include "adapters/RoomCache.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr std::size_t sha256_hex_digits = 64;

QString fileName(const std::string_view sha256) {
    return QString::fromLatin1(sha256.data(), static_cast<qsizetype>(sha256.size()));
}
} // namespace

RoomCache::RoomCache(QString config_directory)
    : directory_(QDir(std::move(config_directory)).filePath(QStringLiteral("room-cache"))) {}

bool RoomCache::isValidSha256(const std::string_view sha256) noexcept {
    return sha256.size() == sha256_hex_digits && std::ranges::all_of(sha256, [](const char digit) {
               return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
           });
}

QString RoomCache::directory() const { return directory_; }

std::optional<QString> RoomCache::pathFor(const std::string_view sha256) const {
    if (!isValidSha256(sha256))
        return std::nullopt;
    auto path = QDir(directory_).filePath(fileName(sha256));
    if (!QFileInfo(path).isFile())
        return std::nullopt;
    return path;
}

std::optional<QString> RoomCache::partialPathFor(const std::string_view sha256) const {
    if (!isValidSha256(sha256))
        return std::nullopt;
    return QDir(directory_).filePath(fileName(sha256) + QStringLiteral(".part"));
}

bool RoomCache::reserve(const std::uint64_t bytes) {
    const auto fits = [this, bytes] {
        const auto used = bytesOnDisk() + reserved_bytes_;
        return used <= room_cache_quota_bytes && bytes <= room_cache_quota_bytes - used;
    };
    while (!fits()) {
        if (played_.empty())
            return false;
        const auto oldest = std::move(played_.front());
        played_.erase(played_.begin());
        if (const auto path = pathFor(oldest))
            QFile::remove(*path);
    }
    reserved_bytes_ += bytes;
    return true;
}

void RoomCache::release(const std::uint64_t bytes) noexcept { reserved_bytes_ -= std::min(bytes, reserved_bytes_); }

void RoomCache::markPlayed(const std::string_view sha256) {
    if (!isValidSha256(sha256) || std::ranges::find(played_, sha256) != played_.end())
        return;
    played_.emplace_back(sha256);
}

void RoomCache::purgeAll() {
    QDir(directory_).removeRecursively();
    reserved_bytes_ = 0;
    played_.clear();
}

std::uint64_t RoomCache::bytesOnDisk() const {
    std::uint64_t total = 0;
    const auto entries = QDir(directory_).entryInfoList(QDir::Files | QDir::Hidden | QDir::System);
    for (const auto& entry : entries)
        total += static_cast<std::uint64_t>(std::max<qint64>(entry.size(), 0));
    return total;
}

} // namespace music_player::m0
