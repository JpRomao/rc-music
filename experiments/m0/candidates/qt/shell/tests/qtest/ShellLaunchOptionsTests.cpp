#include "ShellLaunchOptions.hpp"

#include <QTest>

#include <stdexcept>

using music_player::m0::experiment::InitialShellScenario;
using music_player::m0::experiment::parseShellLaunchOptions;

class ShellLaunchOptionsTests final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsToTheEmptyScenario() {
        const auto options = parseShellLaunchOptions({QStringLiteral("m0_qt_shell")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
        QVERIFY(!options.smoke_error_recovery);
        QVERIFY(!options.smoke_tray_lifecycle);
        QVERIFY(!options.smoke_combined_startup);
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
