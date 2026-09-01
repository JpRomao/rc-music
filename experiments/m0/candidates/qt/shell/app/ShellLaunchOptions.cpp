#include "ShellLaunchOptions.hpp"

#include <QDir>

#include <stdexcept>

namespace music_player::m0::experiment {

ShellLaunchOptions parseShellLaunchOptions(const QStringList& arguments) {
    ShellLaunchOptions options;
    bool scenario_seen = false;
    bool smoke_seen = false;
    bool config_root_seen = false;
    bool source_only = false;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (source_only) {
            options.source_paths.push_back(argument);
            continue;
        }
        if (argument == QStringLiteral("--")) {
            source_only = true;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--m0-scenario="))) {
            if (scenario_seen)
                throw std::invalid_argument{"the M0 shell scenario may be specified only once"};
            scenario_seen = true;
            options.scenario_control_requested = true;
            const QString value = argument.sliced(QStringLiteral("--m0-scenario=").size());
            if (value == QStringLiteral("empty"))
                options.initial_scenario = InitialShellScenario::Empty;
            else if (value == QStringLiteral("unavailable"))
                options.initial_scenario = InitialShellScenario::Unavailable;
            else
                throw std::invalid_argument{"unsupported M0 shell scenario"};
            continue;
        }
        if (argument.startsWith(QStringLiteral("--m0-smoke="))) {
            if (smoke_seen)
                throw std::invalid_argument{"the M0 shell smoke may be specified only once"};
            smoke_seen = true;
            const QString value = argument.sliced(QStringLiteral("--m0-smoke=").size());
            if (value == QStringLiteral("error-recovery"))
                options.smoke_error_recovery = true;
            else if (value == QStringLiteral("tray-lifecycle"))
                options.smoke_tray_lifecycle = true;
            else if (value == QStringLiteral("combined-startup"))
                options.smoke_combined_startup = true;
            else if (value == QStringLiteral("preferences-restart-write"))
                options.smoke_preferences_restart_write = true;
            else if (value == QStringLiteral("preferences-restart-read"))
                options.smoke_preferences_restart_read = true;
            else if (value == QStringLiteral("session-restart-read"))
                options.smoke_session_restart_read = true;
            else if (value == QStringLiteral("session-crash-before-primary-commit"))
                options.smoke_session_crash_before_primary_commit = true;
            else if (value == QStringLiteral("session-recovery-repair"))
                options.smoke_session_recovery_repair = true;
            else if (value == QStringLiteral("session-defaults-read"))
                options.smoke_session_defaults_read = true;
            else
                throw std::invalid_argument{"unsupported M0 shell smoke"};
            continue;
        }
        if (argument.startsWith(QStringLiteral("--m0-test-config-root="))) {
            if (config_root_seen)
                throw std::invalid_argument{"the M0 test config root may be specified only once"};
            config_root_seen = true;
            const QString value = argument.sliced(QStringLiteral("--m0-test-config-root=").size());
            if (value.isEmpty() || !QDir::isAbsolutePath(value))
                throw std::invalid_argument{"the M0 test config root must be an absolute path"};
            options.test_config_root = QDir::cleanPath(value);
            continue;
        }
        if (argument.startsWith(QStringLiteral("--m0-")))
            throw std::invalid_argument{"unsupported M0 shell option"};
        options.source_paths.push_back(argument);
    }
    if (options.smoke_error_recovery && options.initial_scenario != InitialShellScenario::Unavailable)
        throw std::invalid_argument{"the error recovery smoke requires the unavailable scenario"};
    if (options.smoke_tray_lifecycle && options.initial_scenario != InitialShellScenario::Empty)
        throw std::invalid_argument{"the tray lifecycle smoke requires the empty scenario"};
    if (options.smoke_combined_startup && options.initial_scenario != InitialShellScenario::Empty)
        throw std::invalid_argument{"the combined startup smoke requires the empty scenario"};
    const bool preferences_restart = options.smoke_preferences_restart_write || options.smoke_preferences_restart_read;
    const bool session_restart = options.smoke_session_restart_read ||
                                 options.smoke_session_crash_before_primary_commit ||
                                 options.smoke_session_recovery_repair || options.smoke_session_defaults_read;
    const bool executable_smoke = options.smoke_error_recovery || options.smoke_tray_lifecycle ||
                                  options.smoke_combined_startup || preferences_restart || session_restart;
    if ((preferences_restart || session_restart) && !options.test_config_root.has_value())
        throw std::invalid_argument{"the restart smoke requires its test config root"};
    if (options.test_config_root.has_value() && !executable_smoke)
        throw std::invalid_argument{"the M0 test config root requires an executable smoke"};
    if ((preferences_restart || session_restart) && options.initial_scenario != InitialShellScenario::Empty)
        throw std::invalid_argument{"the restart smoke requires the empty scenario"};
    return options;
}

QStringList resolveShellSourcePaths(const QStringList& source_paths, const QString& working_directory) {
    if (working_directory.isEmpty() || !QDir::isAbsolutePath(working_directory))
        throw std::invalid_argument{"the shell working directory must be absolute"};

    const QDir base{QDir::cleanPath(working_directory)};
    QStringList resolved;
    resolved.reserve(source_paths.size());
    for (const QString& source_path : source_paths) {
        if (source_path.isEmpty())
            throw std::invalid_argument{"a shell source path cannot be empty"};
#ifdef Q_OS_WIN
        const bool drive_relative = source_path.size() >= 2 && source_path.at(1) == QLatin1Char(':') &&
                                    (source_path.size() == 2 ||
                                     (source_path.at(2) != QLatin1Char('/') && source_path.at(2) != QLatin1Char('\\')));
        if (drive_relative)
            throw std::invalid_argument{"drive-relative shell source paths are ambiguous"};
#endif
        resolved.push_back(QDir::isAbsolutePath(source_path) ? source_path
                                                             : QDir::cleanPath(base.absoluteFilePath(source_path)));
    }
    return resolved;
}

void validateShellLaunchOptions(const ShellLaunchOptions& options, const bool test_hooks_enabled,
                                const bool combined_audio_enabled) {
    if (!test_hooks_enabled &&
        (options.scenario_control_requested || options.smoke_error_recovery || options.smoke_tray_lifecycle ||
         options.smoke_combined_startup || options.smoke_preferences_restart_write ||
         options.smoke_preferences_restart_read || options.smoke_session_restart_read ||
         options.smoke_session_crash_before_primary_commit || options.smoke_session_recovery_repair ||
         options.smoke_session_defaults_read || options.test_config_root.has_value())) {
        throw std::invalid_argument{"M0 shell scenario controls are unavailable in this build"};
    }
    if (!combined_audio_enabled && options.smoke_combined_startup)
        throw std::invalid_argument{"the combined startup smoke requires audio support"};
    if (!combined_audio_enabled && !options.source_paths.isEmpty())
        throw std::invalid_argument{"shell source paths require audio support"};
    if (!combined_audio_enabled &&
        (options.smoke_session_restart_read || options.smoke_session_crash_before_primary_commit ||
         options.smoke_session_recovery_repair || options.smoke_session_defaults_read))
        throw std::invalid_argument{"the session restart smoke requires audio support"};
}

} // namespace music_player::m0::experiment
