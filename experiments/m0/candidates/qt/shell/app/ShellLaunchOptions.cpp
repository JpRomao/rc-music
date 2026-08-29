#include "ShellLaunchOptions.hpp"

#include <stdexcept>

namespace music_player::m0::experiment {

ShellLaunchOptions parseShellLaunchOptions(const QStringList& arguments) {
    ShellLaunchOptions options;
    bool scenario_seen = false;
    bool smoke_seen = false;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (argument.startsWith(QStringLiteral("--m0-scenario="))) {
            if (scenario_seen)
                throw std::invalid_argument{"the M0 shell scenario may be specified only once"};
            scenario_seen = true;
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
            else
                throw std::invalid_argument{"unsupported M0 shell smoke"};
            continue;
        }
        if (argument.startsWith(QStringLiteral("--m0-")))
            throw std::invalid_argument{"unsupported M0 shell option"};
    }
    if (options.smoke_error_recovery && options.initial_scenario != InitialShellScenario::Unavailable)
        throw std::invalid_argument{"the error recovery smoke requires the unavailable scenario"};
    if (options.smoke_tray_lifecycle && options.initial_scenario != InitialShellScenario::Empty)
        throw std::invalid_argument{"the tray lifecycle smoke requires the empty scenario"};
    if (options.smoke_combined_startup && options.initial_scenario != InitialShellScenario::Empty)
        throw std::invalid_argument{"the combined startup smoke requires the empty scenario"};
    return options;
}

} // namespace music_player::m0::experiment
