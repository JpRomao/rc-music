#include "QtSingleInstanceCoordinator.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

using music_player::m0::encodeOpenSourcesCommand;
using music_player::m0::open_sources_max_payload_bytes;
using music_player::m0::OpenSourcesCommand;
using music_player::m0::QtSingleInstanceCoordinator;
using music_player::m0::QtSingleInstanceOptions;
using music_player::m0::QtSingleInstanceOutcome;
using music_player::m0::QtSingleInstanceStartResult;

namespace {

constexpr int process_start_timeout_ms = 5'000;
constexpr int process_finish_timeout_ms = 10'000;

[[nodiscard]] QtSingleInstanceOptions optionsFor(const QTemporaryDir& root) {
    return {
        .lock_file_path = root.filePath(QStringLiteral("single-instance.lock")),
        .server_name_prefix = QStringLiteral("music-player-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces),
        .max_bootstrap_commands = 16,
        .max_bootstrap_payload_bytes = 4 * open_sources_max_payload_bytes,
        .secondary_retry_timeout = 1'500ms,
        .secondary_retry_interval = 10ms,
        .frame_timeout = 1'000ms,
    };
}

[[nodiscard]] QString sourcePath(const QTemporaryDir& root, const QString& name) {
    return QDir::cleanPath(root.filePath(name));
}

[[nodiscard]] QByteArray frame(const QByteArray& payload) {
    QByteArray framed(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), framed.data());
    framed.append(payload);
    return framed;
}

[[nodiscard]] std::optional<QByteArray> readFrame(QLocalSocket& socket, const int timeout_ms) {
    QDeadlineTimer deadline{timeout_ms};
    QByteArray bytes;
    std::optional<quint32> expected_size;
    while (!deadline.hasExpired()) {
        bytes.append(socket.readAll());
        if (!expected_size.has_value() && bytes.size() >= static_cast<qsizetype>(sizeof(quint32))) {
            expected_size = qFromBigEndian<quint32>(bytes.constData());
            bytes.remove(0, sizeof(quint32));
        }
        if (expected_size.has_value() && bytes.size() >= static_cast<qsizetype>(*expected_size))
            return bytes.first(static_cast<qsizetype>(*expected_size));
        if (socket.state() == QLocalSocket::UnconnectedState)
            break;
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForReadyRead(static_cast<int>(remaining)))
            break;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QByteArray> rawExchange(const QString& server_name, const QByteArray& request_frame) {
    QLocalSocket socket;
    socket.connectToServer(server_name);
    if (socket.state() != QLocalSocket::ConnectedState && !socket.waitForConnected(2'000))
        return std::nullopt;
    if (socket.write(request_frame) != request_frame.size())
        return std::nullopt;
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(2'000))
            return std::nullopt;
    }
    return readFrame(socket, 2'000);
}

[[nodiscard]] QString responseField(const QByteArray& response, const QString& field) {
    const QJsonDocument document = QJsonDocument::fromJson(response);
    if (!document.isObject())
        return {};
    return document.object().value(field).toString();
}

class RunningProcessesGuard final {
  public:
    explicit RunningProcessesGuard(std::vector<std::unique_ptr<QProcess>>& processes) noexcept
        : processes_(processes) {}

    ~RunningProcessesGuard() {
        for (const auto& process : processes_) {
            if (process->state() == QProcess::NotRunning)
                continue;
            process->kill();
            static_cast<void>(process->waitForFinished(5'000));
        }
    }

    RunningProcessesGuard(const RunningProcessesGuard&) = delete;
    RunningProcessesGuard& operator=(const RunningProcessesGuard&) = delete;

  private:
    std::vector<std::unique_ptr<QProcess>>& processes_;
};

class RunningProcessGuard final {
  public:
    explicit RunningProcessGuard(QProcess& process) noexcept : process_(process) {}

    ~RunningProcessGuard() {
        if (process_.state() == QProcess::NotRunning)
            return;
        process_.kill();
        static_cast<void>(process_.waitForFinished(5'000));
    }

    RunningProcessGuard(const RunningProcessGuard&) = delete;
    RunningProcessGuard& operator=(const RunningProcessGuard&) = delete;

  private:
    QProcess& process_;
};

[[nodiscard]] bool waitForBarrier(const QString& barrier_path) {
    QDeadlineTimer deadline{5'000};
    while (!QFileInfo::exists(barrier_path) && !deadline.hasExpired())
        QThread::msleep(5);
    return QFileInfo::exists(barrier_path);
}

[[nodiscard]] int runElectionHelper(const QStringList& arguments) {
    if (arguments.size() != 7)
        return 2;

    const QString lock_path = arguments.at(2);
    const QString server_prefix = arguments.at(3);
    const QString barrier_path = arguments.at(4);
    const QString source_path = arguments.at(5);
    bool expected_ok = false;
    const qsizetype expected_commands = arguments.at(6).toLongLong(&expected_ok);
    if (!expected_ok || expected_commands < 1 || !waitForBarrier(barrier_path))
        return 3;

    QtSingleInstanceOptions options{
        .lock_file_path = lock_path,
        .server_name_prefix = server_prefix,
        .max_bootstrap_commands = 16,
        .max_bootstrap_payload_bytes = 4 * open_sources_max_payload_bytes,
        .secondary_retry_timeout = 4'000ms,
        .secondary_retry_interval = 10ms,
        .frame_timeout = 1'000ms,
    };
    QtSingleInstanceCoordinator coordinator{std::move(options)};
    const QtSingleInstanceStartResult result = coordinator.start(OpenSourcesCommand{{source_path}});
    if (result.outcome == QtSingleInstanceOutcome::Forwarded)
        return 10;
    if (result.outcome != QtSingleInstanceOutcome::Primary)
        return 30;

    QTimer completion_poll;
    QObject::connect(&completion_poll, &QTimer::timeout, &completion_poll, [&] {
        if (coordinator.pendingCommandCount() >= expected_commands)
            QCoreApplication::quit();
    });
    completion_poll.start(10);
    QTimer::singleShot(6'000, QCoreApplication::instance(), &QCoreApplication::quit);
    if (coordinator.pendingCommandCount() < expected_commands)
        static_cast<void>(QCoreApplication::exec());

    return coordinator.pendingCommandCount() == expected_commands ? 20 : 31;
}

[[nodiscard]] int runLockHolderHelper(const QStringList& arguments) {
    if (arguments.size() != 4)
        return 2;

    QtSingleInstanceCoordinator coordinator{{.lock_file_path = arguments.at(2), .server_name_prefix = arguments.at(3)}};
    const auto result = coordinator.start(OpenSourcesCommand{});
    if (result.outcome != QtSingleInstanceOutcome::Primary)
        return 3;
    return QCoreApplication::exec();
}

} // namespace

class QtSingleInstanceCoordinatorTests final : public QObject {
    Q_OBJECT

  private slots:
    void acquiresALongLivedLockBeforeServingAndBoundsTheBootstrapQueue() {
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/music player IPC 日本-XXXXXX")};
        QVERIFY(root.isValid());
        const QtSingleInstanceOptions options = optionsFor(root);
        const OpenSourcesCommand initial{{sourcePath(root, QStringLiteral("Música 'um' & dois.flac"))}};

        QtSingleInstanceCoordinator coordinator{options};
        const auto result = coordinator.start(initial);

        QCOMPARE(result.outcome, QtSingleInstanceOutcome::Primary);
        QVERIFY(coordinator.isPrimary());
        QVERIFY(coordinator.isAccepting());
        QVERIFY(!coordinator.serverName().isEmpty());
        QCOMPARE(coordinator.pendingCommandCount(), 1);
        QVERIFY(coordinator.pendingPayloadBytes() > 0);

        QLockFile competing_lock{options.lock_file_path};
        competing_lock.setStaleLockTime(0ms);
        QVERIFY(!competing_lock.tryLock(0ms));
        QCOMPARE(competing_lock.error(), QLockFile::LockFailedError);
        qint64 owner_pid = 0;
        QVERIFY(competing_lock.getLockInfo(&owner_pid, nullptr, nullptr));
        QCOMPARE(owner_pid, QCoreApplication::applicationPid());

        const auto queued = coordinator.takeNextCommand();
        QVERIFY(queued.has_value());
        QCOMPARE(queued->paths, initial.paths);
        QCOMPARE(coordinator.pendingCommandCount(), 0);
        QCOMPARE(coordinator.pendingPayloadBytes(), 0);
    }

    void forwardsUnicodePathsAndReturnsOnlyAfterThePrimaryAcknowledgesThem() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QtSingleInstanceOptions options = optionsFor(root);
        QtSingleInstanceCoordinator primary{options};
        QCOMPARE(primary.start(OpenSourcesCommand{}).outcome, QtSingleInstanceOutcome::Primary);
        QSignalSpy available_spy{&primary, &QtSingleInstanceCoordinator::commandAvailable};
        const OpenSourcesCommand forwarded{{sourcePath(root, QStringLiteral("日本 música & 'aspas'.mp3")),
                                            sourcePath(root, QStringLiteral("duplicada.wav")),
                                            sourcePath(root, QStringLiteral("duplicada.wav"))}};

        auto result_future = std::async(std::launch::async, [options, forwarded] {
            QtSingleInstanceCoordinator secondary{options};
            return secondary.start(forwarded);
        });

        QTRY_VERIFY_WITH_TIMEOUT(result_future.wait_for(0ms) == std::future_status::ready, 3'000);
        const auto result = result_future.get();
        QCOMPARE(result.outcome, QtSingleInstanceOutcome::Forwarded);
        QCOMPARE(available_spy.count(), 1);
        const auto queued = primary.takeNextCommand();
        QVERIFY(queued.has_value());
        QCOMPARE(queued->paths, forwarded.paths);
    }

    void nacksACommandWhenTheBoundedBootstrapQueueIsFull() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QtSingleInstanceOptions options = optionsFor(root);
        options.max_bootstrap_commands = 1;
        const OpenSourcesCommand first{{sourcePath(root, QStringLiteral("first.wav"))}};
        const OpenSourcesCommand rejected{{sourcePath(root, QStringLiteral("second.wav"))}};
        QtSingleInstanceCoordinator primary{options};
        QCOMPARE(primary.start(first).outcome, QtSingleInstanceOutcome::Primary);

        auto result_future = std::async(std::launch::async, [options, rejected] {
            QtSingleInstanceCoordinator secondary{options};
            return secondary.start(rejected);
        });

        QTRY_VERIFY_WITH_TIMEOUT(result_future.wait_for(0ms) == std::future_status::ready, 3'000);
        const auto result = result_future.get();
        QCOMPARE(result.outcome, QtSingleInstanceOutcome::Rejected);
        QCOMPARE(result.detail, QStringLiteral("queueFull"));
        QCOMPARE(primary.pendingCommandCount(), 1);
        QCOMPARE(primary.takeNextCommand()->paths, first.paths);
    }

    void rejectsAnOversizedLengthBeforeParsingAndStillAcceptsTheNextClient() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QtSingleInstanceOptions options = optionsFor(root);
        QtSingleInstanceCoordinator primary{options};
        QCOMPARE(primary.start(OpenSourcesCommand{}).outcome, QtSingleInstanceOutcome::Primary);

        QByteArray oversized_prefix(sizeof(quint32), Qt::Uninitialized);
        qToBigEndian<quint32>(static_cast<quint32>(open_sources_max_payload_bytes + 1), oversized_prefix.data());
        const QString server_name = primary.serverName();
        auto response_future = std::async(
            std::launch::async, [server_name, oversized_prefix] { return rawExchange(server_name, oversized_prefix); });
        QTRY_VERIFY_WITH_TIMEOUT(response_future.wait_for(0ms) == std::future_status::ready, 3'000);
        const auto response = response_future.get();
        QVERIFY(response.has_value());
        QCOMPARE(responseField(*response, QStringLiteral("status")), QStringLiteral("nack"));
        QCOMPARE(responseField(*response, QStringLiteral("reason")), QStringLiteral("payloadTooLarge"));
        QCOMPARE(primary.pendingCommandCount(), 0);

        const OpenSourcesCommand valid{{sourcePath(root, QStringLiteral("after oversize.wav"))}};
        auto valid_future = std::async(std::launch::async, [options, valid] {
            QtSingleInstanceCoordinator secondary{options};
            return secondary.start(valid);
        });
        QTRY_VERIFY_WITH_TIMEOUT(valid_future.wait_for(0ms) == std::future_status::ready, 3'000);
        QCOMPARE(valid_future.get().outcome, QtSingleInstanceOutcome::Forwarded);
        QCOMPARE(primary.takeNextCommand()->paths, valid.paths);
    }

    void rejectsTrailingBytesAlreadyBufferedAfterACompleteFrame() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QtSingleInstanceOptions options = optionsFor(root);
        QtSingleInstanceCoordinator primary{options};
        QCOMPARE(primary.start(OpenSourcesCommand{}).outcome, QtSingleInstanceOutcome::Primary);
        const OpenSourcesCommand command{{sourcePath(root, QStringLiteral("trailing.wav"))}};
        const auto encoded = encodeOpenSourcesCommand(command);
        QVERIFY(encoded.payload.size() > 0);
        const QByteArray request = frame(encoded.payload) + QByteArrayLiteral("unexpected-second-frame");
        const QString server_name = primary.serverName();

        auto response_future =
            std::async(std::launch::async, [server_name, request] { return rawExchange(server_name, request); });
        QTRY_VERIFY_WITH_TIMEOUT(response_future.wait_for(0ms) == std::future_status::ready, 3'000);
        const auto response = response_future.get();
        QVERIFY(response.has_value());
        QCOMPARE(responseField(*response, QStringLiteral("status")), QStringLiteral("nack"));
        QCOMPARE(responseField(*response, QStringLiteral("reason")), QStringLiteral("invalidFrame"));
        QCOMPARE(primary.pendingCommandCount(), 0);
    }

    void doesNotRetransmitAfterTheRequestWasWrittenButTheAckWasLost() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QtSingleInstanceOptions options = optionsFor(root);
        options.secondary_retry_timeout = 500ms;
        options.secondary_retry_interval = 10ms;
        QLockFile owner{options.lock_file_path};
        owner.setStaleLockTime(0ms);
        QVERIFY(owner.tryLock(0ms));

        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        const QString endpoint =
            options.server_name_prefix + QLatin1Char('.') + QString::number(QCoreApplication::applicationPid());
        QVERIFY2(server.listen(endpoint), qPrintable(server.errorString()));
        int received_requests = 0;
        connect(&server, &QLocalServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                QLocalSocket* socket = server.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QLocalSocket::readyRead, socket, [socket, bytes, &received_requests] {
                    bytes->append(socket->readAll());
                    if (bytes->size() < static_cast<qsizetype>(sizeof(quint32)))
                        return;
                    const quint32 payload_size = qFromBigEndian<quint32>(bytes->constData());
                    if (bytes->size() < static_cast<qsizetype>(sizeof(quint32) + payload_size))
                        return;
                    ++received_requests;
                    socket->abort();
                });
            }
        });

        const OpenSourcesCommand command{{sourcePath(root, QStringLiteral("lost ack.wav"))}};
        auto result_future = std::async(std::launch::async, [options, command] {
            QtSingleInstanceCoordinator secondary{options};
            return secondary.start(command);
        });
        QTRY_VERIFY_WITH_TIMEOUT(result_future.wait_for(0ms) == std::future_status::ready, 2'000);
        const auto result = result_future.get();
        QCOMPARE(result.outcome, QtSingleInstanceOutcome::Failed);
        QCOMPARE(result.detail, QStringLiteral("invalidResponse"));
        QCOMPARE(received_requests, 1);
    }

    void timesOutWithoutStartingAnotherPrimaryWhileTheOwnerHasNoServer() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QtSingleInstanceOptions options = optionsFor(root);
        options.secondary_retry_timeout = 150ms;
        options.secondary_retry_interval = 10ms;

        QLockFile owner{options.lock_file_path};
        owner.setStaleLockTime(0ms);
        QVERIFY(owner.tryLock(0ms));

        auto result_future = std::async(std::launch::async, [options] {
            QtSingleInstanceCoordinator secondary{options};
            return secondary.start(OpenSourcesCommand{});
        });
        QTRY_VERIFY_WITH_TIMEOUT(result_future.wait_for(0ms) == std::future_status::ready, 2'000);
        const auto result = result_future.get();
        QCOMPARE(result.outcome, QtSingleInstanceOutcome::TimedOut);
        QVERIFY(owner.isLocked());
    }

    void electsExactlyOnePrimaryAndForwardsEveryCommandAcrossProcesses() {
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/music player election 日本-XXXXXX")};
        QVERIFY(root.isValid());
        const QtSingleInstanceOptions options = optionsFor(root);
        const QString barrier_path = root.filePath(QStringLiteral("start.barrier"));
        constexpr int process_count = 6;
        std::vector<std::unique_ptr<QProcess>> processes;
        processes.reserve(process_count);
        RunningProcessesGuard cleanup{processes};

        for (int index = 0; index < process_count; ++index) {
            auto process = std::make_unique<QProcess>();
            process->setProgram(QCoreApplication::applicationFilePath());
            process->setArguments({QStringLiteral("--single-instance-election-helper"), options.lock_file_path,
                                   options.server_name_prefix, barrier_path,
                                   sourcePath(root, QStringLiteral("child-%1 日本.wav").arg(index)),
                                   QString::number(process_count)});
            process->start();
            QVERIFY2(process->waitForStarted(process_start_timeout_ms), qPrintable(process->errorString()));
            processes.push_back(std::move(process));
        }

        QFile barrier{barrier_path};
        QVERIFY(barrier.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        barrier.close();

        int primary_count = 0;
        int forwarded_count = 0;
        for (const auto& process : processes) {
            QVERIFY2(process->waitForFinished(process_finish_timeout_ms), qPrintable(process->errorString()));
            const QByteArray output = process->readAllStandardOutput() + process->readAllStandardError();
            QCOMPARE(process->exitStatus(), QProcess::NormalExit);
            if (process->exitCode() == 20) {
                ++primary_count;
            } else if (process->exitCode() == 10) {
                ++forwarded_count;
            } else {
                QFAIL(qPrintable(QStringLiteral("unexpected helper exit %1: %2")
                                     .arg(process->exitCode())
                                     .arg(QString::fromUtf8(output))));
            }
        }
        QCOMPARE(primary_count, 1);
        QCOMPARE(forwarded_count, process_count - 1);
    }

    void removesAStaleLockAfterThePrimaryProcessIsKilled() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QtSingleInstanceOptions options = optionsFor(root);
        QProcess holder;
        RunningProcessGuard holder_cleanup{holder};
        holder.setProgram(QCoreApplication::applicationFilePath());
        holder.setArguments(
            {QStringLiteral("--single-instance-lock-holder"), options.lock_file_path, options.server_name_prefix});
        holder.start();
        QVERIFY2(holder.waitForStarted(process_start_timeout_ms), qPrintable(holder.errorString()));
        QLockFile observer{options.lock_file_path};
        observer.setStaleLockTime(0ms);
        qint64 owner_pid = 0;
        QTRY_VERIFY_WITH_TIMEOUT(observer.getLockInfo(&owner_pid, nullptr, nullptr) &&
                                     owner_pid == static_cast<qint64>(holder.processId()),
                                 process_start_timeout_ms);

        holder.kill();
        QVERIFY2(holder.waitForFinished(process_finish_timeout_ms), qPrintable(holder.errorString()));
        QVERIFY(QFileInfo::exists(options.lock_file_path));

        QtSingleInstanceCoordinator replacement{options};
        QCOMPARE(replacement.start(OpenSourcesCommand{}).outcome, QtSingleInstanceOutcome::Primary);
        QVERIFY(replacement.isPrimary());
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1 && arguments.at(1) == QStringLiteral("--single-instance-election-helper"))
        return runElectionHelper(arguments);
    if (arguments.size() > 1 && arguments.at(1) == QStringLiteral("--single-instance-lock-holder"))
        return runLockHolderHelper(arguments);

    QtSingleInstanceCoordinatorTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "QtSingleInstanceCoordinatorTests.moc"
