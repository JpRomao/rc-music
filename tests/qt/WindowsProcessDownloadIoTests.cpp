#include "WindowsProcessDownloadIo.hpp"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;

namespace {
WindowsProcessDownloadIoConfig configuration(const QString& working_directory, bool real_worker = false) {
    WindowsProcessDownloadIoConfig config;
    config.process.executable =
        QString::fromUtf8(real_worker ? RC_MUSIC_TEST_NETWORK_WORKER_PATH : RC_MUSIC_TEST_IO_HELPER_PATH);
    config.process.working_directory = working_directory;
    QProcessEnvironment environment;
    environment.insert("PATH", QFileInfo(QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH)).absolutePath());
    environment.insert("SystemRoot", qEnvironmentVariable("SystemRoot"));
    config.process.environment = environment;
    config.runtime = {QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH),    QByteArray(RC_MUSIC_TEST_TLS_CORE_SHA256),
                      QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH), QByteArray(RC_MUSIC_TEST_TLS_NETWORK_SHA256),
                      QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH),  QByteArray(RC_MUSIC_TEST_TLS_PLUGIN_SHA256)};
    if (config.runtime.plugin_path.isEmpty()) {
        config.runtime.plugin_path = QDir::tempPath() + "/missing-reviewed-plugin.dll";
        config.runtime.plugin_sha256 = QByteArray(64, '0');
    }
    return config;
}
PinnedDownloadEndpoint endpoint(const QByteArray& target) {
    return {QHostAddress{"93.184.215.14"}, QStringLiteral("media.example.test"), target, 443, true};
}
struct Harness final {
    QTemporaryDir directory{};
    WindowsProcessDownloadIo io;
    explicit Harness(bool real_worker = false) : io(configuration(directory.path(), real_worker)) {}
    ~Harness() { io.cancel(); }
};
} // namespace

class WindowsProcessDownloadIoTests final : public QObject {
    Q_OBJECT
  private slots:
    void successfulDnsWaitsForNativeExitNotJustTerminalFrame() {
        Harness h;
        QVERIFY(h.directory.isValid());
        QVERIFY(h.io.resolve("delayed.example.test"));
        QTest::qWait(150);
        QCOMPARE(h.io.resolution().state, DownloadResolutionState::Pending);
        QTRY_COMPARE_WITH_TIMEOUT(h.io.resolution().state, DownloadResolutionState::Resolved, 5000);
        QCOMPARE(h.io.resolution().addresses, QList<QHostAddress>{QHostAddress{"93.184.215.14"}});
        h.io.cancel();
        QVERIFY(h.io.pollStop().stopped);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::None);
    }
    void successfulStreamRequiresExplicitEofAndDrain_data() {
        QTest::addColumn<bool>("authenticated");
        QTest::newRow("explicit false") << false;
        QTest::newRow("explicit true") << true;
    }
    void successfulStreamRequiresExplicitEofAndDrain() {
        QFETCH(bool, authenticated);
        Harness h;
        QVERIFY(h.io.open(endpoint(authenticated ? "/eof-true" : "/eof-false")));
        QByteArray received;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     received += h.io.read(64 * 1024);
                                     return h.io.poll().state == PinnedDownloadStreamState::Finished;
                                 }()),
                                 5000);
        QCOMPARE(received, QByteArray{"HTTP/1.1 200 OK\r\n\r\naudio"});
        QCOMPARE(h.io.poll().authenticated_eof, authenticated);
        h.io.cancel();
        QVERIFY(h.io.pollStop().stopped);
    }
    void malformedOrUnacknowledgedDnsFailsClosed_data() {
        QTest::addColumn<QString>("host");
        for (const auto* scenario :
             {"missing-terminal", "bad-order", "duplicate", "stderr", "trailing", "exit-nonzero"})
            QTest::newRow(scenario) << (QString::fromLatin1(scenario) + ".example.test");
    }
    void malformedOrUnacknowledgedDnsFailsClosed() {
        QFETCH(QString, host);
        Harness h;
        QVERIFY(h.io.resolve(host));
        QTRY_COMPARE_WITH_TIMEOUT(h.io.resolution().state, DownloadResolutionState::Failed, 5000);
        h.io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::InvalidResult);
        QVERIFY(h.io.resolution().addresses.isEmpty());
    }
    void malformedOrUnacknowledgedStreamNeverReportsFinished_data() {
        QTest::addColumn<QByteArray>("target");
        for (const auto* scenario : {"missing-terminal", "oversize-frame", "stderr", "trailing", "exit-nonzero"})
            QTest::newRow(scenario) << ('/' + QByteArray(scenario));
    }
    void malformedOrUnacknowledgedStreamNeverReportsFinished() {
        QFETCH(QByteArray, target);
        Harness h;
        QVERIFY(h.io.open(endpoint(target)));
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     static_cast<void>(h.io.read(64 * 1024));
                                     return h.io.poll().state == PinnedDownloadStreamState::Failed;
                                 }()),
                                 5000);
        h.io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::InvalidResult);
        QVERIFY(!h.io.poll().authenticated_eof);
    }
    void allDnsAnswersStillPassThroughPublicAddressPolicy() {
        Harness h;
        QBuffer sink;
        QVERIFY(sink.open(QIODevice::WriteOnly));
        DirectDownloadSession session(h.io, sink);
        QVERIFY(session.start("https://mixed.example.test/a.mp3", 0));
        qint64 tick = 0;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     session.tick(++tick);
                                     return session.snapshot().state == DirectDownloadState::Failed;
                                 }()),
                                 5000);
        QCOMPARE(session.snapshot().error, DownloadFailure::Restricted);
        QVERIFY(sink.data().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
    }
    void redirectSuccessorWaitsForCancelledChildAcknowledgement() {
        Harness h;
        QVERIFY(h.io.resolve("stall.example.test"));
        h.io.cancel();
        QVERIFY(h.io.resolve("normal.example.test"));
        QCOMPARE(h.io.resolution().state, DownloadResolutionState::Pending);
        QTRY_COMPARE_WITH_TIMEOUT(h.io.resolution().state, DownloadResolutionState::Resolved, 5000);
        h.io.cancel();
        QVERIFY(h.io.pollStop().stopped);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::None);
    }
    void cancellingAQueuedSuccessorPreventsItFromStarting() {
        Harness h;
        QVERIFY(h.io.resolve("stall.example.test"));
        h.io.cancel();
        QVERIFY(h.io.resolve("normal.example.test"));
        h.io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QVERIFY(h.io.resolution().state != DownloadResolutionState::Resolved);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::None);
    }
    void consumerBackpressureKeepsReadableSlotBoundedAndCancellationDrains() {
        Harness h;
        QVERIFY(h.io.open(endpoint("/flood")));
        QTRY_VERIFY_WITH_TIMEOUT(h.io.poll().available_bytes > 0, 5000);
        for (int iteration = 0; iteration < 50; ++iteration) {
            QCoreApplication::processEvents();
            QVERIFY(h.io.poll().available_bytes <= download_worker_max_data_bytes);
        }
        h.io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::None);
    }
    void wireLimitCountsPayloadNotOnlyIpcFraming() {
        QTemporaryDir directory;
        auto config = configuration(directory.path());
        config.max_wire_bytes = 4;
        WindowsProcessDownloadIo io(config);
        QVERIFY(io.open(endpoint("/normal")));
        QTRY_COMPARE_WITH_TIMEOUT(io.poll().state, PinnedDownloadStreamState::Failed, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(io.resolution().state, DownloadResolutionState::Failed, 5000);
        io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(io.pollStop().stopped, 5000);
        QCOMPARE(io.pollStop().error, DownloadFailure::LimitExceeded);
    }
    void naturalExitAfterCompletePayloadDoesNotHideAMissingTerminalOnCancel() {
        Harness h;
        QVERIFY(h.io.open(endpoint("/missing-terminal")));
        QByteArray received;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     received += h.io.read(64 * 1024);
                                     return !received.isEmpty();
                                 }()),
                                 5000);
        QTest::qWait(150);
        h.io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::InvalidResult);
    }
    void consumptionThenCancelCannotHideANaturalExitFailure_data() {
        QTest::addColumn<QByteArray>("target");
        QTest::newRow("missing Terminal") << QByteArray{"/missing-terminal"};
        QTest::newRow("nonzero exit") << QByteArray{"/exit-nonzero"};
    }
    void consumptionThenCancelCannotHideANaturalExitFailure() {
        QFETCH(QByteArray, target);
        Harness h;
        QVERIFY(h.io.open(endpoint(target)));
        // Leave the one-frame payload unconsumed while the child exits. Unlike
        // a retrying read predicate, poll -> read exactly -> cancel performs no
        // extra observation between consuming Content-Length and cancellation.
        QTRY_VERIFY_WITH_TIMEOUT(h.io.poll().available_bytes > 0, 5000);
        QTest::qWait(150);
        const auto available = h.io.poll().available_bytes;
        QVERIFY(!h.io.read(available).isEmpty());
        h.io.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::InvalidResult);
    }
    void successorCannotEraseAnUnvalidatedNaturalExit_data() {
        QTest::addColumn<QByteArray>("target");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("missing Terminal") << QByteArray{"/missing-terminal"} << false;
        QTest::newRow("nonzero exit") << QByteArray{"/exit-nonzero"} << false;
        QTest::newRow("valid natural exit") << QByteArray{"/normal"} << true;
    }
    void successorCannotEraseAnUnvalidatedNaturalExit() {
        QFETCH(QByteArray, target);
        QFETCH(bool, accepted);
        Harness h;
        QVERIFY(h.io.open(endpoint(target)));
        QTRY_VERIFY_WITH_TIMEOUT(h.io.poll().available_bytes > 0, 5000);
        QTest::qWait(150);
        const auto available = h.io.poll().available_bytes;
        QVERIFY(!h.io.read(available).isEmpty());
        h.io.cancel();
        // The actual redirect path queues its new DNS operation immediately,
        // without an intervening pollStop that could validate the old child.
        QCOMPARE(h.io.resolve("normal.example.test"), accepted);
        if (accepted) {
            QTRY_COMPARE_WITH_TIMEOUT(h.io.resolution().state, DownloadResolutionState::Resolved, 5000);
            h.io.cancel();
        }
        QTRY_VERIFY_WITH_TIMEOUT(h.io.pollStop().stopped, 5000);
        QCOMPARE(h.io.pollStop().error, accepted ? DownloadFailure::None : DownloadFailure::InvalidResult);
    }
    void timeoutRemainsTypedUntilNativeAcknowledgement() {
        QTemporaryDir directory;
        auto config = configuration(directory.path());
        config.process.total_timeout_ms = 80;
        WindowsProcessDownloadIo io(config);
        QVERIFY(io.resolve("timeout.example.test"));
        QTRY_VERIFY_WITH_TIMEOUT(io.pollStop().stopped, 5000);
        QCOMPARE(io.pollStop().error, DownloadFailure::Timeout);
        QVERIFY(io.resolution().state != DownloadResolutionState::Resolved);
    }
    void invalidTrustedCompositionNeverLaunches_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 4; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void invalidTrustedCompositionNeverLaunches() {
        QFETCH(int, scenario);
        QTemporaryDir directory;
        auto config = configuration(directory.path());
        if (scenario == 0)
            config.process.environment.reset();
        if (scenario == 1)
            config.process.arguments = {"private-argument"};
        if (scenario == 2)
            config.process.standard_input = "private-input";
        if (scenario == 3)
            config.runtime.core_sha256 = "invalid";
        WindowsProcessDownloadIo io(config);
        QVERIFY(!io.resolve("normal.example.test"));
        QVERIFY(io.pollStop().stopped);
        QCOMPARE(io.pollStop().error, DownloadFailure::Unavailable);
    }
    void realWorkerNumericDnsTraversesSupervisorCodecAndAcknowledgement() {
        if (QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH).isEmpty())
            QSKIP("Reviewed TLS fixture is opt-in.");
        Harness h(true);
        QVERIFY(h.io.resolve("127.0.0.1"));
        QTRY_COMPARE_WITH_TIMEOUT(h.io.resolution().state, DownloadResolutionState::Resolved, 5000);
        QCOMPARE(h.io.resolution().addresses, QList<QHostAddress>{QHostAddress{QHostAddress::LocalHost}});
        QVERIFY(h.io.pollStop().stopped);
        QCOMPARE(h.io.pollStop().error, DownloadFailure::None);
    }
};
QTEST_GUILESS_MAIN(WindowsProcessDownloadIoTests)
#include "WindowsProcessDownloadIoTests.moc"
