#pragma once

#include "presentation/PlayerPresenter.hpp"

namespace music_player::m0::test {

struct PlayerPresenterTestAccess final {
    static void rejectNextSeek(PlayerPresenter& presenter) noexcept { presenter.rejectNextSeekForTest(); }
    [[nodiscard]] static std::size_t seekCommitAttemptCount(const PlayerPresenter& presenter) noexcept {
        return presenter.seekCommitAttemptCountForTest();
    }
    static void pollMetadata(PlayerPresenter& presenter) { presenter.pollMetadata(); }
    static void pollSourceImport(PlayerPresenter& presenter) { presenter.pollSourceImport(); }
    static void pollPreferencePersistence(PlayerPresenter& presenter) { presenter.pollPreferencePersistence(); }
    static void pollPersistence(PlayerPresenter& presenter) { presenter.pollPersistence(); }
    // Stands for a download import call that is running, as PlayerDownloadImport marks it.
    static void setDownloadImportCallInProgress(PlayerPresenter& presenter, const bool running) noexcept {
        presenter.download_import_call_in_progress_ = running;
    }
};

} // namespace music_player::m0::test
