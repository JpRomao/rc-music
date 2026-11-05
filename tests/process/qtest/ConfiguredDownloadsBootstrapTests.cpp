#include "adapters/QtJsonWindowsPreferencesStore.hpp"
#include "adapters/QtJsonWindowsSessionStore.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <cstdio>
#include <functional>
#include <utility>

using namespace music_player::m0;
namespace {
QString configured_shell, none_shell, off_shell;

struct ChildGuard final {
    QProcess& child;
    ~ChildGuard() {
        if (child.state() != QProcess::NotRunning) {
            child.kill();
            static_cast<void>(child.waitForFinished(5'000));
        }
    }
};

QJsonObject observation(const QByteArray& output, const char* stage) {
    for (const auto& line : output.split('\n')) {
        constexpr QByteArrayView prefix{"CONFIGURED_DOWNLOADS "};
        if (!line.startsWith(prefix))
            continue;
        const auto object = QJsonDocument::fromJson(line.sliced(prefix.size())).object();
        if (object.value(QStringLiteral("stage")).toString() == QLatin1StringView{stage})
            return object;
    }
    return {};
}

bool marker(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
}

bool waitForObservation(QProcess& app, QByteArray& output, const std::function<bool()>& ready, const int timeout) {
    QElapsedTimer timer;
    timer.start();
    do {
        output += app.readAllStandardOutput();
        if (ready())
            return true;
        if (app.state() == QProcess::NotRunning)
            break;
        QTest::qWait(5);
    } while (timer.elapsed() < timeout);
    output += app.readAllStandardOutput();
    return ready();
}

bool finishGracefully(QProcess& app, QByteArray& output, const QString& root) {
    const auto path = QDir{root}.filePath(QStringLiteral("configured-downloads-exit"));
    const bool requested = QFile::exists(path) || marker(path);
    const bool stopped = waitForObservation(app, output, [&] { return app.state() == QProcess::NotRunning; }, 15'000);
    if (!stopped) {
        app.kill();
        static_cast<void>(app.waitForFinished(5'000));
        output += app.readAllStandardOutput();
    }
    return requested && stopped;
}

struct ProbeReleaseGuard final {
    QString path;
    bool release() const { return QFile::exists(path) || marker(path); }
    ~ProbeReleaseGuard() { static_cast<void>(release()); }
};

class FixtureMarkerGuard final {
  public:
    explicit FixtureMarkerGuard(QString path)
        : path_(std::move(path)),
          backup_(path_ + QStringLiteral(".fixture-backup-") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
    bool moveAside() {
        QFile original{path_};
        if (QFileInfo{path_}.isSymLink() || !original.open(QIODevice::ReadOnly) ||
            original.readAll() != QByteArray{"RCMusicDownloadTools-v1\n"})
            return false;
        original.close();
        moved_ = QFile::rename(path_, backup_);
        return moved_;
    }
    bool writeCorrupt() {
        QFile file{path_};
        if (!moved_ || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return false;
        corrupted_ = true;
        return file.write(corrupt_bytes_) == corrupt_bytes_.size();
    }
    bool restore() {
        if (!moved_)
            return true;
        if (corrupted_) {
            QFile file{path_};
            if (QFileInfo{path_}.isSymLink() || !file.open(QIODevice::ReadOnly) || file.readAll() != corrupt_bytes_)
                return false;
            file.close();
            if (!QFile::remove(path_))
                return false;
            corrupted_ = false;
        } else if (QFile::exists(path_)) {
            return false;
        }
        if (!QFile::rename(backup_, path_))
            return false;
        moved_ = false;
        return true;
    }
    ~FixtureMarkerGuard() {
        if (!restore())
            std::fputs("Preserved the fixture marker backup after uncertain restoration\n", stderr);
    }

  private:
    QString path_, backup_;
    const QByteArray corrupt_bytes_{"RCMusicDownloadTools-v1\nnot-installed\n"};
    bool moved_{}, corrupted_{};
};

QStringList directoryEntries(const QString& path) {
    return QDir{path}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}
} // namespace

class ConfiguredDownloadsBootstrapTests final : public QObject {
    Q_OBJECT
  private slots:
    void configuredMainUsesRealOwnerExecutionPersistenceAndReplay() {
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/RC Music bootstrap espaço-音-XXXXXX")};
        QVERIFY(root.isValid());
        QVERIFY(!QFile::exists(root.filePath(QStringLiteral("downloads"))));
        Preferences preferences;
        preferences.restore_last_session = false;
        QtJsonWindowsPreferencesStore preferences_store{root.path()};
        QCOMPARE(preferences_store.save({preferences}), PreferencesSaveStatus::Saved);
        std::string original_job;
        for (int run = 0; run < 2; ++run) {
            if (run != 0) {
                QVERIFY(QFile::remove(root.filePath(QStringLiteral("configured-downloads-exit"))));
                QVERIFY(marker(root.filePath(QStringLiteral("configured-downloads-restart"))));
            }
            QProcess app;
            ChildGuard guard{app};
            app.setWorkingDirectory(root.path());
            app.setProcessChannelMode(QProcess::MergedChannels);
            QByteArray output;
            QObject::connect(&app, &QProcess::readyReadStandardOutput, &app,
                             [&] { output += app.readAllStandardOutput(); });
            app.start(configured_shell, {QStringLiteral("--m0-test-config-root=") + root.path()});
            QVERIFY(app.waitForStarted(5'000));
            QTRY_VERIFY_WITH_TIMEOUT(!observation(output, "ready").isEmpty() || app.state() == QProcess::NotRunning,
                                     10'000);
            const auto ready = observation(output, "ready");
            QCOMPARE(ready.value("profileCalls").toInt(), 1);
            QVERIFY2(ready.value("binding").toBool(), output.constData());
            QVERIFY(ready.value("lyrics").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(root.filePath(QStringLiteral("downloads/state"))) ||
                                         app.state() == QProcess::NotRunning,
                                     5'000);
            QTRY_VERIFY_WITH_TIMEOUT(!observation(output, "finished").isEmpty() ||
                                         !observation(output, "failure").isEmpty() ||
                                         app.state() == QProcess::NotRunning,
                                     30'000);
            QVERIFY2(observation(output, "failure").isEmpty(), output.constData());
            const auto finished = observation(output, "finished");
            QVERIFY2(!finished.isEmpty(), output.constData());
            // Finished observes the prepared store and committed job. The
            // earlier state-directory observation alone can see partial setup.
            for (const auto* name : {"state", "network", "media", "youtube", "staging", "published"})
                QVERIFY2(QFileInfo(root.filePath(QStringLiteral("downloads/") + QLatin1StringView{name})).isDir(),
                         output.constData());
            for (const auto* name : {"private", "network", "media", "staging", "published"})
                QVERIFY(!QFile::exists(root.filePath(QLatin1StringView{name})));
            QCOMPARE(finished.value("acks").toInt(), 2);
            QCOMPARE(finished.value("queue").toInt(), run == 0 ? 1 : 0);
            QCOMPARE(finished.value("jobState").toString(),
                     run == 0 ? QStringLiteral("completed") : QStringLiteral("imported"));
            QVERIFY(!finished.value("playing").toBool());
            // Both real persistence adapters must already expose their durable
            // receipts while main, owner and the worker are still alive.
            QtJsonWindowsSessionStore session_store{root.path()};
            const auto session = session_store.load();
            QCOMPARE(session.status, SessionLoadStatus::Loaded);
            QCOMPARE(session.snapshot.download_import_receipts.size(), std::size_t{1});
            const auto& receipt = session.snapshot.download_import_receipts.front();
            QVERIFY(QFile::exists(QString::fromStdWString(receipt.source_path.native())));
            QVERIFY(receipt.source_path.extension() == std::filesystem::path{L".mp3"});
            QVERIFY(QFile{QString::fromStdWString(receipt.source_path.native())}.size() > 128);
            if (run == 0)
                original_job = receipt.job_id;
            QCOMPARE(receipt.job_id, original_job);
            WindowsDownloadCheckpointStore checkpoint_store{root.filePath(QStringLiteral("downloads/state"))};
            const auto checkpoint = checkpoint_store.load();
            QCOMPARE(checkpoint.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(checkpoint.checkpoint);
            QCOMPARE(checkpoint.checkpoint->jobs.size(), std::size_t{1});
            QCOMPARE(checkpoint.checkpoint->receipts.size(), std::size_t{1});
            QCOMPARE(checkpoint.checkpoint->jobs.front().job_id, original_job);
            QVERIFY(marker(root.filePath(QStringLiteral("configured-downloads-exit"))));
            QTRY_COMPARE_WITH_TIMEOUT(app.state(), QProcess::NotRunning, 15'000);
            output += app.readAllStandardOutput();
            QCOMPARE(app.exitStatus(), QProcess::NormalExit);
            QVERIFY2(app.exitCode() == 0, output.constData());
            const auto stopped = observation(output, "stopped");
            QVERIFY(!stopped.isEmpty());
            QVERIFY(stopped.value("clientStopped").toBool());
            QVERIFY(!stopped.value("failed").toBool());
            QCOMPARE(stopped.value("downloadStatus").toString(), QStringLiteral("Downloads encerrados"));
            const auto final_checkpoint = checkpoint_store.load();
            QCOMPARE(final_checkpoint.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(final_checkpoint.checkpoint);
            QCOMPARE(final_checkpoint.checkpoint->jobs.front().state,
                     run == 0 ? DownloadJobState::Completed : DownloadJobState::Imported);
            QCOMPARE(directoryEntries(root.filePath(QStringLiteral("downloads/published"))).size(), 1);
            for (const auto* name : {"network", "media", "youtube", "staging"})
                QVERIFY2(
                    directoryEntries(root.filePath(QStringLiteral("downloads/") + QLatin1StringView{name})).isEmpty(),
                    name);
        }
    }

    void missingOrCorruptInstalledMarkerKeepsThePlayerLocal_data() {
        QTest::addColumn<bool>("corrupt");
        QTest::newRow("missing") << false;
        QTest::newRow("corrupt") << true;
    }
    void missingOrCorruptInstalledMarkerKeepsThePlayerLocal() {
        QFETCH(bool, corrupt);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(marker(root.filePath(QStringLiteral("configured-downloads-local-only"))));
        FixtureMarkerGuard installed{
            QFileInfo{configured_shell}.absoluteDir().filePath(QStringLiteral("download-tools/installed-v1"))};
        QVERIFY(installed.moveAside());
        if (corrupt)
            QVERIFY(installed.writeCorrupt());
        QProcess app;
        ChildGuard guard{app};
        app.setWorkingDirectory(root.path());
        app.setProcessChannelMode(QProcess::MergedChannels);
        QByteArray output;
        QObject::connect(&app, &QProcess::readyReadStandardOutput, &app,
                         [&] { output += app.readAllStandardOutput(); });
        app.start(configured_shell, {QStringLiteral("--m0-test-config-root=") + root.path()});
        QVERIFY(app.waitForStarted(5'000));
        const bool locally_ready =
            waitForObservation(app, output, [&] { return !observation(output, "local-ready").isEmpty(); }, 3'000);
        const bool reader_finished = waitForObservation(
            app, output,
            [&] { return !observation(output, "ready").isEmpty() && !observation(output, "finished").isEmpty(); },
            2'000);
        // The old eager main never invokes the reader. Always close it before
        // asserting the Red, so no owner or fixture handle survives this case.
        const bool gracefully_stopped = finishGracefully(app, output, root.path());
        const bool restored = installed.restore();
        QVERIFY2(gracefully_stopped, output.constData());
        QVERIFY(restored);
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
        QVERIFY2(app.exitCode() == 0, output.constData());
        QVERIFY2(locally_ready, output.constData());
        QVERIFY2(reader_finished, output.constData());
        QVERIFY(QFile::exists(root.filePath(QStringLiteral("configured-downloads-probe-completed"))));
        const auto ready = observation(output, "ready");
        QCOMPARE(ready.value("profileCalls").toInt(), 1);
        QVERIFY(!ready.value("binding").toBool());
        const auto finished = observation(output, "finished");
        QCOMPARE(finished.value("acks").toInt(), 0);
        QCOMPARE(finished.value("queue").toInt(), 0);
        QVERIFY(!finished.value("playing").toBool());
        const auto stopped = observation(output, "stopped");
        QVERIFY(!stopped.isEmpty());
        QVERIFY(!stopped.value("everBinding").toBool());
        QVERIFY(stopped.value("clientStopped").toBool());
        QVERIFY(!stopped.value("failed").toBool());
        QVERIFY(!QFile::exists(root.filePath(QStringLiteral("downloads"))));
    }
    void exitWhileTheComponentProbeIsPendingDrainsWithoutLateBinding() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(marker(root.filePath(QStringLiteral("configured-downloads-local-only"))));
        QVERIFY(marker(root.filePath(QStringLiteral("configured-downloads-slow-component-probe"))));
        QProcess app;
        ChildGuard guard{app};
        ProbeReleaseGuard release{root.filePath(QStringLiteral("configured-downloads-probe-release"))};
        app.setWorkingDirectory(root.path());
        app.setProcessChannelMode(QProcess::MergedChannels);
        QByteArray output;
        QObject::connect(&app, &QProcess::readyReadStandardOutput, &app,
                         [&] { output += app.readAllStandardOutput(); });
        app.start(configured_shell, {QStringLiteral("--m0-test-config-root=") + root.path()});
        QVERIFY(app.waitForStarted(5'000));
        const bool locally_ready =
            waitForObservation(app, output, [&] { return !observation(output, "local-ready").isEmpty(); }, 3'000);
        const bool entered = waitForObservation(
            app, output,
            [&] { return QFile::exists(root.filePath(QStringLiteral("configured-downloads-probe-entered"))); }, 2'000);
        const bool requested = marker(root.filePath(QStringLiteral("configured-downloads-exit")));
        const bool pending =
            waitForObservation(app, output, [&] { return !observation(output, "exit-pending").isEmpty(); }, 2'000);
        // Release the owned worker even when the eager main never called it.
        // All behavior assertions follow the graceful shutdown observation.
        const bool released = release.release();
        const bool gracefully_stopped = finishGracefully(app, output, root.path());
        QVERIFY2(gracefully_stopped, output.constData());
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
        QVERIFY2(app.exitCode() == 0, output.constData());
        QVERIFY(requested);
        QVERIFY(released);
        QVERIFY2(locally_ready, output.constData());
        QVERIFY2(entered, output.constData());
        QVERIFY2(pending, output.constData());
        QVERIFY(!observation(output, "exit-requested").isEmpty());
        QVERIFY(!observation(output, "local-ready").value("binding").toBool());
        const auto stopped = observation(output, "stopped");
        QVERIFY(!stopped.isEmpty());
        QVERIFY(!stopped.value("everBinding").toBool());
        QVERIFY(stopped.value("clientStopped").toBool());
        QVERIFY(!stopped.value("failed").toBool());
        QVERIFY(!QFile::exists(root.filePath(QStringLiteral("downloads"))));
    }
    void noConfiguredHeaderOrExplicitNoneKeepsLocalPlayerWithoutWork_data() {
        QTest::addColumn<QString>("executable");
        QTest::addColumn<int>("calls");
        QTest::newRow("no-header") << off_shell << 0;
        QTest::newRow("explicit-none") << none_shell << 1;
    }
    void noConfiguredHeaderOrExplicitNoneKeepsLocalPlayerWithoutWork() {
        QFETCH(QString, executable);
        QFETCH(int, calls);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QProcess app;
        ChildGuard guard{app};
        app.setProcessChannelMode(QProcess::MergedChannels);
        QByteArray output;
        QObject::connect(&app, &QProcess::readyReadStandardOutput, &app,
                         [&] { output += app.readAllStandardOutput(); });
        app.start(executable, {QStringLiteral("--m0-test-config-root=") + root.path()});
        QVERIFY(app.waitForStarted(5'000));
        QTRY_VERIFY_WITH_TIMEOUT(!observation(output, "finished").isEmpty() || app.state() == QProcess::NotRunning,
                                 10'000);
        const auto ready = observation(output, "ready");
        QCOMPARE(ready.value("profileCalls").toInt(), calls);
        QVERIFY(!ready.value("binding").toBool());
        const auto finished = observation(output, "finished");
        QVERIFY(!finished.isEmpty());
        QCOMPARE(finished.value("acks").toInt(), 0);
        QCOMPARE(finished.value("queue").toInt(), 0);
        QVERIFY(!finished.value("playing").toBool());
        for (const auto* name : {"downloads", "private", "network", "media", "staging", "published"})
            QVERIFY(!QFile::exists(root.filePath(QLatin1StringView{name})));
        QVERIFY(marker(root.filePath(QStringLiteral("configured-downloads-exit"))));
        QTRY_COMPARE_WITH_TIMEOUT(app.state(), QProcess::NotRunning, 10'000);
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
        QVERIFY2(app.exitCode() == 0, output.constData());
    }
};

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    if (application.arguments().size() != 4)
        return 1;
    configured_shell = application.arguments().at(1);
    none_shell = application.arguments().at(2);
    off_shell = application.arguments().at(3);
    ConfiguredDownloadsBootstrapTests tests;
    return QTest::qExec(&tests);
}

#include "ConfiguredDownloadsBootstrapTests.moc"
