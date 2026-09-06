#include "adapters/QtJsonWindowsPreferencesStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

class DiagnosticExecutableTests final : public QObject {
    Q_OBJECT
  public:
    explicit DiagnosticExecutableTests(QString executable) : executable_(std::move(executable)) {}

  private slots:
    void protectsRealDecoderOutputAndOnlyCreatesLogsAfterOptIn_data() {
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("native_debug");
        QTest::addColumn<bool>("collision");
        QTest::newRow("default off - native callback") << false << false << false;
        QTest::newRow("opt in - native callback") << true << false << false;
        QTest::newRow("default off - Qt callback") << false << true << false;
        QTest::newRow("opt in - Qt callback") << true << true << false;
        QTest::newRow("default off preserves existing files") << false << false << true;
        QTest::newRow("log storage failure does not prevent playback") << true << false << true;
    }

    void protectsRealDecoderOutputAndOnlyCreatesLogsAfterOptIn() {
        QFETCH(bool, enabled);
        QFETCH(bool, native_debug);
        QFETCH(bool, collision);
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/PRIVATE_DIAGNOSTIC_ROOT-日本-XXXXXX")};
        QVERIFY(root.isValid());
        if (collision) {
            QVERIFY(QDir{root.path()}.mkdir(QStringLiteral("diagnostics")));
            QFile unrelated{root.filePath(QStringLiteral("diagnostics/diagnostic-0.log"))};
            QVERIFY(unrelated.open(QIODevice::WriteOnly));
            QCOMPARE(unrelated.write("unrelated user data"), qint64{19});
        }
        // One second of generated, silent mono PCM, no personal/licensed input required.
        QByteArray wave = QByteArray::fromHex(
            "52494646a43e000057415645666d74201000000001000100401f0000803e00000200100064617461803e0000");
        wave.append(QByteArray(16000, '\0'));
        QFile source{root.filePath(QStringLiteral("PRIVATE_DIAGNOSTIC_TRACK.wav"))};
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(wave), wave.size());
        source.close();
        music_player::m0::QtJsonWindowsPreferencesStore preferences{root.path()};
        music_player::m0::Preferences settings;
        settings.muted = true;
        QCOMPARE(preferences.save({.preferences = settings}), music_player::m0::PreferencesSaveStatus::Saved);
        QFile corrupt_session{root.filePath(QStringLiteral("session.json"))};
        QVERIFY(corrupt_session.open(QIODevice::WriteOnly));
        QVERIFY(corrupt_session.write("PRIVATE_DIAGNOSTIC_CORRUPT_SESSION") > 0);
        corrupt_session.close();

        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("QSG_RHI_BACKEND"), QStringLiteral("software"));
        environment.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("qt.multimedia.*=true"));
        if (native_debug)
            environment.insert(QStringLiteral("QT_FFMPEG_DEBUG"), QStringLiteral("1"));
        else
            environment.remove(QStringLiteral("QT_FFMPEG_DEBUG"));
        environment.remove(QStringLiteral("QT_FATAL_WARNINGS"));
        environment.remove(QStringLiteral("QT_FATAL_CRITICALS"));
        QProcess process;
        process.setProcessEnvironment(environment);
        process.setProgram(executable_);
        QStringList arguments{QStringLiteral("--m0-smoke=diagnostics"),
                              QStringLiteral("--m0-test-config-root=") + root.path()};
        if (enabled)
            arguments.push_back(QStringLiteral("--diagnostics"));
        arguments << QStringLiteral("--") << source.fileName();
        process.setArguments(arguments);
        process.start();
        QVERIFY(process.waitForStarted(5000));
        const bool finished = process.waitForFinished(15000);
        if (!finished) {
            process.kill();
            QVERIFY(process.waitForFinished(5000));
        }
        QVERIFY(finished);
        const QByteArray stderr_bytes = process.readAllStandardError();
        if (process.exitStatus() != QProcess::NormalExit)
            qInfo("Diagnostic child exit code: %d", process.exitCode());
        QVERIFY2(process.exitStatus() == QProcess::NormalExit, stderr_bytes.constData());
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.readAllStandardOutput(), QByteArray{"M0_DIAGNOSTICS_DECODER_READY\r\n"});
        QCOMPARE(stderr_bytes, QByteArray{});
        const QDir logs{root.filePath(QStringLiteral("diagnostics"))};
        QCOMPARE(logs.exists(), enabled || collision);
        if (collision) {
            QCOMPARE(logs.entryList(QDir::Files), QStringList{QStringLiteral("diagnostic-0.log")});
            QFile unrelated{logs.filePath(QStringLiteral("diagnostic-0.log"))};
            QVERIFY(unrelated.open(QIODevice::ReadOnly));
            QCOMPARE(unrelated.readAll(), QByteArray{"unrelated user data"});
            return;
        }
        if (!enabled)
            return;
        const auto files = logs.entryList(QDir::Files | QDir::NoDotAndDotDot);
        QVERIFY(!files.isEmpty());
        QVERIFY(files.size() <= 3);
        QByteArray contents;
        for (const auto& name : files) {
            QVERIFY(QRegularExpression{QStringLiteral("^diagnostic-[0-2]\\.log$")}.match(name).hasMatch());
            QFile log{logs.filePath(name)};
            QVERIFY(log.open(QIODevice::ReadOnly));
            QVERIFY(log.size() <= 256 * 1024);
            contents += log.readAll();
        }
        QVERIFY(!contents.contains("PRIVATE_DIAGNOSTIC"));
        QVERIFY(!contents.contains(root.path().toUtf8()));
        QVERIFY(!contents.contains("https://"));
        QVERIFY(contents.contains("event=application_started"));
        QVERIFY(contents.contains("event=application_stopping"));
        QVERIFY(contents.contains("event=session_warning code=corrupt_state"));
    }

  private:
    QString executable_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2)
        return 2;
    DiagnosticExecutableTests tests{application.arguments().at(1)};
    int test_argc = 1;
    char* test_argv[] = {argv[0], nullptr};
    return QTest::qExec(&tests, test_argc, test_argv);
}

#include "DiagnosticExecutableTests.moc"
