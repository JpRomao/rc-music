#pragma once

#include "presentation/PlayerPresenter.hpp"

namespace music_player::m0::test {

struct PlayerPresenterTestAccess final {
    static void rejectNextSeek(PlayerPresenter& presenter) noexcept { presenter.rejectNextSeekForTest(); }
    [[nodiscard]] static bool enterFrozenUnavailableScenario(PlayerPresenter& presenter) {
        return presenter.enterFrozenUnavailableScenarioForTest();
    }
};

} // namespace music_player::m0::test
