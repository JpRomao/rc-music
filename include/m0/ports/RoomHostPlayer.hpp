#pragma once

#include "m0/domain/LocalFileQueue.hpp"
#include "m0/domain/PlaybackTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace music_player::m0 {

// The local player as the listening-room host drives it (ADR-0022, S7.1). Qt-free.
//
// PlayerController owns transport and preferences only; the queue, next/previous and the session
// autosave live with the presentation (PlayerPresenter), which implements this port in S7.3 by
// delegating to its own commands. Tests implement it with LocalFileQueue + PlayerController + the
// DeterministicPlaybackBackend (tests/support/FakeRoomHost.hpp).
//
// Shuffle, repeat and the end-of-queue behaviour are host-only and deliberately absent: they reach
// the room only as the queue order and the current item that state() reports.
struct RoomHostPlayerState final {
    LocalFileQueueSnapshot queue{};
    PlaybackStatus status{PlaybackStatus::Empty};
    std::int64_t position_ms{};
    bool operator==(const RoomHostPlayerState&) const = default;
};

class RoomHostPlayer {
  public:
    virtual ~RoomHostPlayer() = default;

    [[nodiscard]] virtual RoomHostPlayerState state() const = 0;
    // Each command returns false when the player refuses it and nothing changed.
    virtual bool next() = 0;
    virtual bool previous() = 0;
    virtual bool seek(std::int64_t position_ms) = 0;
    virtual bool pause() = 0;
    virtual bool play() = 0;
    // Appends one local file at the end of the queue; its new occurrence, or nullopt when refused.
    virtual std::optional<LocalQueueOccurrenceId> enqueue(const std::filesystem::path& source) = 0;
    virtual bool remove(LocalQueueOccurrenceId occurrence) = 0;
    // destination_index is an index of the whole local queue after the move.
    virtual bool move(LocalQueueOccurrenceId occurrence, std::size_t destination_index) = 0;

    // Called after any queue, current-item, play/pause or seek change, whoever made it (the host's
    // own UI or a command above), possibly from inside the command. Not called for the position
    // ticks of normal playback.
    std::function<void()> onChanged;
};

} // namespace music_player::m0
