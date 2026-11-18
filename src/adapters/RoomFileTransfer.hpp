#pragma once

#include "adapters/RoomCache.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomMessages.hpp"
#include "m0/ports/RoomTransport.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {

// Hash-addressed file transfer over a room link (ADR-0022). The host offers a file with
// RoomFileOffer{sha, size, transfer_id} on the control channel and streams RoomFileChunk on the
// media channel in order; the guest writes <cache>/<sha>.part and keeps it only if the SHA-256 of
// the whole file matches. Both sides run on the thread that owns the link.

// The sender only queues more chunks while the media channel holds less than this.
inline constexpr std::size_t room_media_buffer_high_water_bytes = 1024 * 1024;

enum class RoomTransferError : std::uint8_t { TooLarge, HashMismatch, OutOfOrder, QuotaExceeded, Io };

// One sender per link. Transfers are sent one after the other in offer order (the current item
// first, then the prefetched ones), reading room_file_chunk_bytes at a time: the file is never
// loaded whole, and one pump() sends at most room_media_buffer_high_water_bytes.
class RoomFileSender {
  public:
    // Sends the RoomFileOffer and queues the chunks from `offset` (a RoomFileRequest's offset when
    // resuming; the offer always carries the full size). false, with nothing sent, for an invalid
    // hash, a transfer_id already queued, an unreadable file or an offset beyond its size. The
    // content is not re-hashed here: the receiver checks it.
    bool offer(RoomLink& link, const QString& local_path, std::string_view sha256, std::uint32_t transfer_id,
               std::uint64_t offset = 0);
    // Sends chunks while link.mediaBufferedBytes() < room_media_buffer_high_water_bytes. A link
    // that refuses a chunk (closed) drops every queued transfer.
    void pump(RoomLink& link);
    // Drops a queued transfer (its item left the queue).
    void cancel(std::uint32_t transfer_id);
    [[nodiscard]] bool idle() const noexcept;

    // A transfer dropped because its file could not be read any more.
    std::function<void(std::uint32_t /*transfer_id*/)> onFailed;

  private:
    struct Outgoing {
        std::uint32_t transfer_id{};
        std::uint64_t next_offset{};
        std::uint64_t size_bytes{};
        std::unique_ptr<QFile> file;
    };
    std::deque<Outgoing> queue_;
};

// One transfer per transfer_id at a time. accept() returns false for every refused offer; onFailed
// is also called when the offer was well formed (TooLarge, QuotaExceeded, Io). A malformed offer
// (invalid hash, size 0), a busy transfer_id or hash, or a hash already complete in the cache is
// only refused. consume() returns false for a chunk of an unknown transfer and for every failure;
// a failed transfer calls onFailed once and deletes its .part.
//
// Resume: when <sha>.part exists, resumeRequest() gives the offset to ask the sender for (its size
// rounded down to a whole chunk) and accept() continues from there: it cuts any partial tail and
// hashes the kept prefix, so the final check covers the whole file. A sender starting elsewhere
// gets OutOfOrder, which deletes the .part, so the next attempt starts from zero.
class RoomFileReceiver {
  public:
    // maximum_file_bytes: the host receiving guest uploads passes maximum_guest_upload_bytes.
    explicit RoomFileReceiver(RoomCache& cache, std::uint64_t maximum_file_bytes = room_cache_quota_bytes);

    [[nodiscard]] bool accept(const RoomFileOffer& offer);
    [[nodiscard]] bool consume(const RoomFileChunk& chunk);
    [[nodiscard]] std::optional<RoomFileRequest> resumeRequest(std::string_view sha256) const;
    // The link dropped: forgets the transfer and its reservation but keeps the .part for resume.
    void abandon(std::uint32_t transfer_id);

    std::function<void(std::string /*sha256*/, QString /*path*/)> onComplete;
    std::function<void(std::string, RoomTransferError)> onFailed;

  private:
    struct Incoming {
        std::string sha256;
        std::uint64_t size_bytes{};
        std::uint64_t next_offset{};
        std::unique_ptr<QCryptographicHash> hash;
    };
    bool fail(std::uint32_t transfer_id, RoomTransferError error);
    bool finish(std::uint32_t transfer_id);

    RoomCache& cache_;
    std::uint64_t maximum_file_bytes_;
    std::map<std::uint32_t, Incoming> incoming_;
};

} // namespace music_player::m0
