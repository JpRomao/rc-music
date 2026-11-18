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

// Host side: streams one of the host's files to a guest link.
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
    bool operator==(const RoomFileStatus&) const = default;
};

// Guest side: the disposable room cache and its receiver. In the app the file work runs on a worker
// thread; every call returns at once and every callback comes back on the guest's thread, in the
// order of the calls.
class RoomFileInbox {
  public:
    virtual ~RoomFileInbox() = default;
    // Calls done once with what is in the cache for sha256.
    virtual void status(const std::string& sha256, std::function<void(RoomFileStatus)> done) = 0;
    // An offer the guest asked for; a refused offer ends in onFailed (or nothing when malformed).
    virtual void accept(const RoomFileOffer& offer) = 0;
    virtual void consume(RoomFileChunk chunk) = 0;
    // Stops a transfer (its item left the window, or the link dropped); the partial file stays for resume.
    virtual void abandon(std::uint32_t transfer_id) = 0;
    // The item stopped being current: its file may be evicted to make room.
    virtual void markPlayed(const std::string& sha256) = 0;
    // Leaving the room: abandons every transfer in flight, then deletes every cached file.
    virtual void purge() = 0;

    std::function<void(std::string /*sha256*/, std::filesystem::path /*file*/)> onComplete;
    std::function<void(std::string /*sha256*/)> onFailed;
};

} // namespace music_player::m0
