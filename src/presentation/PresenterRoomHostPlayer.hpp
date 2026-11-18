#pragma once

#include "m0/ports/RoomHostPlayer.hpp"

#include <QMetaObject>

namespace music_player::m0 {

class PlayerPresenter;

// RoomHostPlayer over the real player (ADR-0022, S7.3): every command delegates to PlayerPresenter,
// so the host's own UI and the room drive the same queue. onChanged follows the presenter's
// stateChanged and playbackChanged signals. The composition turns PlayerPresenter::setRoomPlayOrder on
// while a room is open, so the queue order stays the play order. Construct, use and destroy on the
// presenter's thread; it must not outlive the presenter.
class PresenterRoomHostPlayer final : public RoomHostPlayer {
  public:
    explicit PresenterRoomHostPlayer(PlayerPresenter& presenter);
    ~PresenterRoomHostPlayer() override;
    PresenterRoomHostPlayer(const PresenterRoomHostPlayer&) = delete;
    PresenterRoomHostPlayer& operator=(const PresenterRoomHostPlayer&) = delete;

    [[nodiscard]] RoomHostPlayerState state() const override;
    bool next() override;
    bool previous() override;
    bool seek(std::int64_t position_ms) override;
    bool pause() override;
    bool play() override;
    std::optional<LocalQueueOccurrenceId> enqueue(const std::filesystem::path& source) override;
    bool remove(LocalQueueOccurrenceId occurrence) override;
    bool move(LocalQueueOccurrenceId occurrence, std::size_t destination_index) override;

  private:
    void changed() const;

    PlayerPresenter& presenter_;
    QMetaObject::Connection state_connection_;
    QMetaObject::Connection playback_connection_;
};

} // namespace music_player::m0
