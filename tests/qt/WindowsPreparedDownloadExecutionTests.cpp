#include "WindowsDownloadExecution.hpp"
#include "WindowsDownloadNetworkLaunch.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <functional>
#include <utility>

using namespace music_player::m0;

namespace {
const DownloadItem item{"https://media.example.test/audio.mp3", "Synthetic audio", DownloadMediaKind::Audio};
const QByteArray sentinel_bytes{"owned-sentinel-preserved"};

QByteArray fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}

bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer watchdog;
    watchdog.start();
    do {
        if (condition())
            return true;
        QTest::qWait(5);
    } while (watchdog.elapsed() < 15000);
    return condition();
}

struct DrainOnExit final {
    WindowsDownloadExecution& execution;
    ~DrainOnExit() {
        execution.shutdown();
        if (!waitFor([&] { return execution.stopped(); }))
            QTest::qFail("Execution did not acknowledge thread teardown before fixture destruction", __FILE__,
                         __LINE__);
    }
};

struct Harness final {
    QTemporaryDir directory{};
    QString runtime{directory.filePath("runtime")};
    QString staging{directory.filePath("staging")};
    QString publication{directory.filePath("published")};

    bool initialize() const {
        if (!directory.isValid())
            return false;
        for (const auto& parent : {runtime, staging, publication}) {
            if (!QDir().mkpath(parent))
                return false;
            QFile sentinel(parent + "/preserve.keep");
            if (!sentinel.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                sentinel.write(sentinel_bytes) != sentinel_bytes.size())
                return false;
        }
        return true;
    }

    WindowsDownloadJobSessionConfig jobConfiguration() const {
        WindowsDownloadJobSessionConfig config;
        config.workspace = {staging, publication};
        return config;
    }

    bool parentsCleanAndPreserved() const {
        for (const auto& parent : {runtime, staging, publication}) {
            if (QDir(parent).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot) !=
                QStringList{"preserve.keep"})
                return false;
            QFile sentinel(parent + "/preserve.keep");
            if (!sentinel.open(QIODevice::ReadOnly) || sentinel.readAll() != sentinel_bytes)
                return false;
        }
        return true;
    }
};

bool realEnabled() {
    return !QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH).isEmpty() &&
           !QString::fromUtf8(RC_MUSIC_TEST_CRT_DIR).isEmpty();
}

WindowsDownloadNetworkLaunchConfig realConfiguration(const QString& runtime_parent) {
    WindowsDownloadNetworkLaunchConfig config;
    config.runtime_parent = runtime_parent;
    const QString crt = QString::fromUtf8(RC_MUSIC_TEST_CRT_DIR);
    const std::array<QString, download_launch_artifact_count> paths{QString::fromUtf8(RC_MUSIC_TEST_BOOTSTRAP_PATH),
                                                                    QString::fromUtf8(RC_MUSIC_TEST_PAYLOAD_PATH),
                                                                    QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH),
                                                                    QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH),
                                                                    crt + "/msvcp140.dll",
                                                                    crt + "/msvcp140_1.dll",
                                                                    crt + "/vcruntime140.dll",
                                                                    crt + "/vcruntime140_1.dll",
                                                                    QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH)};
    for (std::size_t index = 0; index < paths.size(); ++index)
        config.artifacts[index] = {paths[index], fileHash(paths[index])};
    return config;
}

bool completePins(const WindowsDownloadNetworkLaunchConfig& config) {
    for (const auto& artifact : config.artifacts) {
        if (artifact.path.isEmpty() || artifact.sha256.size() != 64)
            return false;
    }
    return true;
}

bool sourcesPreserved(const WindowsDownloadNetworkLaunchConfig& config) {
    for (const auto& artifact : config.artifacts) {
        if (fileHash(artifact.path) != artifact.sha256)
            return false;
    }
    return true;
}

bool terminal(DownloadExecutionState state) {
    return state == DownloadExecutionState::Failed || state == DownloadExecutionState::Cancelled ||
           state == DownloadExecutionState::Published;
}
} // namespace

class WindowsPreparedDownloadExecutionTests final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsEveryIncorrectArtifactPinWithoutRuntimeOrMedia_data() {
        QTest::addColumn<int>("role");
        constexpr std::array names{"bootstrap",    "payload",        "QtCore",
                                   "QtNetwork",    "msvcp140",       "msvcp140_1",
                                   "vcruntime140", "vcruntime140_1", "restricted-TLS-plugin"};
        static_assert(names.size() == download_launch_artifact_count);
        for (std::size_t index = 0; index < names.size(); ++index)
            QTest::newRow(names[index]) << static_cast<int>(index);
    }

    void rejectsEveryIncorrectArtifactPinWithoutRuntimeOrMedia() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QFETCH(int, role);
        Harness h;
        QVERIFY(h.initialize());
        const auto trusted = realConfiguration(h.runtime);
        QVERIFY(completePins(trusted));
        auto invalid = trusted;
        invalid.artifacts[static_cast<std::size_t>(role)].sha256 = QByteArray(64, '0');
        WindowsDownloadExecution execution(std::move(invalid), h.jobConfiguration());
        DrainOnExit drain{execution};
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(execution.start("bad-pin", item));
        DownloadExecutionUpdate result;
        QVERIFY(waitFor([&] {
            result = execution.poll("bad-pin");
            return terminal(result.state);
        }));
        QCOMPARE(result.job_id, std::string{"bad-pin"});
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Unavailable);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(execution.retainedPublicationPath("bad-pin").empty());
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(sourcesPreserved(trusted));
        execution.shutdown();
        QVERIFY(waitFor([&] { return execution.stopped(); }));
        QVERIFY(h.parentsCleanAndPreserved());
    }

    void jobConfigurationFailureCleansPrivateParentsBeforeTerminal_data() {
        QTest::addColumn<bool>("bad_clock_limit");
        QTest::newRow("invalid-total-timeout") << true;
        QTest::newRow("invalid-workspace-budget") << false;
    }

    void jobConfigurationFailureCleansPrivateParentsBeforeTerminal() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QFETCH(bool, bad_clock_limit);
        Harness h;
        QVERIFY(h.initialize());
        const auto launch = realConfiguration(h.runtime);
        QVERIFY(completePins(launch));
        auto job = h.jobConfiguration();
        if (bad_clock_limit)
            job.total_timeout_ms = 0;
        else
            job.workspace.max_workspace_bytes = 0;
        // Observe the typed failure and clean parents, not an intermediate
        // preparation step. A zero timeout is rejected before preparation;
        // this test does not directly observe preparation for a zero budget.
        // Deliberately stop before DNS. The principal's lexical URL contract
        // rejects numeric hosts, and its address policy must not admit loopback
        // merely to manufacture a complete-network success test.
        WindowsDownloadExecution execution(launch, std::move(job));
        DrainOnExit drain{execution};
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(execution.start("bad-job", item));
        DownloadExecutionUpdate result;
        QVERIFY(waitFor([&] {
            result = execution.poll("bad-job");
            return terminal(result.state);
        }));
        QCOMPARE(result.job_id, std::string{"bad-job"});
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(execution.retainedPublicationPath("bad-job").empty());
        // Terminal acknowledges task destruction, not just session failure.
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(sourcesPreserved(launch));
        execution.shutdown();
        QVERIFY(waitFor([&] { return execution.stopped(); }));
        QVERIFY(h.parentsCleanAndPreserved());
    }

    void eachAdmittedJobReauthenticatesSourcesRatherThanReusingThePreviousLease() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        Harness h;
        QVERIFY(h.initialize());
        auto launch = realConfiguration(h.runtime);
        QVERIFY(completePins(launch));
        const auto original = launch;
        const auto own_bootstrap = h.directory.filePath("owned-bootstrap-source.exe");
        QVERIFY(QFile::copy(launch.artifacts[0].path, own_bootstrap));
        launch.artifacts[0].path = own_bootstrap;
        QCOMPARE(fileHash(own_bootstrap), launch.artifacts[0].sha256);
        auto job = h.jobConfiguration();
        job.workspace.max_workspace_bytes = 0; // no DNS/socket/tool can start
        WindowsDownloadExecution execution(launch, std::move(job));
        DrainOnExit drain{execution};
        QVERIFY(execution.start("first-job", item));
        DownloadExecutionUpdate first;
        QVERIFY(waitFor([&] {
            first = execution.poll("first-job");
            return terminal(first.state);
        }));
        QCOMPARE(first.state, DownloadExecutionState::Failed);
        QCOMPARE(first.error, DownloadFailure::InvalidResult);
        QVERIFY(first.published_mp3.empty());
        QVERIFY(execution.retainedPublicationPath("first-job").empty());
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(sourcesPreserved(launch));
        // Mutate only this test-owned regular file after the preceding terminal
        // ACK. The SDK, CRT and separately built plugin remain read-only sources.
        QFile writer(own_bootstrap);
        QVERIFY(writer.open(QIODevice::ReadWrite));
        const auto first_byte = writer.read(1);
        QCOMPARE(first_byte.size(), qsizetype{1});
        QVERIFY(writer.seek(0));
        QCOMPARE(writer.write(QByteArray(1, static_cast<char>(first_byte[0] ^ 1))), qint64{1});
        QVERIFY(writer.flush());
        writer.close();
        const auto altered_hash = fileHash(own_bootstrap);
        QVERIFY(altered_hash.size() == 64 && altered_hash != launch.artifacts[0].sha256);
        QVERIFY(execution.start("second-job", item));
        DownloadExecutionUpdate second;
        QVERIFY(waitFor([&] {
            second = execution.poll("second-job");
            return terminal(second.state);
        }));
        QCOMPARE(second.job_id, std::string{"second-job"});
        QCOMPARE(second.state, DownloadExecutionState::Failed);
        QCOMPARE(second.error, DownloadFailure::Unavailable);
        QVERIFY(second.published_mp3.empty());
        QVERIFY(execution.retainedPublicationPath("second-job").empty());
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(sourcesPreserved(original));
        QCOMPARE(fileHash(own_bootstrap), altered_hash);
        execution.shutdown();
        QVERIFY(waitFor([&] { return execution.stopped(); }));
        QVERIFY(h.parentsCleanAndPreserved());
    }

    void constructorRejectedRequestsAndIdleShutdownDoNotPrepareFiles_data() {
        QTest::addColumn<QString>("url");
        QTest::newRow("numeric-host") << QStringLiteral("http://127.0.0.1/audio.mp3");
        QTest::newRow("local-file") << QStringLiteral("file:///C:/synthetic.mp3");
        QTest::newRow("YouTube-independent") << QStringLiteral("https://youtube.com/audio.mp4");
    }

    void constructorRejectedRequestsAndIdleShutdownDoNotPrepareFiles() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QFETCH(QString, url);
        Harness h;
        QVERIFY(h.initialize());
        const auto launch = realConfiguration(h.runtime);
        QVERIFY(completePins(launch));
        WindowsDownloadExecution execution(launch, h.jobConfiguration());
        DrainOnExit drain{execution};
        QVERIFY(h.parentsCleanAndPreserved());
        const DownloadItem rejected{url.toStdString(), "Synthetic unsupported", DownloadMediaKind::Audio};
        QVERIFY(!execution.start("rejected", rejected));
        QVERIFY(h.parentsCleanAndPreserved());
        execution.shutdown();
        QVERIFY(waitFor([&] { return execution.stopped(); }));
        QVERIFY(!execution.start("after-shutdown", item));
        QVERIFY(h.parentsCleanAndPreserved());
        QVERIFY(sourcesPreserved(launch));
    }
};

QTEST_GUILESS_MAIN(WindowsPreparedDownloadExecutionTests)
#include "WindowsPreparedDownloadExecutionTests.moc"
