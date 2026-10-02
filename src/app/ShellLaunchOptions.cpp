#include "ShellLaunchOptions.hpp"

#include "OpenSourcesCommand.hpp"

#include <QDir>

#include <stdexcept>

namespace music_player::m0::experiment {
namespace {

#ifdef Q_OS_WIN
[[nodiscard]] bool isWindowsSeparator(const QChar character) {
    return character == QLatin1Char('/') || character == QLatin1Char('\\');
}

[[nodiscard]] qsizetype nextWindowsSeparator(const QString& path, const qsizetype offset) {
    for (qsizetype index = offset; index < path.size(); ++index) {
        if (isWindowsSeparator(path.at(index)))
            return index;
    }
    return -1;
}

[[nodiscard]] QString windowsVolumeRoot(const QString& fully_qualified_path) {
    if (fully_qualified_path.size() >= 3 && fully_qualified_path.at(1) == QLatin1Char(':'))
        return fully_qualified_path.first(2);

    const bool extended = fully_qualified_path.size() >= 4 && fully_qualified_path.at(2) == QLatin1Char('?') &&
                          isWindowsSeparator(fully_qualified_path.at(3));
    if (extended && fully_qualified_path.size() >= 7 && fully_qualified_path.at(5) == QLatin1Char(':'))
        return fully_qualified_path.first(6);

    const qsizetype server_start = extended ? 8 : 2;
    const qsizetype server_end = nextWindowsSeparator(fully_qualified_path, server_start);
    const qsizetype share_end = nextWindowsSeparator(fully_qualified_path, server_end + 1);
    return fully_qualified_path.first(share_end < 0 ? fully_qualified_path.size() : share_end);
}
#endif

} // namespace

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
        if (argument == QStringLiteral("--diagnostics")) {
            if (options.diagnostics_enabled)
                throw std::invalid_argument{"diagnostics may be enabled only once"};
            options.diagnostics_enabled = true;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--diagnostics=")))
            throw std::invalid_argument{"diagnostics does not accept a value"};
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
            else if (value == QStringLiteral("diagnostics"))
                options.smoke_diagnostics = true;
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
            else if (value == QStringLiteral("session-receipts-without-restore"))
                options.smoke_session_receipts_without_restore = true;
            else if (value == QStringLiteral("downloads"))
                options.smoke_downloads = true;
            else if (value == QStringLiteral("single-instance-executable"))
                options.smoke_single_instance_executable = true;
            else if (value == QStringLiteral("start-minimized-no-tray"))
                options.smoke_start_minimized_no_tray = true;
            else if (value == QStringLiteral("startup-source-precedence"))
                options.smoke_startup_source_precedence = true;
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
                                 options.smoke_session_recovery_repair || options.smoke_session_defaults_read ||
                                 options.smoke_session_receipts_without_restore || options.smoke_downloads;
    const bool startup_behavior =
        options.smoke_start_minimized_no_tray || options.smoke_startup_source_precedence || options.smoke_diagnostics;
    if ((preferences_restart || session_restart || options.smoke_single_instance_executable || startup_behavior) &&
        !options.test_config_root.has_value())
        throw std::invalid_argument{"the restart smoke requires its test config root"};
    if ((preferences_restart || session_restart || options.smoke_single_instance_executable || startup_behavior) &&
        options.initial_scenario != InitialShellScenario::Empty)
        throw std::invalid_argument{"the restart smoke requires the empty scenario"};
    if ((options.smoke_startup_source_precedence || options.smoke_diagnostics) && options.source_paths.isEmpty())
        throw std::invalid_argument{"the startup source precedence smoke requires a source"};
    if (options.smoke_start_minimized_no_tray && !options.source_paths.isEmpty())
        throw std::invalid_argument{"the no-tray startup smoke requires no source"};
    if (options.smoke_session_receipts_without_restore && !options.source_paths.isEmpty())
        throw std::invalid_argument{"the session receipt smoke requires no source"};
    if (options.smoke_downloads && !options.source_paths.isEmpty())
        throw std::invalid_argument{"the download composition smoke requires no source"};
    return options;
}

InitialWindowDisposition resolveInitialWindowDisposition(const bool start_minimized, const bool tray_available,
                                                         const bool has_explicit_sources) noexcept {
    if (!start_minimized || has_explicit_sources)
        return InitialWindowDisposition::Show;
    if (tray_available)
        return InitialWindowDisposition::HideToTray;
    return InitialWindowDisposition::ShowWithTrayUnavailableNotice;
}

QStringList resolveShellSourcePaths(const QStringList& source_paths, const QString& working_directory) {
    if (!music_player::m0::isFullyQualifiedFilesystemPath(working_directory))
        throw std::invalid_argument{"the shell working directory must be fully qualified"};

    const QDir base{QDir::cleanPath(working_directory)};
    QStringList resolved;
    resolved.reserve(source_paths.size());
    for (const QString& source_path : source_paths) {
        if (source_path.isEmpty() || source_path.contains(QChar::Null))
            throw std::invalid_argument{"a shell source path cannot be empty"};
        if (source_path.startsWith(QStringLiteral(":/")))
            throw std::invalid_argument{"Qt resource paths are not shell filesystem sources"};
#ifdef Q_OS_WIN
        const bool drive_relative = source_path.size() >= 2 && source_path.at(1) == QLatin1Char(':') &&
                                    (source_path.size() == 2 ||
                                     (source_path.at(2) != QLatin1Char('/') && source_path.at(2) != QLatin1Char('\\')));
        if (drive_relative)
            throw std::invalid_argument{"drive-relative shell source paths are ambiguous"};

        if (music_player::m0::isFullyQualifiedFilesystemPath(source_path)) {
            resolved.push_back(source_path);
            continue;
        }

        const bool starts_with_separator =
            source_path.startsWith(QLatin1Char('/')) || source_path.startsWith(QLatin1Char('\\'));
        if (starts_with_separator) {
            const bool starts_with_two_separators = source_path.size() >= 2 && (source_path.at(1) == QLatin1Char('/') ||
                                                                                source_path.at(1) == QLatin1Char('\\'));
            if (starts_with_two_separators)
                throw std::invalid_argument{"the rooted shell source path requires the invoking drive"};
            resolved.push_back(windowsVolumeRoot(working_directory) + source_path);
            continue;
        }
#else
        if (music_player::m0::isFullyQualifiedFilesystemPath(source_path)) {
            resolved.push_back(source_path);
            continue;
        }
#endif
        resolved.push_back(QDir::cleanPath(base.absoluteFilePath(source_path)));
    }
    return resolved;
}

void validateShellLaunchOptions(const ShellLaunchOptions& options, const bool test_hooks_enabled,
                                const bool combined_audio_enabled) {
    if (options.diagnostics_enabled && !combined_audio_enabled)
        throw std::invalid_argument{"diagnostics requires the combined audio application"};
    if (!test_hooks_enabled &&
        (options.scenario_control_requested || options.smoke_error_recovery || options.smoke_tray_lifecycle ||
         options.smoke_combined_startup || options.smoke_preferences_restart_write ||
         options.smoke_preferences_restart_read || options.smoke_session_restart_read ||
         options.smoke_session_crash_before_primary_commit || options.smoke_session_recovery_repair ||
         options.smoke_session_defaults_read || options.smoke_session_receipts_without_restore ||
         options.smoke_downloads || options.smoke_single_instance_executable || options.smoke_start_minimized_no_tray ||
         options.smoke_startup_source_precedence || options.smoke_diagnostics ||
         options.test_config_root.has_value())) {
        throw std::invalid_argument{"M0 shell scenario controls are unavailable in this build"};
    }
    if (!combined_audio_enabled && options.smoke_combined_startup)
        throw std::invalid_argument{"the combined startup smoke requires audio support"};
    if (!combined_audio_enabled && options.smoke_single_instance_executable)
        throw std::invalid_argument{"the single-instance executable smoke requires audio support"};
    if (!combined_audio_enabled && (options.smoke_startup_source_precedence || options.smoke_diagnostics))
        throw std::invalid_argument{"the startup source precedence smoke requires audio support"};
    if (!combined_audio_enabled && !options.source_paths.isEmpty())
        throw std::invalid_argument{"shell source paths require audio support"};
    if (!combined_audio_enabled &&
        (options.smoke_session_restart_read || options.smoke_session_crash_before_primary_commit ||
         options.smoke_session_recovery_repair || options.smoke_session_defaults_read ||
         options.smoke_session_receipts_without_restore || options.smoke_downloads))
        throw std::invalid_argument{"the session restart smoke requires audio support"};
}

} // namespace music_player::m0::experiment
