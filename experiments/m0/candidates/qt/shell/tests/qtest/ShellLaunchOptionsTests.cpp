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
    }

    void acceptsTheClosedUnavailableScenario() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Unavailable);
        QVERIFY(!options.smoke_error_recovery);
    }

    void acceptsTheExplicitEmptyScenario() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=empty")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
    }

    void acceptsTheClosedErrorRecoverySmoke() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-scenario=unavailable"),
                                     QStringLiteral("--m0-smoke=error-recovery")});
        QCOMPARE(options.initial_scenario, InitialShellScenario::Unavailable);
        QVERIFY(options.smoke_error_recovery);
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
        QTest::newRow("smoke without unavailable scenario")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=error-recovery")};
        QTest::newRow("duplicate smoke") << QStringList{QStringLiteral("m0_qt_shell"),
                                                        QStringLiteral("--m0-smoke=error-recovery"),
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
