#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

class PreferencesRestartTests final : public QObject {
    Q_OBJECT

  public:
    explicit PreferencesRestartTests(QString shell_executable) : shell_executable_(std::move(shell_executable)) {}

  private slots:
    void writesAllPreferencesAndLoadsTheInitialThemeInASecondProcess() {
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player restart 日本-XXXXXX")};
        QVERIFY(config_root.isValid());

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("QSG_RHI_BACKEND"), QStringLiteral("software"));
        environment.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("Basic"));

        const auto run = [&](const QString& phase) {
            QProcess process;
            process.setProcessEnvironment(environment);
            process.setProgram(shell_executable_);
            process.setArguments(
                {QStringLiteral("--m0-smoke=") + phase, QStringLiteral("--m0-test-config-root=") + config_root.path()});
            process.start();
            QVERIFY2(process.waitForStarted(5'000), qPrintable(process.errorString()));
            QVERIFY2(process.waitForFinished(10000), qPrintable(process.errorString()));
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        };

        run(QStringLiteral("preferences-restart-write"));
        QVERIFY(QFileInfo::exists(config_root.filePath(QStringLiteral("preferences.json"))));
        run(QStringLiteral("preferences-restart-read"));
    }

  private:
    QString shell_executable_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 2)
        return 2;
    PreferencesRestartTests tests(arguments.at(1));
    int test_argc = 1;
    char* test_argv[] = {argv[0], nullptr};
    return QTest::qExec(&tests, test_argc, test_argv);
}

#include "PreferencesRestartTests.moc"
