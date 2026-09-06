#include <QCoreApplication>
#include <QDir>
#ifdef Q_OS_WIN
#include <QFileInfo>
#endif
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

class ShellExecutableSmokeTests final : public QObject {
    Q_OBJECT

  public:
    ShellExecutableSmokeTests(QString executable, QString smoke, QString scenario)
        : executable_(std::move(executable)), smoke_(std::move(smoke)), scenario_(std::move(scenario)) {}

  private slots:
    void runsWithAnIsolatedTemporaryConfigRoot() {
        QString safe_smoke = smoke_;
        safe_smoke.replace(u'/', u'-');
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player smoke-") + safe_smoke +
                                  QStringLiteral("-日本-XXXXXX")};
        QVERIFY(config_root.isValid());

        QProcess process;
        process.setProgram(executable_);
        process.setArguments({QStringLiteral("--m0-scenario=") + scenario_, QStringLiteral("--m0-smoke=") + smoke_,
                              QStringLiteral("--m0-test-config-root=") + config_root.path()});
        process.start();
        QVERIFY2(process.waitForStarted(5'000), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(15'000), qPrintable(process.errorString()));
        const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());
#ifdef Q_OS_WIN
        if (smoke_ == QStringLiteral("tray-lifecycle")) {
            QVERIFY2(QFileInfo::exists(config_root.filePath(QStringLiteral("preferences.json"))),
                     "Tray smoke did not persist inside its isolated test root.");
        }
#endif
    }

  private:
    QString executable_;
    QString smoke_;
    QString scenario_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 4) {
        qCritical("Expected shell executable, smoke name and scenario arguments.");
        return 2;
    }

    ShellExecutableSmokeTests tests{arguments.at(1), arguments.at(2), arguments.at(3)};
    int test_argc = 1;
    char* test_argv[] = {argv[0], nullptr};
    return QTest::qExec(&tests, test_argc, test_argv);
}

#include "ShellExecutableSmokeTests.moc"
