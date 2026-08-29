#pragma once

#include <QStringList>

namespace music_player::m0::experiment {

enum class InitialShellScenario { Empty, Unavailable };

struct ShellLaunchOptions final {
    InitialShellScenario initial_scenario{InitialShellScenario::Empty};
    bool smoke_error_recovery{false};
    bool smoke_tray_lifecycle{false};
    bool smoke_combined_startup{false};
};

[[nodiscard]] ShellLaunchOptions parseShellLaunchOptions(const QStringList& arguments);

} // namespace music_player::m0::experiment
