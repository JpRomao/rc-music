#include "adapters/QtJsonWindowsPreferencesStore.hpp"
#include "adapters/QtJsonWindowsSessionStore.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <Sddl.h>

using namespace music_player::m0;
namespace {
QString configured_shell, none_shell, off_shell;

bool privateDocuments(const QString& path) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token{raw};
    DWORD size{};
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    std::vector<unsigned char> user(size);
    if (user.empty() || !GetTokenInformation(token.get(), TokenUser, user.data(), size, &size))
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid))
        return false;
    const auto release_sid = qScopeGuard([&] { LocalFree(sid); });
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto release_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != FALSE;
}

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
        QVERIFY(privateDocuments(root.filePath(QStringLiteral("private"))));
        for (const auto* name : {"network", "media", "staging", "published"})
            QVERIFY(QDir{}.mkdir(root.filePath(QLatin1StringView{name})));
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
            QTRY_VERIFY_WITH_TIMEOUT(!observation(output, "finished").isEmpty() ||
                                         !observation(output, "failure").isEmpty() ||
                                         app.state() == QProcess::NotRunning,
                                     30'000);
            QVERIFY2(observation(output, "failure").isEmpty(), output.constData());
            const auto finished = observation(output, "finished");
            QVERIFY2(!finished.isEmpty(), output.constData());
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
            WindowsDownloadCheckpointStore checkpoint_store{root.filePath(QStringLiteral("private"))};
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
            QCOMPARE(directoryEntries(root.filePath(QStringLiteral("published"))).size(), 1);
            for (const auto* name : {"network", "media", "staging"})
                QVERIFY2(directoryEntries(root.filePath(QLatin1StringView{name})).isEmpty(), name);
        }
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
        for (const auto* name : {"private", "network", "media", "staging", "published"})
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
