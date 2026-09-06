#include "presentation/SystemPowerEventsBinding.hpp"

#include "presentation/PlayerPresenter.hpp"

namespace music_player::m0 {

SystemPowerEventsBinding::SystemPowerEventsBinding(PlayerPresenter& presenter, SystemPowerEvents& events)
    : presenter_(presenter), events_(events) {
    events_.setEventHandler(
        [this](const SystemPowerEvent event) { static_cast<void>(presenter_.handleSystemPowerEvent(event)); });
}

SystemPowerEventsBinding::~SystemPowerEventsBinding() {
    events_.setEventHandler({});
    events_.shutdown();
}

} // namespace music_player::m0
