#pragma once

#include "m0/ports/SystemPowerEvents.hpp"

namespace music_player::m0 {

class PlayerPresenter;

class SystemPowerEventsBinding final {
  public:
    SystemPowerEventsBinding(PlayerPresenter& presenter, SystemPowerEvents& events);
    ~SystemPowerEventsBinding();

    SystemPowerEventsBinding(const SystemPowerEventsBinding&) = delete;
    SystemPowerEventsBinding& operator=(const SystemPowerEventsBinding&) = delete;

  private:
    PlayerPresenter& presenter_;
    SystemPowerEvents& events_;
};

} // namespace music_player::m0
