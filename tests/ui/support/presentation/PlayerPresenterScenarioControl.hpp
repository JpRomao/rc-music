#pragma once

#include "presentation/PlayerPresenter.hpp"

namespace music_player::m0::experiment {

class PlayerPresenterScenarioControl final {
  public:
    [[nodiscard]] static bool enterUnavailable(PlayerPresenter& presenter) {
        return presenter.enterFrozenUnavailableScenario();
    }
};

} // namespace music_player::m0::experiment
