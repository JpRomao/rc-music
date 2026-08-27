#pragma once

#include "presentation/PlayerPresenter.hpp"

namespace music_player::m0::test {

struct PlayerPresenterTestAccess final {
    static void rejectNextSeek(PlayerPresenter& presenter) noexcept { presenter.rejectNextSeekForTest(); }
};

} // namespace music_player::m0::test
