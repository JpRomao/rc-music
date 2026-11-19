#pragma once

#include "m0/ports/SystemMediaControls.hpp"

#include <QObject>

#include <functional>
#include <optional>

namespace music_player::m0 {

class PlayerPresenter;

// While a guest follows a listening room (S10.1), the system controls belong to the room: commands go to
// send (never to the local player) and a transport control is enabled only when allows says so.
struct SystemMediaRoomRoute final {
    std::function<bool()> following;
    std::function<bool(SystemMediaCommandKind)> allows;
    std::function<void(const SystemMediaCommand&)> send;
};

class SystemMediaControlsBinding final : public QObject {
  public:
    SystemMediaControlsBinding(PlayerPresenter& presenter, SystemMediaControls& controls, QObject* parent = nullptr);
    ~SystemMediaControlsBinding() override;

    void refresh();
    void setRoomRoute(SystemMediaRoomRoute route);

  private:
    void dispatch(SystemMediaCommand command);
    [[nodiscard]] bool followingRoom() const;

    PlayerPresenter& presenter_;
    SystemMediaControls& controls_;
    SystemMediaRoomRoute room_;
    std::optional<SystemMediaSnapshot> published_snapshot_;
};

} // namespace music_player::m0
