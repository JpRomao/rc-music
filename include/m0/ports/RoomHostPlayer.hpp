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
// Play order: from the current item on, state().queue.items IS the order in which the tracks will
// play. The room's queue is what everyone hears and what the S8 prefetch follows, so an implementation
// must never pick the next track out of that order. Requirement for S7.3: PlayerPresenter's shuffle
// mode currently picks a random unvisited item on advance; while a room is open it must instead turn
// shuffle into a physical reorder of the upcoming items (as shuffleQueue() does) and then advance in
// queue order.
//
// Shuffle, repeat and the end-of-queue behaviour are host-only and deliberately absent: they reach
// the room only as the queue order and the current item that state() reports.
//
// status and position_ms are where the player is heading, not only what its engine has confirmed: a
// command it accepted counts at once (play reads Playing, a seek its target), and so does a held start
// (holdStart). The engine may confirm later (in the app, on a later event-loop turn); the room must never
// mistake that wait for a change. seeks counts the seeks the player accepted, whoever asked: a seek is a
// start even when it lands near the old position. starting: status is Playing but the engine has not
// started yet (a held start, a play it has not confirmed, a track still loading to play), so position_ms
// stands still until it does.
struct RoomHostPlayerState final {
    LocalFileQueueSnapshot queue{};
    PlaybackStatus status{PlaybackStatus::Empty};
    std::int64_t position_ms{};
    std::uint64_t seeks{};
    bool starting{};
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

    // A start the room leads (room_play_lead_ms): holdStart keeps the current track silent where it is
    // (pausing it, or keeping a track that is still loading from starting) while state() and the host's
    // own controls go on showing Playing. releaseStart plays it; it returns false when nothing is held or
    // the player refuses to play, and state() then says so. The host's own pause, another track or an
    // error ends a hold, and play() during one changes nothing. A player may hold a resume of its own
    // controls before its engine hears it and notify (onChanged, state() Playing and starting): a
    // holdStart from that notification keeps the hold; without one the player plays at once.
    virtual bool holdStart() = 0;
    virtual bool releaseStart() = 0;

    // Called after any queue, current-item, play/pause or seek change, whoever made it (the host's
    // own UI or a command above), possibly from inside the command. It may also be called for the
    // position updates of normal playback: the host keeps its anchor while the position stays within
    // room_anchor_tolerance_ms of it.
    std::function<void()> onChanged;
};

} // namespace music_player::m0
