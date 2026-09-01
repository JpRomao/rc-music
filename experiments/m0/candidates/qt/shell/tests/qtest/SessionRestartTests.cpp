#include "adapters/QtJsonWindowsSessionStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <utility>

using music_player::m0::LocalQueueOccurrenceId;
using music_player::m0::QtJsonWindowsSessionStore;
using music_player::m0::SessionSaveRequest;
using music_player::m0::SessionSaveStatus;
using music_player::m0::SessionSnapshot;

class SessionRestartTests final : public QObject {
    Q_OBJECT

  public:
    SessionRestartTests(QString shell_executable, QString audio_fixture)
        : shell_executable_(std::move(shell_executable)), audio_fixture_(std::move(audio_fixture)) {}

  private slots:
    void restoresAUnicodeSessionPausedAtItsConfirmedPosition() {
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player session 日本-XXXXXX")};
        QVERIFY(config_root.isValid());
        const QString unicode_audio = config_root.filePath(QStringLiteral("Abertura 日本語.wav"));
        QVERIFY(QFile::copy(audio_fixture_, unicode_audio));

        const SessionSnapshot snapshot{
            .queue =
                {
                    .items = {{{77}, std::filesystem::path{unicode_audio.toStdWString()}}},
                    .current_occurrence_id = LocalQueueOccurrenceId{77},
                },
            .position_ms = 750,
        };
        QtJsonWindowsSessionStore store{config_root.path()};
        QCOMPARE(store.save(SessionSaveRequest{.snapshot = snapshot}), SessionSaveStatus::Saved);

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("QSG_RHI_BACKEND"), QStringLiteral("software"));
        environment.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("Basic"));

        QProcess process;
        process.setProcessEnvironment(environment);
        process.setProgram(shell_executable_);
        process.setArguments({QStringLiteral("--m0-smoke=session-restart-read"),
                              QStringLiteral("--m0-test-config-root=") + config_root.path()});
        process.start();
        QVERIFY2(process.waitForStarted(5'000), qPrintable(process.errorString()));
        if (!process.waitForFinished(10'000)) {
            process.kill();
            process.waitForFinished(5'000);
            QFAIL("the restored-session shell did not finish before the watchdog");
        }
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    }

  private:
    QString shell_executable_;
    QString audio_fixture_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 3)
        return 2;
    SessionRestartTests tests(arguments.at(1), arguments.at(2));
    int test_argc = 1;
    char* test_argv[] = {argv[0], nullptr};
    return QTest::qExec(&tests, test_argc, test_argv);
}

#include "SessionRestartTests.moc"
