#pragma once

#include "presentation/PlayerPresenter.hpp"

namespace music_player::m0::test {

struct PlayerPresenterTestAccess final {
    static void rejectNextSeek(PlayerPresenter& presenter) noexcept { presenter.rejectNextSeekForTest(); }
    [[nodiscard]] static std::size_t seekCommitAttemptCount(const PlayerPresenter& presenter) noexcept {
        return presenter.seekCommitAttemptCountForTest();
    }
};

} // namespace music_player::m0::test
