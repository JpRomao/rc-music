#pragma once

#include "adapters/QtSystemAppearance.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/ShellUiContract.hpp"

namespace music_player::m0 {

inline void bindShellAppearance(PlayerPresenter& presenter, ShellUiContract& contract, QtSystemAppearance& system) {
    const auto refresh = [&presenter, &contract, &system] {
        const auto appearance = system.snapshot();
        const auto& preferences = presenter.preferences();
        contract.updateAppearance(preferences.theme, preferences.motion, appearance.dark, appearance.reduced_motion);
    };
    QObject::connect(&presenter, &PlayerPresenter::preferencesChanged, &contract, refresh);
    QObject::connect(&system, &QtSystemAppearance::appearanceChanged, &contract, refresh);
    refresh();
}

} // namespace music_player::m0
