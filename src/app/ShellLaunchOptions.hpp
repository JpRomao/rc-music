#pragma once

#include <QStringList>

#include <optional>

namespace music_player::m0::experiment {

enum class InitialShellScenario { Empty, Unavailable };
enum class InitialWindowDisposition { Show, HideToTray, ShowWithTrayUnavailableNotice };

struct ShellLaunchOptions final {
    bool diagnostics_enabled{false};
    bool smoke_diagnostics{false};
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
    bool smoke_single_instance_executable{false};
    bool smoke_start_minimized_no_tray{false};
    bool smoke_startup_source_precedence{false};
    std::optional<QString> test_config_root{};
    QStringList source_paths{};
};

[[nodiscard]] ShellLaunchOptions parseShellLaunchOptions(const QStringList& arguments);
[[nodiscard]] InitialWindowDisposition resolveInitialWindowDisposition(bool start_minimized, bool tray_available,
                                                                       bool has_explicit_sources) noexcept;
[[nodiscard]] QStringList resolveShellSourcePaths(const QStringList& source_paths, const QString& working_directory);
void validateShellLaunchOptions(const ShellLaunchOptions& options, bool test_hooks_enabled,
                                bool combined_audio_enabled);

} // namespace music_player::m0::experiment
