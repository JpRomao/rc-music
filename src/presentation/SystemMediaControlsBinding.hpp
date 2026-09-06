#pragma once

#include "m0/ports/SystemMediaControls.hpp"

#include <QObject>

#include <optional>

namespace music_player::m0 {

class PlayerPresenter;

class SystemMediaControlsBinding final : public QObject {
  public:
    SystemMediaControlsBinding(PlayerPresenter& presenter, SystemMediaControls& controls, QObject* parent = nullptr);
    ~SystemMediaControlsBinding() override;

    void refresh();

  private:
    void dispatch(SystemMediaCommand command);

    PlayerPresenter& presenter_;
    SystemMediaControls& controls_;
    std::optional<SystemMediaSnapshot> published_snapshot_;
};

} // namespace music_player::m0
