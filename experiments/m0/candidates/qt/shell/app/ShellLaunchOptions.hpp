#pragma once

#include <QStringList>

#include <optional>

namespace music_player::m0::experiment {

enum class InitialShellScenario { Empty, Unavailable };

struct ShellLaunchOptions final {
    InitialShellScenario initial_scenario{InitialShellScenario::Empty};
    bool scenario_control_requested{false};
    bool smoke_error_recovery{false};
    bool smoke_tray_lifecycle{false};
    bool smoke_combined_startup{false};
    bool smoke_preferences_restart_write{false};
    bool smoke_preferences_restart_read{false};
    bool smoke_session_restart_read{false};
    bool smoke_session_crash_before_primary_commit{false};
    bool smoke_session_recovery_repair{false};
    bool smoke_session_defaults_read{false};
    std::optional<QString> test_config_root{};
};

[[nodiscard]] ShellLaunchOptions parseShellLaunchOptions(const QStringList& arguments);
void validateShellLaunchOptions(const ShellLaunchOptions& options, bool test_hooks_enabled,
                                bool combined_audio_enabled);

} // namespace music_player::m0::experiment
