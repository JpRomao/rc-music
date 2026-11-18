#include "adapters/RoomFileTransfer.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
constexpr std::uint64_t chunk_bytes = room_file_chunk_bytes;

std::uint64_t roundedDownToChunk(const qint64 size) {
    const auto bytes = static_cast<std::uint64_t>(std::max<qint64>(size, 0));
    return bytes - bytes % chunk_bytes;
}
} // namespace

bool RoomFileSender::offer(RoomLink& link, const QString& local_path, const std::string_view sha256,
                           const std::uint32_t transfer_id, const std::uint64_t offset) {
    if (!RoomCache::isValidSha256(sha256) || std::ranges::any_of(queue_, [transfer_id](const Outgoing& queued) {
            return queued.transfer_id == transfer_id;
        }))
        return false;
    auto file = std::make_unique<QFile>(local_path);
    if (!file->open(QIODevice::ReadOnly) || file->size() <= 0)
        return false;
    const auto size_bytes = static_cast<std::uint64_t>(file->size());
    if (offset > size_bytes || !file->seek(static_cast<qint64>(offset)))
        return false;
    if (!link.sendControl(RoomFileOffer{std::string(sha256), size_bytes, transfer_id}))
        return false;
    queue_.push_back({transfer_id, offset, size_bytes, std::move(file)});
    return true;
}

void RoomFileSender::pump(RoomLink& link) {
    std::vector<std::uint32_t> unreadable;
    std::size_t sent = 0;
    while (!queue_.empty() && link.mediaBufferedBytes() < room_media_buffer_high_water_bytes &&
           sent < room_media_buffer_high_water_bytes) {
        auto& outgoing = queue_.front();
        if (outgoing.next_offset >= outgoing.size_bytes) {
            queue_.pop_front();
            continue;
        }
        const auto length = std::min(chunk_bytes, outgoing.size_bytes - outgoing.next_offset);
        const auto data = outgoing.file->read(static_cast<qint64>(length));
        if (static_cast<std::uint64_t>(data.size()) != length) {
            unreadable.push_back(outgoing.transfer_id);
            queue_.pop_front();
            continue;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.constData());
        if (!link.sendMedia(
                {outgoing.transfer_id, outgoing.next_offset, std::vector<std::uint8_t>(bytes, bytes + data.size())})) {
            queue_.clear();
            break;
        }
        outgoing.next_offset += length;
        sent += static_cast<std::size_t>(length);
    }
    if (onFailed)
        for (const auto transfer_id : unreadable)
            onFailed(transfer_id);
}

void RoomFileSender::cancel(const std::uint32_t transfer_id) {
    std::erase_if(queue_, [transfer_id](const Outgoing& queued) { return queued.transfer_id == transfer_id; });
}

bool RoomFileSender::idle() const noexcept { return queue_.empty(); }

RoomFileReceiver::RoomFileReceiver(RoomCache& cache, const std::uint64_t maximum_file_bytes)
    : cache_(cache), maximum_file_bytes_(maximum_file_bytes) {}

bool RoomFileReceiver::accept(const RoomFileOffer& offer) {
    const auto& sha = offer.content_sha256;
    if (!RoomCache::isValidSha256(sha) || offer.size_bytes == 0 || incoming_.contains(offer.transfer_id) ||
        std::ranges::any_of(incoming_, [&sha](const auto& entry) { return entry.second.sha256 == sha; }) ||
        cache_.pathFor(sha))
        return false;
    const auto report = [this, &sha](const RoomTransferError error) {
        if (onFailed)
            onFailed(sha, error);
        return false;
    };
    if (offer.size_bytes > maximum_file_bytes_ || offer.size_bytes > room_cache_quota_bytes)
        return report(RoomTransferError::TooLarge);

    const auto part_path = *cache_.partialPathFor(sha);
    const QFileInfo part_info(part_path);
    auto resume_offset = part_info.isFile() ? roundedDownToChunk(part_info.size()) : 0;
    if (resume_offset > offer.size_bytes) { // not a prefix of this file
        QFile::remove(part_path);
        resume_offset = 0;
    }
    const auto expected = offer.size_bytes - resume_offset;
    if (!cache_.reserve(expected))
        return report(RoomTransferError::QuotaExceeded);

    auto hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    QFile part(part_path);
    auto prepared = QDir().mkpath(cache_.directory()) && part.open(QIODevice::ReadWrite);
    for (std::uint64_t hashed = 0; prepared && hashed < resume_offset;) {
        const auto data = part.read(static_cast<qint64>(std::min(chunk_bytes, resume_offset - hashed)));
        prepared = !data.isEmpty();
        hash->addData(data);
        hashed += static_cast<std::uint64_t>(data.size());
    }
    prepared = prepared && part.resize(static_cast<qint64>(resume_offset));
    part.close();
    if (!prepared) {
        cache_.release(expected);
        QFile::remove(part_path);
        return report(RoomTransferError::Io);
    }
    incoming_.emplace(offer.transfer_id, Incoming{sha, offer.size_bytes, resume_offset, std::move(hash)});
    return resume_offset == offer.size_bytes ? finish(offer.transfer_id) : true;
}

bool RoomFileReceiver::consume(const RoomFileChunk& chunk) {
    const auto found = incoming_.find(chunk.transfer_id);
    if (found == incoming_.end())
        return false;
    auto& incoming = found->second;
    if (chunk.offset != incoming.next_offset) {
        if (incoming.started || chunk.offset > incoming.next_offset)
            return fail(chunk.transfer_id, RoomTransferError::OutOfOrder);
        if (const auto error = goBack(incoming, chunk.offset))
            return fail(chunk.transfer_id, *error);
    }
    incoming.started = true;
    const auto length = static_cast<std::uint64_t>(chunk.payload.size());
    if (length > chunk_bytes || length > incoming.size_bytes - incoming.next_offset)
        return fail(chunk.transfer_id, RoomTransferError::TooLarge);
    if (length == 0)
        return true;

    const QByteArrayView data(reinterpret_cast<const char*>(chunk.payload.data()), static_cast<qsizetype>(length));
    QFile part(*cache_.partialPathFor(incoming.sha256));
    // Opened per chunk, so purgeAll() can always delete it; the size check catches a file changed under us.
    if (!part.open(QIODevice::WriteOnly | QIODevice::Append) ||
        static_cast<std::uint64_t>(part.size()) != incoming.next_offset ||
        part.write(data.data(), data.size()) != data.size() || !part.flush()) {
        part.close();
        return fail(chunk.transfer_id, RoomTransferError::Io);
    }
    part.close();
    incoming.hash->addData(data);
    incoming.next_offset += length;
    cache_.release(length);
    return incoming.next_offset == incoming.size_bytes ? finish(chunk.transfer_id) : true;
}

std::optional<RoomFileRequest> RoomFileReceiver::resumeRequest(const std::string_view sha256) const {
    const auto part_path = cache_.partialPathFor(sha256);
    if (!part_path)
        return std::nullopt;
    const QFileInfo info(*part_path);
    if (!info.isFile())
        return std::nullopt;
    return RoomFileRequest{std::string(sha256), roundedDownToChunk(info.size())};
}

void RoomFileReceiver::abandon(const std::uint32_t transfer_id) {
    const auto found = incoming_.find(transfer_id);
    if (found == incoming_.end())
        return;
    cache_.release(found->second.size_bytes - found->second.next_offset);
    incoming_.erase(found);
}

std::optional<RoomTransferError> RoomFileReceiver::goBack(Incoming& incoming, const std::uint64_t offset) {
    const auto again = incoming.next_offset - offset;
    if (!cache_.reserve(again))
        return RoomTransferError::QuotaExceeded;
    // From here fail() releases size - next_offset; the bytes reserved again are counted once offset
    // becomes next_offset.
    auto hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    QFile part(*cache_.partialPathFor(incoming.sha256));
    auto prepared = part.open(QIODevice::ReadWrite);
    for (std::uint64_t hashed = 0; prepared && hashed < offset;) {
        const auto data = part.read(static_cast<qint64>(std::min(chunk_bytes, offset - hashed)));
        prepared = !data.isEmpty();
        hash->addData(data);
        hashed += static_cast<std::uint64_t>(data.size());
    }
    prepared = prepared && part.resize(static_cast<qint64>(offset));
    part.close();
    incoming.next_offset = offset;
    if (!prepared)
        return RoomTransferError::Io;
    incoming.hash = std::move(hash);
    return std::nullopt;
}

bool RoomFileReceiver::fail(const std::uint32_t transfer_id, const RoomTransferError error) {
    auto node = incoming_.extract(transfer_id);
    if (node.empty())
        return false;
    auto& incoming = node.mapped();
    cache_.release(incoming.size_bytes - incoming.next_offset);
    QFile::remove(*cache_.partialPathFor(incoming.sha256));
    if (onFailed)
        onFailed(incoming.sha256, error);
    return false;
}

bool RoomFileReceiver::finish(const std::uint32_t transfer_id) {
    auto& incoming = incoming_.at(transfer_id);
    if (incoming.hash->result().toHex() != QByteArrayView(incoming.sha256))
        return fail(transfer_id, RoomTransferError::HashMismatch);
    const auto part_path = *cache_.partialPathFor(incoming.sha256);
    const auto final_path = QDir(cache_.directory()).filePath(QString::fromStdString(incoming.sha256));
    if (!QFile::rename(part_path, final_path))
        return fail(transfer_id, RoomTransferError::Io);
    auto sha = std::move(incoming.sha256);
    incoming_.erase(transfer_id);
    if (onComplete)
        onComplete(std::move(sha), final_path);
    return true;
}

} // namespace music_player::m0
