#include "adapters/WindowsDownloadProcess.hpp"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>

using namespace music_player::m0;
namespace {
using InputResult = WindowsDownloadInputResult;
using State = WindowsDownloadProcessState;
using Error = WindowsDownloadProcessError;

bool terminal(const State state) {
    return state == State::Exited || state == State::Failed || state == State::Cancelled;
}
WindowsDownloadProcessConfig configuration(const QTemporaryDir& directory,
                                           const QStringList& arguments = {QStringLiteral("read-stdin")}) {
    WindowsDownloadProcessConfig config;
    config.executable = QString::fromUtf8(RC_MUSIC_DOWNLOAD_PROCESS_HELPER_PATH);
    config.arguments = arguments;
    config.working_directory = directory.path();
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PATH"), QLibraryInfo::path(QLibraryInfo::BinariesPath));
    environment.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
    config.environment = environment;
    config.max_stdout_bytes = 1024 * 1024;
    config.max_stderr_bytes = 128 * 1024;
    config.max_memory_bytes = 256 * 1024 * 1024;
    config.total_timeout_ms = 5000;
    config.interactive_input = WindowsDownloadInteractiveInputLimits{};
    return config;
}
struct Collected final {
    WindowsDownloadProcessUpdate update{};
    QByteArray output{}, errors{};
    qsizetype largest_poll{};
};
void sample(WindowsDownloadProcess& process, Collected& result, const qint64 now_ms) {
    QCoreApplication::processEvents();
    result.update = process.poll(now_ms);
    result.output += result.update.stdout_chunk;
    result.errors += result.update.stderr_chunk;
    result.largest_poll =
        std::max(result.largest_poll, result.update.stdout_chunk.size() + result.update.stderr_chunk.size());
}
bool collect(WindowsDownloadProcess& process, Collected& result, const qint64 now_ms = 0) {
    QElapsedTimer timer;
    timer.start();
    do {
        sample(process, result, now_ms);
        if (terminal(result.update.state))
            return true;
        QThread::msleep(1);
    } while (timer.elapsed() < 5000);
    return false;
}
bool waitForOutput(WindowsDownloadProcess& process, Collected& result, const qsizetype bytes) {
    QElapsedTimer timer;
    timer.start();
    do {
        sample(process, result, 0);
        if (result.output.size() >= bytes)
            return true;
        if (terminal(result.update.state))
            return false;
        QThread::msleep(1);
    } while (timer.elapsed() < 5000);
    return false;
}
bool waitForReady(WindowsDownloadProcess& process, Collected& result) {
    QElapsedTimer timer;
    timer.start();
    do {
        sample(process, result, 0);
        if (result.output.contains('\n'))
            return true;
        if (terminal(result.update.state))
            return false;
        QThread::msleep(1);
    } while (timer.elapsed() < 5000);
    return false;
}
struct DrainGuard final {
    WindowsDownloadProcess& process;
    QTemporaryDir& directory;
    ~DrainGuard() {
        process.cancel();
        Collected ignored;
        if (!collect(process, ignored))
            directory.setAutoRemove(false);
    }
};
struct NativeHandle final {
    HANDLE value{};
    ~NativeHandle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
DWORD readyPid(const Collected& result) {
    return static_cast<DWORD>(
        QJsonDocument::fromJson(result.output.left(result.output.indexOf('\n'))).object().value("pid").toInteger());
}
int gatedExitChild(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    const NativeHandle gate{OpenEventW(SYNCHRONIZE, FALSE, arguments.at(2).toStdWString().c_str())};
    if (!gate.value)
        return 80;
    const auto ready = QJsonDocument(QJsonObject{{QStringLiteral("pid"), qint64{GetCurrentProcessId()}}})
                           .toJson(QJsonDocument::Compact) +
                       '\n';
    DWORD written{};
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ready.constData(), static_cast<DWORD>(ready.size()), &written,
                   nullptr) ||
        static_cast<qsizetype>(written) != ready.size())
        return 81;
    // This helper deliberately never reads its own stdin. The parent owns the
    // unique local gate and can witness natural exit before polling the writer.
    if (WaitForSingleObject(gate.value, 5000) != WAIT_OBJECT_0)
        return 82;
    return arguments.at(3).toInt();
}
} // namespace

class WindowsDownloadInteractiveInputTests final : public QObject {
    Q_OBJECT
  private slots:
    void defaultModeIdleAndTerminalRefuseInteractiveOperations() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"idle"}), InputResult::Closed);
        QVERIFY(!process.closeStandardInput());
        auto config = configuration(directory);
        config.interactive_input.reset();
        config.standard_input = QByteArrayLiteral("legacy one-shot");
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"extra"}), InputResult::Closed);
        QVERIFY(!process.closeStandardInput());
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.output, config.standard_input);
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Closed);
        QVERIFY(!process.closeStandardInput());
    }
    void interactiveFifoStaysOpenBetweenBatchesAndEmptyEnqueues() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(configuration(directory), 0));
        const auto first = QStringLiteral("Primeiro 日本\n").toUtf8() + QByteArray("\0\xff", 2);
        const auto second = QStringLiteral("Segunda ação\n").toUtf8();
        QCOMPARE(process.enqueueStandardInput(first), InputResult::Accepted);
        Collected result;
        QVERIFY(waitForOutput(process, result, first.size()));
        QCOMPARE(result.output, first);
        QCOMPARE(result.update.state, State::Running);
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Accepted);
        sample(process, result, 0);
        QCOMPARE(result.update.state, State::Running);
        QCOMPARE(result.output, first);
        QCOMPARE(process.enqueueStandardInput(second), InputResult::Accepted);
        QVERIFY(waitForOutput(process, result, first.size() + second.size()));
        QVERIFY(process.closeStandardInput());
        QVERIFY(process.closeStandardInput());
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"sealed"}), InputResult::Closed);
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(result.output, first + second);
        QVERIFY(result.errors.isEmpty());
        QVERIFY(result.largest_poll <= download_process_poll_bytes);
    }
    void emptyInteractiveInputClosesExplicitlyAndCannotBeReopened() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(configuration(directory), 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Accepted);
        QVERIFY(process.closeStandardInput());
        QVERIFY(process.closeStandardInput());
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Closed);
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QVERIFY(result.output.isEmpty());
        QVERIFY(result.errors.isEmpty());
        QVERIFY(!process.closeStandardInput());
    }
    void initialInputPrecedesQueuedBytesAndCountsTowardTheExactTotal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        config.interactive_input = WindowsDownloadInteractiveInputLimits{8, 8};
        config.standard_input = QByteArrayLiteral("1234");
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"5678"}), InputResult::Accepted);
        QVERIFY(process.closeStandardInput());
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.output, QByteArrayLiteral("12345678"));
    }
    void invalidInteractiveLimitsOrInitialInputRefuseBeforeLaunch_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("zero buffered") << 0;
        QTest::newRow("negative buffered") << 1;
        QTest::newRow("buffer exceeds hard ceiling") << 2;
        QTest::newRow("zero total") << 3;
        QTest::newRow("total exceeds 64 MiB") << 4;
        QTest::newRow("initial exceeds buffer") << 5;
        QTest::newRow("initial exceeds total") << 6;
    }
    void invalidInteractiveLimitsOrInitialInputRefuseBeforeLaunch() {
        QFETCH(int, mode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        switch (mode) {
        case 0:
            config.interactive_input->max_buffered_bytes = 0;
            break;
        case 1:
            config.interactive_input->max_buffered_bytes = -1;
            break;
        case 2:
            config.interactive_input->max_buffered_bytes = download_process_max_input_bytes + 1;
            break;
        case 3:
            config.interactive_input->max_total_bytes = 0;
            break;
        case 4:
            config.interactive_input->max_total_bytes = 64ULL * 1024 * 1024 + 1;
            break;
        case 5:
            config.interactive_input->max_buffered_bytes = 4;
            config.standard_input = QByteArrayLiteral("12345");
            break;
        case 6:
            config.interactive_input->max_total_bytes = 4;
            config.standard_input = QByteArrayLiteral("12345");
            break;
        }
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(!process.start(config, 0));
        const auto result = process.poll(0);
        QCOMPARE(result.state, State::Failed);
        QCOMPARE(result.error, Error::InvalidConfiguration);
        QVERIFY(!result.exit_code);
        QVERIFY(result.stdout_chunk.isEmpty());
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"rejected"}), InputResult::Closed);
    }
    void supportedLimitCeilingsAndTotalSmallerThanBufferAreValid_data() {
        QTest::addColumn<bool>("small_total");
        QTest::newRow("hard ceilings") << false;
        QTest::newRow("smaller total") << true;
    }
    void supportedLimitCeilingsAndTotalSmallerThanBufferAreValid() {
        QFETCH(bool, small_total);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        config.interactive_input = WindowsDownloadInteractiveInputLimits{download_process_max_input_bytes,
                                                                         small_total ? 1 : 64ULL * 1024 * 1024};
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Accepted);
        QVERIFY(process.closeStandardInput());
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
    }
    void segmentedInteractiveTotalCanExceedTheLegacyOneShotCeiling() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        constexpr qsizetype segment_bytes = 64 * 1024;
        constexpr quint64 total_bytes = 512 * 1024;
        config.interactive_input = WindowsDownloadInteractiveInputLimits{segment_bytes, total_bytes};
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        Collected result;
        QByteArray expected;
        for (int index = 0; index < 8; ++index) {
            const QByteArray chunk(segment_bytes, static_cast<char>('a' + index));
            QCOMPARE(process.enqueueStandardInput(chunk), InputResult::Accepted);
            expected += chunk;
            QVERIFY(waitForOutput(process, result, expected.size()));
            QCOMPARE(result.output, expected);
        }
        QVERIFY(process.closeStandardInput());
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(static_cast<quint64>(result.output.size()), total_bytes);
        QCOMPARE(result.output, expected);
        QVERIFY(result.largest_poll <= download_process_poll_bytes);
    }
    void cumulativeQuotaIncludesInitialAndAlreadyDrainedBytesAndOverflowIsFatal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        config.interactive_input = WindowsDownloadInteractiveInputLimits{8, 8};
        config.standard_input = QByteArrayLiteral("1234");
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"5678"}), InputResult::Accepted);
        Collected result;
        QVERIFY(waitForOutput(process, result, 8));
        QCOMPARE(result.output, QByteArrayLiteral("12345678"));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"!"}), InputResult::LimitExceeded);
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"late"}), InputResult::Closed);
        QVERIFY(!process.closeStandardInput());
        QVERIFY(!process.start(config, 0));
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Error::LimitExceeded);
    }
    void oneOversizedChunkIsFatalEvenWhenTheQueueIsEmpty() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        config.interactive_input = WindowsDownloadInteractiveInputLimits{8, 64};
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"123456789"}), InputResult::LimitExceeded);
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Closed);
        QVERIFY(!process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Error::LimitExceeded);
    }
    void backpressureDoesNotWriteCountOrRetainRejectedBytesAndAllowsRetryAfterDrain() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory);
        config.interactive_input = WindowsDownloadInteractiveInputLimits{8, 16};
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"12345678"}), InputResult::Accepted);
        // No poll has run: enqueue must not write through to the native pipe.
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"!"}), InputResult::Backpressure);
        Collected result;
        QVERIFY(waitForOutput(process, result, 8));
        QCOMPARE(result.output, QByteArrayLiteral("12345678"));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"abcdefgh"}), InputResult::Accepted);
        QVERIFY(process.closeStandardInput());
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.output, QByteArrayLiteral("12345678abcdefgh"));
    }
    void initialAndQueuedInputOwnCopiesOfMutableRawViews() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::array<char, 7> initial{'I', 'N', 'I', 'T', '\0', 'x', static_cast<char>(0xff)};
        std::array<char, 6> queued{'Q', 'U', 'E', 'U', 'E', '\0'};
        const QByteArray expected_initial(initial.data(), initial.size()),
            expected_queued(queued.data(), queued.size());
        auto config = configuration(directory);
        config.standard_input = QByteArray::fromRawData(initial.data(), initial.size());
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        std::fill(initial.begin(), initial.end(), 'X');
        QCOMPARE(process.enqueueStandardInput(QByteArrayView(queued.data(), queued.size())), InputResult::Accepted);
        std::fill(queued.begin(), queued.end(), 'Y');
        QVERIFY(process.closeStandardInput());
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.output, expected_initial + expected_queued);
    }
    void cancellationOfPendingInputRequiresAckBeforeReuseAndResetsModesAndBudgets() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory, {QStringLiteral("hang")});
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        const QByteArray blocked(download_process_max_input_bytes, 'X');
        QCOMPARE(process.enqueueStandardInput(blocked), InputResult::Accepted);
        Collected result;
        QVERIFY(waitForReady(process, result));
        const NativeHandle child{OpenProcess(SYNCHRONIZE, FALSE, readyPid(result))};
        QVERIFY(child.value);
        process.cancel();
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"late"}), InputResult::Closed);
        QVERIFY(!process.closeStandardInput());
        QVERIFY(!process.start(config, 0));
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Cancelled);
        QCOMPARE(WaitForSingleObject(child.value, 0), DWORD{WAIT_OBJECT_0});

        config = configuration(directory);
        config.interactive_input.reset();
        config.standard_input = QByteArrayLiteral("legacy after cancel");
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"no"}), InputResult::Closed);
        Collected legacy;
        QVERIFY(collect(process, legacy));
        QCOMPARE(legacy.update.state, State::Exited);
        QCOMPARE(legacy.output, config.standard_input);

        config = configuration(directory);
        config.interactive_input = WindowsDownloadInteractiveInputLimits{16, 8};
        QVERIFY(process.start(config, 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"reset"}), InputResult::Accepted);
        QVERIFY(process.closeStandardInput());
        Collected next;
        QVERIFY(collect(process, next));
        QCOMPARE(next.update.state, State::Exited);
        QCOMPARE(next.output, QByteArrayLiteral("reset"));
    }
    void naturalExitWithIdleOpenInputPreservesExitAndTheNoChildRestriction_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("exit_code");
        QTest::addColumn<QByteArray>("output");
        QTest::newRow("nonzero native exit") << QStringLiteral("nonzero") << 17 << QByteArrayLiteral("OUT");
        QTest::newRow("no descendant") << QStringLiteral("spawn-probe") << 0 << QByteArrayLiteral("SPAWN_BLOCKED");
    }
    void naturalExitWithIdleOpenInputPreservesExitAndTheNoChildRestriction() {
        QFETCH(QString, mode);
        QFETCH(int, exit_code);
        QFETCH(QByteArray, output);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(configuration(directory, {mode}), 0));
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Accepted);
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.update.error, Error::None);
        QCOMPARE(result.update.exit_code, std::optional<int>{exit_code});
        QCOMPARE(result.output, output);
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"after exit"}), InputResult::Closed);
        QVERIFY(!process.closeStandardInput());
    }
    void nativeExitWithUndeliveredAcceptedInputFailsIoAndStillDrains() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto event_name = QStringLiteral("Local\\RCMusic-interactive-gated-exit-") +
                                QUuid::createUuid().toString(QUuid::WithoutBraces);
        const NativeHandle gate{CreateEventW(nullptr, TRUE, FALSE, event_name.toStdWString().c_str())};
        QVERIFY(gate.value);
        QVERIFY(GetLastError() != ERROR_ALREADY_EXISTS);
        auto config =
            configuration(directory, {QStringLiteral("--interactive-gated-exit"), event_name, QStringLiteral("17")});
        config.executable = QCoreApplication::applicationFilePath();
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        const QByteArray blocked(download_process_max_input_bytes, 'X');
        QCOMPARE(process.enqueueStandardInput(blocked), InputResult::Accepted);
        Collected result;
        QVERIFY(waitForReady(process, result));
        const NativeHandle child{OpenProcess(SYNCHRONIZE, FALSE, readyPid(result))};
        QVERIFY(child.value);
        QVERIFY(SetEvent(gate.value));
        // Witness native exit without another supervisor poll. More than one
        // pipe buffer was accepted and this helper never consumed any stdin.
        QCOMPARE(WaitForSingleObject(child.value, 5000), DWORD{WAIT_OBJECT_0});
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Error::Io);
        QCOMPARE(WaitForSingleObject(child.value, 0), DWORD{WAIT_OBJECT_0});
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Closed);
    }
    void cancellationAfterWitnessedNativeExitClosesInputAdmission_data() {
        QTest::addColumn<bool>("pending");
        QTest::newRow("idle input") << false;
        QTest::newRow("undelivered input") << true;
    }
    void cancellationAfterWitnessedNativeExitClosesInputAdmission() {
        QFETCH(bool, pending);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto event_name = QStringLiteral("Local\\RCMusic-interactive-exited-cancel-") +
                                QUuid::createUuid().toString(QUuid::WithoutBraces);
        const NativeHandle gate{CreateEventW(nullptr, TRUE, FALSE, event_name.toStdWString().c_str())};
        QVERIFY(gate.value);
        QVERIFY(GetLastError() != ERROR_ALREADY_EXISTS);
        auto config =
            configuration(directory, {QStringLiteral("--interactive-gated-exit"), event_name, QStringLiteral("0")});
        config.executable = QCoreApplication::applicationFilePath();
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        if (pending)
            QCOMPARE(process.enqueueStandardInput(QByteArray(download_process_max_input_bytes, 'X')),
                     InputResult::Accepted);
        Collected result;
        QVERIFY(waitForReady(process, result));
        const NativeHandle child{OpenProcess(SYNCHRONIZE, FALSE, readyPid(result))};
        QVERIFY(child.value);
        QVERIFY(SetEvent(gate.value));
        QCOMPARE(WaitForSingleObject(child.value, 5000), DWORD{WAIT_OBJECT_0});
        process.cancel();
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{"late"}), InputResult::Closed);
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, pending ? State::Failed : State::Exited);
        QCOMPARE(result.update.error, pending ? Error::Io : Error::None);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
    }
    void openOrBackpressuredInteractiveInputStillHonorsTimeoutAndClockRegression_data() {
        QTest::addColumn<bool>("backward");
        QTest::addColumn<bool>("pending");
        QTest::newRow("idle timeout") << false << false;
        QTest::newRow("pending timeout") << false << true;
        QTest::newRow("idle backward clock") << true << false;
        QTest::newRow("pending backward clock") << true << true;
    }
    void openOrBackpressuredInteractiveInputStillHonorsTimeoutAndClockRegression() {
        QFETCH(bool, backward);
        QFETCH(bool, pending);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory, {QStringLiteral("hang")});
        config.total_timeout_ms = 10;
        WindowsDownloadProcess process;
        DrainGuard guard{process, directory};
        QVERIFY(process.start(config, 0));
        const QByteArray bytes(pending ? download_process_max_input_bytes : 0, 'X');
        QCOMPARE(process.enqueueStandardInput(bytes), InputResult::Accepted);
        Collected result;
        QVERIFY(waitForReady(process, result));
        sample(process, result, backward ? 5 : 9);
        QCOMPARE(result.update.state, State::Running);
        QVERIFY(collect(process, result, backward ? 4 : 10));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, backward ? Error::InvalidConfiguration : Error::Timeout);
        QCOMPARE(process.enqueueStandardInput(QByteArrayView{}), InputResult::Closed);
    }
};

int main(int argc, char* argv[]) {
    if (argc == 4 && QByteArray(argv[1]) == QByteArrayLiteral("--interactive-gated-exit"))
        return gatedExitChild(argc, argv);
    QCoreApplication application(argc, argv);
    WindowsDownloadInteractiveInputTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "WindowsDownloadInteractiveInputTests.moc"
