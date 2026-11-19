#pragma once

#include "m0/domain/RoomMessages.hpp"
#include "m0/ports/RoomTransport.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace music_player::m0 {

// Room file transfer as the application layer drives it (ADR-0022, S8.2). Qt-free ports:
// src/adapters/QtRoomFileOutbox and QtRoomFileInbox implement them over RoomFileSender,
// RoomFileReceiver and RoomCache. Files are addressed by content hash only: no local path or file
// name ever goes on the wire.

// Host side: streams one of the host's files to a guest link. A guest uploading its own file (S12.1) uses
// one the same way over its link to the host.
class RoomFileOutbox {
  public:
    virtual ~RoomFileOutbox() = default;
    // Sends RoomFileOffer{sha256, size, transfer_id} and then the chunks from offset, in the order of
    // the calls for that link (one file after the other), paced by the link's media buffer. false when
    // nothing was sent: an unreadable file, an offset beyond its size or a closed link.
    virtual bool send(const std::shared_ptr<RoomLink>& link, const std::filesystem::path& file,
                      const std::string& sha256, std::uint32_t transfer_id, std::uint64_t offset) = 0;
    // Stops a transfer still queued or in progress for that link.
    virtual void cancel(const RoomLink& link, std::uint32_t transfer_id) = 0;
    // The link is gone: drops everything queued for it.
    virtual void forget(const RoomLink& link) = 0;
};

struct RoomFileStatus final {
    // The verified file is in the cache.
    bool complete{};
    // Otherwise, the offset to request: 0, or the whole chunks of a partial file kept for resume.
    std::uint64_t resume_offset{};
    // The cached file when complete.
    std::filesystem::path file{};
    // Its size in bytes when complete (S12.1: the host's upload cache, for a file it is not sent again).
    std::uint64_t size_bytes{};
    bool operator==(const RoomFileStatus&) const = default;
};

// Why a transfer ended without its file. Gap: the host streams from beyond the partial file kept for
// resume (it answered a request made before that file was cut back); the partial file stays, and
// asking again with the offset status() gives now resumes it. Failed: anything else (hash mismatch,
// I/O, quota, size, a chunk out of order); the partial file is gone.
enum class RoomFileFailure : std::uint8_t { Failed, Gap };

// Guest side: the disposable room cache and its receiver. The host receives guest uploads (S12.1) through
// another instance, its own upload cache, with the roles reversed. In the app the file work runs on a worker
// thread; every call returns at once and every callback comes back on the guest's thread, in the
// order of the calls.
class RoomFileInbox {
  public:
    virtual ~RoomFileInbox() = default;
    // Calls done once with what is in the cache for sha256. A complete file it reports is wanted again: it
    // stops being evictable until marked as played again.
    virtual void status(const std::string& sha256, std::function<void(RoomFileStatus)> done) = 0;
    // An offer the guest asked for; a refused offer ends in onFailed (or nothing when malformed).
    virtual void accept(const RoomFileOffer& offer) = 0;
    // The offer carries no offset: a transfer continues from where its first chunk starts, even below
    // the partial file kept for resume (the host answering an older request); a first chunk beyond it,
    // or any later chunk out of order, ends the transfer (Gap and Failed respectively).
    virtual void consume(RoomFileChunk chunk) = 0;
    // Stops a transfer (its item left the window, or the link dropped); the partial file stays for resume.
    virtual void abandon(std::uint32_t transfer_id) = 0;
    // The item stopped being current: its file may be evicted to make room.
    virtual void markPlayed(const std::string& sha256) = 0;
    // Leaving the room: abandons every transfer in flight, then deletes every cached file.
    virtual void purge() = 0;
    // S12.1, the host's upload cache: a guest file it refused, or one no queued guest item uses any more.
    // Abandons any transfer of sha256 and deletes its partial and complete file, so nothing is left behind (a
    // complete file still open elsewhere goes later, when the cache needs the room).
    virtual void discard(const std::string& sha256) = 0;

    std::function<void(std::string /*sha256*/, std::filesystem::path /*file*/)> onComplete;
    // transfer_id is the accepted offer's: a late answer for a transfer the guest already replaced is
    // told apart from the live one.
    std::function<void(std::string /*sha256*/, std::uint32_t /*transfer_id*/, RoomFileFailure)> onFailed;
};

} // namespace music_player::m0
