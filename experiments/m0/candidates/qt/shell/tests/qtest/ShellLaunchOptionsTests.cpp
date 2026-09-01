#include "ShellLaunchOptions.hpp"

#include <QDir>
#include <QTest>

#include <stdexcept>

using music_player::m0::experiment::InitialShellScenario;
using music_player::m0::experiment::parseShellLaunchOptions;
using music_player::m0::experiment::validateShellLaunchOptions;

namespace {

[[nodiscard]] QString testConfigRoot(const QString& suffix) {
    return QDir{QDir::tempPath()}.filePath(QStringLiteral("music player ") + suffix);
}

[[nodiscard]] QString testConfigArgument(const QString& suffix) {
    return QStringLiteral("--m0-test-config-root=") + testConfigRoot(suffix);
}

} // namespace

class ShellLaunchOptionsTests final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsToTheEmptyScenario() {
        const auto options = parseShellLaunchOptions({QStringLiteral("m0_qt_shell")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
        QVERIFY(!options.scenario_control_requested);
        QVERIFY(!options.smoke_error_recovery);
        QVERIFY(!options.smoke_tray_lifecycle);
        QVERIFY(!options.smoke_combined_startup);
        QVERIFY(!options.smoke_preferences_restart_write);
        QVERIFY(!options.smoke_preferences_restart_read);
        QVERIFY(!options.smoke_session_restart_read);
        QVERIFY(!options.smoke_session_crash_before_primary_commit);
        QVERIFY(!options.smoke_session_recovery_repair);
        QVERIFY(!options.smoke_session_defaults_read);
        QVERIFY(!options.test_config_root.has_value());
    }

    void acceptsTheClosedUnavailableScenario() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Unavailable);
        QVERIFY(!options.smoke_error_recovery);
        QVERIFY(!options.smoke_tray_lifecycle);
        QVERIFY(!options.smoke_combined_startup);
    }

    void acceptsTheExplicitEmptyScenario() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=empty")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
        QVERIFY(options.scenario_control_requested);
        QVERIFY(!options.smoke_error_recovery);
        QVERIFY(!options.smoke_tray_lifecycle);
        QVERIFY(!options.smoke_combined_startup);
    }

    void acceptsTheClosedErrorRecoverySmoke_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("scenario before smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable"),
                           QStringLiteral("--m0-smoke=error-recovery")};
        QTest::newRow("scenario after smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=error-recovery"),
                           QStringLiteral("--m0-scenario=unavailable")};
    }

    void acceptsTheClosedErrorRecoverySmoke() {
        QFETCH(QStringList, arguments);
        const auto options = parseShellLaunchOptions(arguments);
        QCOMPARE(options.initial_scenario, InitialShellScenario::Unavailable);
        QVERIFY(options.smoke_error_recovery);
        QVERIFY(!options.smoke_tray_lifecycle);
        QVERIFY(!options.smoke_combined_startup);
    }

    void acceptsTheClosedTrayLifecycleSmoke_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("implicit empty scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=tray-lifecycle")};
        QTest::newRow("explicit empty before smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=empty"),
                           QStringLiteral("--m0-smoke=tray-lifecycle")};
        QTest::newRow("explicit empty after smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=tray-lifecycle"),
                           QStringLiteral("--m0-scenario=empty")};
    }

    void acceptsTheClosedTrayLifecycleSmoke() {
        QFETCH(QStringList, arguments);
        try {
            const auto options = parseShellLaunchOptions(arguments);
            QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
            QVERIFY(!options.smoke_error_recovery);
            QVERIFY(options.smoke_tray_lifecycle);
            QVERIFY(!options.smoke_combined_startup);
        } catch (const std::invalid_argument& error) {
            QFAIL(error.what());
        }
    }

    void acceptsTheCombinedStartupSmoke() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=combined-startup")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
        QVERIFY(!options.smoke_error_recovery);
        QVERIFY(!options.smoke_tray_lifecycle);
        QVERIFY(options.smoke_combined_startup);
    }

    void acceptsClosedPreferencesRestartHooks_data() {
        QTest::addColumn<QString>("phase");
        QTest::newRow("write") << QStringLiteral("preferences-restart-write");
        QTest::newRow("read") << QStringLiteral("preferences-restart-read");
    }

    void acceptsClosedPreferencesRestartHooks() {
        QFETCH(QString, phase);
        const QString root = testConfigRoot(QStringLiteral("temporary preferences"));
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=") + phase,
                                     QStringLiteral("--m0-test-config-root=") + root});
        QCOMPARE(options.smoke_preferences_restart_write, phase.endsWith(QStringLiteral("write")));
        QCOMPARE(options.smoke_preferences_restart_read, phase.endsWith(QStringLiteral("read")));
        QCOMPARE(options.test_config_root, std::optional<QString>{QDir::cleanPath(root)});
        validateShellLaunchOptions(options, true, true);
    }

    void acceptsClosedSessionRestartHookOnlyForTheCombinedShell() {
        const QString root = testConfigRoot(QStringLiteral("temporary session"));
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-restart-read"),
                                     QStringLiteral("--m0-test-config-root=") + root});
        QVERIFY(options.smoke_session_restart_read);
        QCOMPARE(options.test_config_root, std::optional<QString>{QDir::cleanPath(root)});
        validateShellLaunchOptions(options, true, true);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, true, false));
    }

    void acceptsClosedSessionRecoveryHooksOnlyForTheCombinedShell_data() {
        QTest::addColumn<QString>("smoke");
        QTest::newRow("forced termination before primary commit")
            << QStringLiteral("session-crash-before-primary-commit");
        QTest::newRow("corrupt primary repair") << QStringLiteral("session-recovery-repair");
        QTest::newRow("invalid slots use defaults") << QStringLiteral("session-defaults-read");
    }

    void acceptsClosedSessionRecoveryHooksOnlyForTheCombinedShell() {
        QFETCH(QString, smoke);
        const QString root = testConfigRoot(QStringLiteral("session recovery"));
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=") + smoke,
                                     QStringLiteral("--m0-test-config-root=") + root});
        QCOMPARE(options.smoke_session_crash_before_primary_commit,
                 smoke == QStringLiteral("session-crash-before-primary-commit"));
        QCOMPARE(options.smoke_session_recovery_repair, smoke == QStringLiteral("session-recovery-repair"));
        QCOMPARE(options.smoke_session_defaults_read, smoke == QStringLiteral("session-defaults-read"));
        QCOMPARE(options.test_config_root, std::optional<QString>{QDir::cleanPath(root)});
        validateShellLaunchOptions(options, true, true);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, true, false));
    }

    void acceptsIsolatedConfigForEveryExecutableSmoke_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("error recovery") << QStringList{
            QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable"),
            QStringLiteral("--m0-smoke=error-recovery"), testConfigArgument(QStringLiteral("isolated error"))};
        QTest::newRow("tray lifecycle") << QStringList{QStringLiteral("m0_qt_shell"),
                                                       QStringLiteral("--m0-smoke=tray-lifecycle"),
                                                       testConfigArgument(QStringLiteral("isolated tray"))};
        QTest::newRow("combined startup")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=combined-startup"),
                           testConfigArgument(QStringLiteral("isolated combined"))};
    }

    void acceptsIsolatedConfigForEveryExecutableSmoke() {
        QFETCH(QStringList, arguments);
        const auto options = parseShellLaunchOptions(arguments);
        QVERIFY(options.test_config_root.has_value());
        validateShellLaunchOptions(options, true, true);
    }

    void rejectsEveryClosedControlInProduction_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("explicit default scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=empty")};
        QTest::newRow("scenario") << QStringList{QStringLiteral("m0_qt_shell"),
                                                 QStringLiteral("--m0-scenario=unavailable")};
        QTest::newRow("existing smoke") << QStringList{QStringLiteral("m0_qt_shell"),
                                                       QStringLiteral("--m0-smoke=tray-lifecycle")};
        QTest::newRow("restart write") << QStringList{QStringLiteral("m0_qt_shell"),
                                                      QStringLiteral("--m0-smoke=preferences-restart-write"),
                                                      testConfigArgument(QStringLiteral("temporary write"))};
        QTest::newRow("restart read") << QStringList{QStringLiteral("m0_qt_shell"),
                                                     QStringLiteral("--m0-smoke=preferences-restart-read"),
                                                     testConfigArgument(QStringLiteral("temporary read"))};
        QTest::newRow("session restart read")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-restart-read"),
                           testConfigArgument(QStringLiteral("temporary session"))};
        QTest::newRow("session crash") << QStringList{QStringLiteral("m0_qt_shell"),
                                                      QStringLiteral("--m0-smoke=session-crash-before-primary-commit"),
                                                      testConfigArgument(QStringLiteral("temporary crash"))};
        QTest::newRow("session recovery repair")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-recovery-repair"),
                           testConfigArgument(QStringLiteral("temporary repair"))};
        QTest::newRow("session defaults")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-defaults-read"),
                           testConfigArgument(QStringLiteral("temporary defaults"))};
    }

    void rejectsEveryClosedControlInProduction() {
        QFETCH(QStringList, arguments);
        const auto options = parseShellLaunchOptions(arguments);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, false, true));
    }

    void rejectsMalformedM0Options_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("missing scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=")};
        QTest::newRow("unknown scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=playing")};
        QTest::newRow("duplicate scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=empty"),
                           QStringLiteral("--m0-scenario=unavailable")};
        QTest::newRow("unknown smoke") << QStringList{QStringLiteral("m0_qt_shell"),
                                                      QStringLiteral("--m0-smoke=other")};
        QTest::newRow("missing smoke") << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=")};
        QTest::newRow("smoke without unavailable scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=error-recovery")};
        QTest::newRow("error recovery with empty before smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=empty"),
                           QStringLiteral("--m0-smoke=error-recovery")};
        QTest::newRow("error recovery with empty after smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=error-recovery"),
                           QStringLiteral("--m0-scenario=empty")};
        QTest::newRow("tray lifecycle with unavailable before smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable"),
                           QStringLiteral("--m0-smoke=tray-lifecycle")};
        QTest::newRow("tray lifecycle with unavailable after smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=tray-lifecycle"),
                           QStringLiteral("--m0-scenario=unavailable")};
        QTest::newRow("combined startup with unavailable scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=combined-startup"),
                           QStringLiteral("--m0-scenario=unavailable")};
        QTest::newRow("duplicate smoke") << QStringList{
            QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable"),
            QStringLiteral("--m0-smoke=error-recovery"), QStringLiteral("--m0-smoke=error-recovery")};
        QTest::newRow("duplicate tray lifecycle smoke")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=tray-lifecycle"),
                           QStringLiteral("--m0-smoke=tray-lifecycle")};
        QTest::newRow("error recovery and tray lifecycle smokes")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable"),
                           QStringLiteral("--m0-smoke=error-recovery"), QStringLiteral("--m0-smoke=tray-lifecycle")};
        QTest::newRow("tray lifecycle and error recovery smokes")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=tray-lifecycle"),
                           QStringLiteral("--m0-smoke=error-recovery")};
        QTest::newRow("restart without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=preferences-restart-write")};
        QTest::newRow("session restart without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-restart-read")};
        QTest::newRow("session crash without root") << QStringList{
            QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-crash-before-primary-commit")};
        QTest::newRow("session recovery without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-recovery-repair")};
        QTest::newRow("session defaults without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=session-defaults-read")};
        QTest::newRow("root without restart")
            << QStringList{QStringLiteral("m0_qt_shell"), testConfigArgument(QStringLiteral("without restart"))};
        QTest::newRow("relative root") << QStringList{QStringLiteral("m0_qt_shell"),
                                                      QStringLiteral("--m0-smoke=preferences-restart-read"),
                                                      QStringLiteral("--m0-test-config-root=relative")};
        QTest::newRow("empty root") << QStringList{QStringLiteral("m0_qt_shell"),
                                                   QStringLiteral("--m0-smoke=preferences-restart-read"),
                                                   QStringLiteral("--m0-test-config-root=")};
        QTest::newRow("duplicate root") << QStringList{
            QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=preferences-restart-read"),
            testConfigArgument(QStringLiteral("one")), testConfigArgument(QStringLiteral("two"))};
    }

    void rejectsMalformedM0Options() {
        QFETCH(QStringList, arguments);
        bool rejected = false;
        try {
            const auto unexpected = parseShellLaunchOptions(arguments);
            Q_UNUSED(unexpected);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        QVERIFY(rejected);
    }
};

QTEST_GUILESS_MAIN(ShellLaunchOptionsTests)

#include "ShellLaunchOptionsTests.moc"
