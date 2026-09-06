#include "ShellLaunchOptions.hpp"

#include <QDir>
#include <QTest>

#include <stdexcept>

using music_player::m0::experiment::InitialShellScenario;
using music_player::m0::experiment::InitialWindowDisposition;
using music_player::m0::experiment::parseShellLaunchOptions;
using music_player::m0::experiment::resolveInitialWindowDisposition;
using music_player::m0::experiment::resolveShellSourcePaths;
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
    void diagnosticsRequireExplicitOptInAndAreNotSourcePaths() {
        QVERIFY(!parseShellLaunchOptions({QStringLiteral("app")}).diagnostics_enabled);
        const auto options = parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--diagnostics")});
        QVERIFY(options.diagnostics_enabled);
        QVERIFY(options.source_paths.isEmpty());
        validateShellLaunchOptions(options, false, true);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, false, false));
    }

    void rejectsDuplicateAndMalformedDiagnosticOptions() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 (void)parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--diagnostics"),
                                                                QStringLiteral("--diagnostics")}));
        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            (void)parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--diagnostics=true")}));
    }

    void diagnosticFlagCanBeEscapedAsAFileName() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--"), QStringLiteral("--diagnostics")});
        QVERIFY(!options.diagnostics_enabled);
        QCOMPARE(options.source_paths, QStringList{QStringLiteral("--diagnostics")});
    }

    void diagnosticSmokeRequiresIsolatedAudioAndIsUnavailableInProduction() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--m0-smoke=diagnostics"),
                                     testConfigArgument(QStringLiteral("diagnostics")), QStringLiteral("test.wav")});
        QVERIFY(options.smoke_diagnostics);
        validateShellLaunchOptions(options, true, true);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, false, true));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, true, false));
        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            (void)parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--m0-smoke=diagnostics")}));
        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            (void)parseShellLaunchOptions({QStringLiteral("app"), QStringLiteral("--m0-smoke=diagnostics"),
                                           testConfigArgument(QStringLiteral("diagnostics"))}));
    }

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
        QVERIFY(!options.smoke_single_instance_executable);
        QVERIFY(!options.smoke_start_minimized_no_tray);
        QVERIFY(!options.smoke_startup_source_precedence);
        QVERIFY(!options.test_config_root.has_value());
        QVERIFY(options.source_paths.isEmpty());
    }

    void resolvesTheInitialWindowDisposition_data() {
        QTest::addColumn<bool>("start_minimized");
        QTest::addColumn<bool>("tray_available");
        QTest::addColumn<bool>("has_explicit_sources");
        QTest::addColumn<InitialWindowDisposition>("expected");

        QTest::newRow("preference off") << false << true << false << InitialWindowDisposition::Show;
        QTest::newRow("start hidden with tray") << true << true << false << InitialWindowDisposition::HideToTray;
        QTest::newRow("tray unavailable")
            << true << false << false << InitialWindowDisposition::ShowWithTrayUnavailableNotice;
        QTest::newRow("explicit source wins") << true << true << true << InitialWindowDisposition::Show;
        QTest::newRow("explicit source also wins without tray")
            << true << false << true << InitialWindowDisposition::Show;
    }

    void resolvesTheInitialWindowDisposition() {
        QFETCH(bool, start_minimized);
        QFETCH(bool, tray_available);
        QFETCH(bool, has_explicit_sources);
        QFETCH(InitialWindowDisposition, expected);

        QCOMPARE(resolveInitialWindowDisposition(start_minimized, tray_available, has_explicit_sources), expected);
    }

    void capturesSourcePathsAsOrderedData() {
        const QString unicode = QStringLiteral("C:/Música 日本/abertura 01.mp3");
        const QString metacharacters = QStringLiteral("C:/data/faixa 'aspas' & calc.exe;.flac");
        const QString long_path = QStringLiteral("C:/") + QString(320, QLatin1Char('a')) + QStringLiteral(".wav");

        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), unicode, metacharacters, long_path});

        QCOMPARE(options.source_paths, QStringList({unicode, metacharacters, long_path}));
        validateShellLaunchOptions(options, false, true);
    }

    void rejectsProductPathsWhenAudioSupportIsUnavailable() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("C:/music/song.mp3")});
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, false, false));
    }

    void resolvesRelativeSourcesAgainstTheInvokingWorkingDirectory() {
        const QString working_directory = QDir::cleanPath(testConfigRoot(QStringLiteral("invocation cwd")));
        const QString absolute = QDir{working_directory}.absoluteFilePath(QStringLiteral("já absoluto.mp3"));
        const QStringList sources{QStringLiteral("subdir/../faixa 01.mp3"), absolute};

        QCOMPARE(resolveShellSourcePaths(sources, working_directory),
                 QStringList({QDir{working_directory}.absoluteFilePath(QStringLiteral("faixa 01.mp3")),
                              QDir::cleanPath(absolute)}));
    }

    void rejectsQResourceSourcesBeforeIpc() {
        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            (void)resolveShellSourcePaths({QStringLiteral(":/audio/faixa.wav")}, QDir::currentPath()));
    }

    void rejectsQResourceAsTheInvokingWorkingDirectory() {
        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            (void)resolveShellSourcePaths({QStringLiteral("faixa.wav")}, QStringLiteral(":/invoking/cwd")));
    }

    void rejectsAnEmptySourceArgument() {
        const auto options = parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QString{}});
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 (void)resolveShellSourcePaths(options.source_paths, QDir::currentPath()));
    }

    void treatsReservedLookingArgumentsAfterTheDelimiterAsPaths() {
        const auto options =
            parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), QStringLiteral("--"),
                                     QStringLiteral("--m0-smoke=not-a-control.mp3"), QStringLiteral("next.wav")});
        QCOMPARE(options.source_paths,
                 QStringList({QStringLiteral("--m0-smoke=not-a-control.mp3"), QStringLiteral("next.wav")}));
        QVERIFY(!options.smoke_error_recovery);
    }

    void preservesAbsolutePathSpelling() {
#ifdef Q_OS_WIN
        const QString extended = QStringLiteral(R"(\\?\C:\Música\very long\..\track.mp3)");
#else
        const QString extended = QStringLiteral("/Música/very long/../track.mp3");
#endif
        QCOMPARE(resolveShellSourcePaths({extended}, QDir::rootPath()), QStringList({extended}));
    }

#ifdef Q_OS_WIN
    void resolvesRootedOnlySourcesAgainstTheInvokingDrive() {
        const QString working_directory = QStringLiteral("D:/invoking/process/cwd");

        QCOMPARE(
            resolveShellSourcePaths({QStringLiteral(R"(\album\faixa 01.wav)"), QStringLiteral("/Música/faixa 02.wav")},
                                    working_directory),
            QStringList({QStringLiteral(R"(D:\album\faixa 01.wav)"), QStringLiteral("D:/Música/faixa 02.wav")}));
    }

    void resolvesRootedOnlySourcesAgainstTheInvokingUncShare() {
        const QString working_directory = QStringLiteral(R"(\\servidor\share\invoking\cwd)");

        QCOMPARE(resolveShellSourcePaths({QStringLiteral(R"(\album\faixa.wav)")}, working_directory),
                 QStringList({QStringLiteral(R"(\\servidor\share\album\faixa.wav)")}));
    }

    void preservesFullyQualifiedWindowsPathSpelling_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("drive") << QStringLiteral(R"(C:\Música\..\faixa.wav)");
        QTest::newRow("UNC") << QStringLiteral(R"(\\servidor\Música\..\faixa.wav)");
        QTest::newRow("UNC with forward separators") << QStringLiteral("//servidor/Música/../faixa.wav");
        QTest::newRow("extended drive") << QStringLiteral(R"(\\?\C:\Música\..\faixa.wav)");
        QTest::newRow("extended UNC") << QStringLiteral(R"(\\?\UNC\servidor\Música\..\faixa.wav)");
    }

    void preservesFullyQualifiedWindowsPathSpelling() {
        QFETCH(QString, path);
        QCOMPARE(resolveShellSourcePaths({path}, QStringLiteral("D:/invoking/process/cwd")), QStringList({path}));
    }

    void rejectsWin32DeviceAndPseudoExtendedSourcesBeforeIpc_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("Win32 physical device") << QStringLiteral(R"(\\.\PhysicalDrive0)");
        QTest::newRow("Win32 named pipe") << QStringLiteral(R"(\\.\pipe\audio.wav)");
        QTest::newRow("object manager alias") << QStringLiteral(R"(\\??\C:\audio.wav)");
        QTest::newRow("forward-slash device namespace") << QStringLiteral("//./pipe/audio.wav");
        QTest::newRow("forward-slash extended prefix") << QStringLiteral("//?/C:/audio.wav");
        QTest::newRow("extended drive with slash") << QStringLiteral(R"(\\?\C:/audio.wav)");
        QTest::newRow("extended UNC with slash") << QStringLiteral(R"(\\?\UNC/servidor/share/audio.wav)");
    }

    void rejectsWin32DeviceAndPseudoExtendedSourcesBeforeIpc() {
        QFETCH(QString, path);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 (void)resolveShellSourcePaths({path}, QStringLiteral("D:/invoking/process/cwd")));
    }

    void rejectsAmbiguousDriveRelativePaths() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, (void)resolveShellSourcePaths(
                                                            {QStringLiteral("C:relative.mp3")}, QDir::currentPath()));
    }
#endif

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

    void acceptsTheClosedSingleInstanceExecutableHookOnlyForTheCombinedShell() {
        const QString root = testConfigRoot(QStringLiteral("single instance"));
        const auto options = parseShellLaunchOptions(
            {QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=single-instance-executable"),
             QStringLiteral("--m0-test-config-root=") + root, QStringLiteral("--"),
             QStringLiteral("Música & abertura 日本 (01).wav")});

        QVERIFY(options.smoke_single_instance_executable);
        QCOMPARE(options.test_config_root, std::optional<QString>{QDir::cleanPath(root)});
        QCOMPARE(options.source_paths, QStringList{QStringLiteral("Música & abertura 日本 (01).wav")});
        validateShellLaunchOptions(options, true, true);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, true, false));
    }

    void acceptsClosedStartupBehaviorHooks() {
        const QString root = testConfigRoot(QStringLiteral("startup behavior"));
        const auto no_tray = parseShellLaunchOptions({QStringLiteral("m0_qt_shell"),
                                                      QStringLiteral("--m0-smoke=start-minimized-no-tray"),
                                                      QStringLiteral("--m0-test-config-root=") + root});
        QVERIFY(no_tray.smoke_start_minimized_no_tray);
        QVERIFY(!no_tray.smoke_startup_source_precedence);
        validateShellLaunchOptions(no_tray, true, true);

        const auto explicit_source = parseShellLaunchOptions(
            {QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=startup-source-precedence"),
             QStringLiteral("--m0-test-config-root=") + root, QStringLiteral("--"),
             QStringLiteral("C:/Música/abertura.wav")});
        QVERIFY(!explicit_source.smoke_start_minimized_no_tray);
        QVERIFY(explicit_source.smoke_startup_source_precedence);
        QCOMPARE(explicit_source.source_paths, QStringList{QStringLiteral("C:/Música/abertura.wav")});
        validateShellLaunchOptions(explicit_source, true, true);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(explicit_source, true, false));
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
        QTest::newRow("single instance") << QStringList{QStringLiteral("m0_qt_shell"),
                                                        QStringLiteral("--m0-smoke=single-instance-executable"),
                                                        testConfigArgument(QStringLiteral("isolated single instance"))};
    }

    void acceptsIsolatedConfigForEveryExecutableSmoke() {
        QFETCH(QStringList, arguments);
        const auto options = parseShellLaunchOptions(arguments);
        QVERIFY(options.test_config_root.has_value());
        validateShellLaunchOptions(options, true, true);
    }

    void acceptsStandaloneIsolatedConfigOnlyWithTestHooks_data() {
        QTest::addColumn<bool>("combined_audio_enabled");
        QTest::newRow("fixture shell") << false;
        QTest::newRow("combined shell") << true;
    }

    void acceptsStandaloneIsolatedConfigOnlyWithTestHooks() {
        QFETCH(bool, combined_audio_enabled);
        try {
            const QString root = testConfigRoot(QStringLiteral("manual journey 日本"));
            const auto options = parseShellLaunchOptions(
                {QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-test-config-root=") + root});
            QCOMPARE(options.test_config_root, std::optional<QString>{QDir::cleanPath(root)});
            QCOMPARE(options.initial_scenario, InitialShellScenario::Empty);
            QVERIFY(!options.scenario_control_requested);
            QVERIFY(!options.diagnostics_enabled);
            QVERIFY(!options.smoke_diagnostics);
            QVERIFY(!options.smoke_error_recovery);
            QVERIFY(!options.smoke_tray_lifecycle);
            QVERIFY(!options.smoke_combined_startup);
            QVERIFY(!options.smoke_preferences_restart_write);
            QVERIFY(!options.smoke_preferences_restart_read);
            QVERIFY(!options.smoke_session_restart_read);
            QVERIFY(!options.smoke_session_crash_before_primary_commit);
            QVERIFY(!options.smoke_session_recovery_repair);
            QVERIFY(!options.smoke_session_defaults_read);
            QVERIFY(!options.smoke_single_instance_executable);
            QVERIFY(!options.smoke_start_minimized_no_tray);
            QVERIFY(!options.smoke_startup_source_precedence);
            QVERIFY(options.source_paths.isEmpty());
            validateShellLaunchOptions(options, true, combined_audio_enabled);
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                     validateShellLaunchOptions(options, false, combined_audio_enabled));
        } catch (const std::invalid_argument& error) {
            QFAIL(error.what());
        }
    }

    void standaloneIsolatedConfigPreservesSourcesAndDelimiter() {
        try {
            const QString first = QStringLiteral("C:/Música 日本/abertura & (01).wav");
            const QString escaped = QStringLiteral("--m0-test-config-root=literal.wav");
            const auto options = parseShellLaunchOptions({QStringLiteral("m0_qt_shell"), first,
                                                          testConfigArgument(QStringLiteral("manual sources")),
                                                          QStringLiteral("--"), escaped, first});
            QCOMPARE(options.test_config_root,
                     std::optional<QString>{QDir::cleanPath(testConfigRoot(QStringLiteral("manual sources")))});
            QCOMPARE(options.source_paths, QStringList({first, escaped, first}));
            validateShellLaunchOptions(options, true, true);
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, true, false));
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, validateShellLaunchOptions(options, false, true));
        } catch (const std::invalid_argument& error) {
            QFAIL(error.what());
        }
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
        QTest::newRow("single instance") << QStringList{
            QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=single-instance-executable"),
            testConfigArgument(QStringLiteral("temporary single instance"))};
        QTest::newRow("start minimized without tray")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=start-minimized-no-tray"),
                           testConfigArgument(QStringLiteral("temporary no tray"))};
        QTest::newRow("startup source precedence")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=startup-source-precedence"),
                           testConfigArgument(QStringLiteral("temporary source precedence")), QStringLiteral("--"),
                           QStringLiteral("C:/Música/abertura.wav")};
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
        QTest::newRow("single instance without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=single-instance-executable")};
        QTest::newRow("start minimized without tray and without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=start-minimized-no-tray")};
        QTest::newRow("source precedence without root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=startup-source-precedence"),
                           QStringLiteral("--"), QStringLiteral("C:/Música/abertura.wav")};
        QTest::newRow("source precedence without source")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=startup-source-precedence"),
                           testConfigArgument(QStringLiteral("source precedence without source"))};
        QTest::newRow("no tray hook with a source")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-smoke=start-minimized-no-tray"),
                           testConfigArgument(QStringLiteral("no tray with source")), QStringLiteral("--"),
                           QStringLiteral("C:/Música/abertura.wav")};
        QTest::newRow("standalone relative root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-test-config-root=relative")};
        QTest::newRow("standalone empty root")
            << QStringList{QStringLiteral("m0_qt_shell"), QStringLiteral("--m0-test-config-root=")};
        QTest::newRow("standalone duplicate root")
            << QStringList{QStringLiteral("m0_qt_shell"), testConfigArgument(QStringLiteral("one")),
                           testConfigArgument(QStringLiteral("two"))};
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
