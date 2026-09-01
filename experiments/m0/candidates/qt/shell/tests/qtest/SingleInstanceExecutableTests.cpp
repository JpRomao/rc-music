#include "OpenSourcesCommand.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <optional>
#include <utility>

using music_player::m0::open_sources_max_payload_bytes;

namespace {

constexpr int process_start_timeout_ms = 5'000;
constexpr int secondary_finish_timeout_ms = 5'000;
constexpr int primary_finish_timeout_ms = 15'000;
constexpr int process_cleanup_timeout_ms = 5'000;
constexpr int protocol_timeout_ms = 3'000;
constexpr qsizetype maximum_reply_bytes = 4'096;

const QByteArray backend_ready_marker{"M0_SINGLE_INSTANCE_BACKEND_READY"};
const QByteArray startup_hidden_marker{"M0_SINGLE_INSTANCE_STARTUP_HIDDEN"};
const QByteArray ipc_restored_marker{"M0_SINGLE_INSTANCE_IPC_RESTORED"};
const QByteArray explicit_source_visible_marker{"M0_STARTUP_EXPLICIT_SOURCE_VISIBLE"};
const QByteArray primary_ready_prefix{"M0_SINGLE_INSTANCE_READY "};
const QByteArray forwarded_marker{"M0_SINGLE_INSTANCE_FORWARDED"};
const QByteArray import_held_prefix{"M0_SINGLE_INSTANCE_IMPORT_HELD "};
const QByteArray exit_draining_prefix{"M0_SINGLE_INSTANCE_EXIT_DRAINING "};
const QByteArray completed_marker{"M0_SINGLE_INSTANCE_COMPLETED"};

class RunningProcessGuard final {
  public:
    explicit RunningProcessGuard(QProcess& process) noexcept : process_(process) {}

    ~RunningProcessGuard() {
        if (process_.state() == QProcess::NotRunning)
            return;
        process_.kill();
        static_cast<void>(process_.waitForFinished(process_cleanup_timeout_ms));
    }

    RunningProcessGuard(const RunningProcessGuard&) = delete;
    RunningProcessGuard& operator=(const RunningProcessGuard&) = delete;

  private:
    QProcess& process_;
};

[[nodiscard]] QProcessEnvironment shellEnvironment() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QSG_RHI_BACKEND"), QStringLiteral("software"));
    environment.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("Basic"));
    return environment;
}

[[nodiscard]] QString smokeArgument() { return QStringLiteral("--m0-smoke=single-instance-executable"); }

[[nodiscard]] QString configArgument(const QString& config_root) {
    return QStringLiteral("--m0-test-config-root=") + config_root;
}

void configureShellProcess(QProcess& process, const QString& executable, const QString& config_root,
                           const QString& working_directory, QStringList trailing_arguments = {}) {
    QStringList arguments{smokeArgument(), configArgument(config_root)};
    arguments.append(std::move(trailing_arguments));
    process.setProcessEnvironment(shellEnvironment());
    process.setProgram(executable);
    process.setArguments(std::move(arguments));
    process.setWorkingDirectory(working_directory);
    process.setProcessChannelMode(QProcess::SeparateChannels);
}

[[nodiscard]] bool writeStartMinimizedPreferences(const QString& config_root) {
    QFile preferences{QDir{config_root}.filePath(QStringLiteral("preferences.json"))};
    if (!preferences.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray document{
        R"({"schemaVersion":1,"theme":"system","motion":"system","volume":1,"muted":false,"skipBackwardSeconds":5,"skipForwardSeconds":5,"closeBehavior":"minimize_to_tray","restoreLastSession":false,"startMinimized":true,"queueEndBehavior":"stop_keep_selection"})"};
    return preferences.write(document) == document.size() && preferences.flush();
}

[[nodiscard]] QList<QByteArray> protocolLines(const QByteArray& output) {
    QList<QByteArray> lines = output.split('\n');
    for (QByteArray& line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
    }
    return lines;
}

[[nodiscard]] bool containsProtocolLine(const QByteArray& output, const QByteArray& expected) {
    return protocolLines(output).contains(expected);
}

[[nodiscard]] qsizetype countProtocolLine(const QByteArray& output, const QByteArray& expected) {
    qsizetype count = 0;
    for (const QByteArray& line : protocolLines(output)) {
        if (line == expected)
            ++count;
    }
    return count;
}

[[nodiscard]] qsizetype countProtocolLinesWithPrefix(const QByteArray& output, const QByteArray& prefix) {
    qsizetype count = 0;
    for (const QByteArray& line : protocolLines(output)) {
        if (line.startsWith(prefix) && line.size() > prefix.size())
            ++count;
    }
    return count;
}

[[nodiscard]] std::optional<QByteArray> lineWithPrefix(const QByteArray& output, const QByteArray& prefix) {
    for (const QByteArray& line : protocolLines(output)) {
        if (line.startsWith(prefix) && line.size() > prefix.size())
            return line;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QByteArray> waitForStdoutLineWithPrefix(QProcess& process, const QByteArray& prefix,
                                                                    QByteArray& captured_stdout, const int timeout_ms) {
    QDeadlineTimer deadline{timeout_ms};
    while (!deadline.hasExpired()) {
        captured_stdout.append(process.readAllStandardOutput());
        if (auto line = lineWithPrefix(captured_stdout, prefix); line.has_value())
            return line;
        if (process.state() == QProcess::NotRunning)
            break;
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0)
            break;
        static_cast<void>(process.waitForReadyRead(static_cast<int>(remaining)));
    }
    captured_stdout.append(process.readAllStandardOutput());
    return lineWithPrefix(captured_stdout, prefix);
}

[[nodiscard]] QByteArray diagnostics(QProcess& process, QByteArray captured_stdout = {}) {
    captured_stdout.append(process.readAllStandardOutput());
    QByteArray output{"stdout:\n"};
    output.append(captured_stdout);
    output.append("\nstderr:\n");
    output.append(process.readAllStandardError());
    if (output == QByteArrayLiteral("stdout:\n\nstderr:\n"))
        output.append(process.errorString().toUtf8());
    return output;
}

[[nodiscard]] bool writeAll(QLocalSocket& socket, const QByteArray& bytes, QDeadlineTimer& deadline) {
    if (socket.write(bytes) != bytes.size())
        return false;
    while (socket.bytesToWrite() > 0) {
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForBytesWritten(static_cast<int>(remaining)))
            return false;
    }
    return true;
}

[[nodiscard]] std::optional<QByteArray> readFrame(QLocalSocket& socket, QDeadlineTimer& deadline) {
    constexpr qsizetype prefix_bytes = sizeof(quint32);
    QByteArray bytes;
    std::optional<quint32> expected_size;
    while (!deadline.hasExpired()) {
        bytes.append(socket.readAll());
        if (!expected_size.has_value() && bytes.size() >= prefix_bytes) {
            expected_size = qFromBigEndian<quint32>(bytes.constData());
            bytes.remove(0, prefix_bytes);
            if (*expected_size == 0 || static_cast<qsizetype>(*expected_size) > maximum_reply_bytes)
                return std::nullopt;
        }
        if (expected_size.has_value() && bytes.size() >= static_cast<qsizetype>(*expected_size))
            return bytes.first(static_cast<qsizetype>(*expected_size));
        if (socket.state() == QLocalSocket::UnconnectedState)
            return std::nullopt;
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForReadyRead(static_cast<int>(remaining)))
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QByteArray> sendOversizedFramePrefix(const QString& endpoint) {
    QLocalSocket socket;
    socket.setReadBufferSize(maximum_reply_bytes + static_cast<qsizetype>(sizeof(quint32)) + 1);
    QDeadlineTimer deadline{protocol_timeout_ms};
    socket.connectToServer(endpoint);
    if (socket.state() != QLocalSocket::ConnectedState) {
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForConnected(static_cast<int>(remaining)))
            return std::nullopt;
    }

    QByteArray prefix(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(open_sources_max_payload_bytes + 1), prefix.data());
    if (!writeAll(socket, prefix, deadline))
        return std::nullopt;
    return readFrame(socket, deadline);
}

[[nodiscard]] bool isPayloadTooLargeNack(const QByteArray& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return false;
    const QJsonObject object = document.object();
    return object.size() == 2 && object.value(QStringLiteral("status")).toString() == QStringLiteral("nack") &&
           object.value(QStringLiteral("reason")).toString() == QStringLiteral("payloadTooLarge");
}

[[nodiscard]] std::optional<QStringList> persistedSourcePaths(const QString& config_root) {
    QFile session{QDir{config_root}.filePath(QStringLiteral("session.json"))};
    if (!session.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(session.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonArray items =
        document.object().value(QStringLiteral("queue")).toObject().value(QStringLiteral("items")).toArray();
    QStringList paths;
    paths.reserve(items.size());
    for (const QJsonValue& item : items) {
        if (!item.isObject())
            return std::nullopt;
        const QJsonValue source_path = item.toObject().value(QStringLiteral("sourcePath"));
        if (!source_path.isString())
            return std::nullopt;
        paths.push_back(source_path.toString());
    }
    return paths;
}

} // namespace

class SingleInstanceExecutableTests final : public QObject {
    Q_OBJECT

  public:
    SingleInstanceExecutableTests(QString shell_executable, QString audio_fixture)
        : shell_executable_(std::move(shell_executable)), audio_fixture_(std::move(audio_fixture)) {}

  private slots:
    void explicitInitialSourceOverridesStartMinimized() {
        QVERIFY2(QFileInfo::exists(shell_executable_), qPrintable(shell_executable_));
        QVERIFY2(QFileInfo::exists(audio_fixture_), qPrintable(audio_fixture_));

        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/music player startup source 日本-XXXXXX")};
        QVERIFY(root.isValid());
        const QString config_root = root.filePath(QStringLiteral("configuração isolada"));
        const QString working_directory = root.filePath(QStringLiteral("cwd fonte explícita"));
        QVERIFY(QDir{}.mkpath(config_root));
        QVERIFY(QDir{}.mkpath(working_directory));
        QVERIFY(writeStartMinimizedPreferences(config_root));
        const QString audio_path = QDir{working_directory}.filePath(QStringLiteral("abertura inicial 日本.wav"));
        QVERIFY2(QFile::copy(audio_fixture_, audio_path), qPrintable(audio_path));

        QProcess process;
        RunningProcessGuard process_guard{process};
        process.setProcessEnvironment(shellEnvironment());
        process.setProgram(shell_executable_);
        process.setArguments({QStringLiteral("--m0-smoke=startup-source-precedence"), configArgument(config_root),
                              QStringLiteral("--"), audio_path});
        process.setWorkingDirectory(working_directory);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        QVERIFY2(process.waitForStarted(process_start_timeout_ms), qPrintable(process.errorString()));
        const bool finished = process.waitForFinished(primary_finish_timeout_ms);
        const QByteArray output = diagnostics(process);
        QVERIFY2(finished, output.constData());
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());
        QVERIFY2(containsProtocolLine(output, explicit_source_visible_marker), output.constData());
    }

    void drainsTwoAcknowledgedSourcesBeforeExitAndRejectsNewCommands() {
        QVERIFY2(QFileInfo::exists(shell_executable_), qPrintable(shell_executable_));
        QVERIFY2(QFileInfo::exists(audio_fixture_), qPrintable(audio_fixture_));

        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/music player single instance 日本-XXXXXX")};
        QVERIFY(root.isValid());
        const QString config_root = root.filePath(QStringLiteral("configuração isolada"));
        const QString primary_working_directory = root.filePath(QStringLiteral("cwd primária"));
        const QString secondary_working_directory = root.filePath(QStringLiteral("cwd secundária 日本 & dados"));
        QVERIFY(QDir{}.mkpath(config_root));
        QVERIFY(QDir{}.mkpath(primary_working_directory));
        QVERIFY(QDir{}.mkpath(secondary_working_directory));
        QVERIFY(writeStartMinimizedPreferences(config_root));
        QVERIFY(QDir::cleanPath(primary_working_directory) != QDir::cleanPath(secondary_working_directory));

        const QString first_relative_audio = QStringLiteral("A ativa & abertura; 日本 (01) 'tema'.wav");
        const QString first_audio = QDir{secondary_working_directory}.filePath(first_relative_audio);
        QVERIFY2(QFile::copy(audio_fixture_, first_audio), qPrintable(first_audio));

        const QString long_segment_a = QString(96, QLatin1Char('a')) + QStringLiteral(" & dados 日本 (A)");
        const QString long_segment_b = QString(96, QLatin1Char('b')) + QStringLiteral(" ; fila 'B' (02)");
        const QString long_directory =
            QDir{secondary_working_directory}.filePath(long_segment_a + QLatin1Char('/') + long_segment_b);
        QVERIFY2(QDir{}.mkpath(long_directory), qPrintable(long_directory));
        const QString second_file_name = QStringLiteral("B longa & abertura; 日本 (02) 'tema'.wav");
        const QString second_audio = QDir{long_directory}.filePath(second_file_name);
        QVERIFY2(QFile::copy(audio_fixture_, second_audio), qPrintable(second_audio));
        const QString normalized_second_audio = QDir::cleanPath(second_audio);
        QVERIFY2(normalized_second_audio.size() > 260,
                 qPrintable(QStringLiteral("expected a Windows path longer than 260 characters, got %1: %2")
                                .arg(normalized_second_audio.size())
                                .arg(normalized_second_audio)));
        const QString second_relative_audio =
            QDir{secondary_working_directory}.relativeFilePath(normalized_second_audio);
        QVERIFY(!QDir::isAbsolutePath(second_relative_audio));

        QProcess primary;
        RunningProcessGuard primary_guard{primary};
        configureShellProcess(primary, shell_executable_, config_root, primary_working_directory);
        primary.start();
        QVERIFY2(primary.waitForStarted(process_start_timeout_ms), qPrintable(primary.errorString()));
        const qint64 primary_pid = primary.processId();
        QVERIFY(primary_pid > 0);

        QByteArray primary_stdout;
        const auto ready_line =
            waitForStdoutLineWithPrefix(primary, primary_ready_prefix, primary_stdout, primary_finish_timeout_ms);
        if (!ready_line.has_value()) {
            const QByteArray output = diagnostics(primary, std::move(primary_stdout));
            QFAIL(qPrintable(QStringLiteral("the primary shell did not advertise its IPC endpoint: %1")
                                 .arg(QString::fromUtf8(output))));
        }
        QVERIFY(containsProtocolLine(primary_stdout, backend_ready_marker));
        QVERIFY(containsProtocolLine(primary_stdout, startup_hidden_marker));
        QCOMPARE(countProtocolLine(primary_stdout, backend_ready_marker), 1);
        QCOMPARE(primary.state(), QProcess::Running);
        QCOMPARE(primary.processId(), primary_pid);
        const QByteArray endpoint_bytes = ready_line->mid(primary_ready_prefix.size());
        QVERIFY(!endpoint_bytes.isEmpty());
        const QString endpoint = QString::fromUtf8(endpoint_bytes);
        QVERIFY(!endpoint.isEmpty());

        QByteArray too_many_output;
        {
            QProcess too_many;
            RunningProcessGuard too_many_guard{too_many};
            QStringList arguments{QStringLiteral("--")};
            arguments.reserve(1'002);
            for (qsizetype index = 0; index <= 1'000; ++index)
                arguments.push_back(QStringLiteral("x.wav"));
            configureShellProcess(too_many, shell_executable_, config_root, secondary_working_directory,
                                  std::move(arguments));
            too_many.start();
            QVERIFY2(too_many.waitForStarted(process_start_timeout_ms), qPrintable(too_many.errorString()));
            const bool finished = too_many.waitForFinished(secondary_finish_timeout_ms);
            too_many_output = diagnostics(too_many);
            QVERIFY2(finished, too_many_output.constData());
            QCOMPARE(too_many.exitStatus(), QProcess::NormalExit);
            QVERIFY2(too_many.exitCode() != 0, too_many_output.constData());
            QVERIFY2(too_many_output.contains("tooManyPaths"), too_many_output.constData());
            QVERIFY(!containsProtocolLine(too_many_output, backend_ready_marker));
        }
        QCOMPARE(primary.state(), QProcess::Running);
        QCOMPARE(primary.processId(), primary_pid);

        const auto oversized_reply = sendOversizedFramePrefix(endpoint);
        QVERIFY2(oversized_reply.has_value(), "the live primary did not answer the oversized frame");
        QVERIFY2(isPayloadTooLargeNack(*oversized_reply), oversized_reply->constData());
        QCOMPARE(primary.state(), QProcess::Running);
        QCOMPARE(primary.processId(), primary_pid);

        QByteArray first_forward_output;
        {
            QProcess first_forward;
            RunningProcessGuard first_forward_guard{first_forward};
            configureShellProcess(first_forward, shell_executable_, config_root, secondary_working_directory,
                                  {QStringLiteral("--"), first_relative_audio});
            first_forward.start();
            QVERIFY2(first_forward.waitForStarted(process_start_timeout_ms), qPrintable(first_forward.errorString()));
            const bool finished = first_forward.waitForFinished(secondary_finish_timeout_ms);
            first_forward_output = diagnostics(first_forward);
            QVERIFY2(finished, first_forward_output.constData());
            QCOMPARE(first_forward.exitStatus(), QProcess::NormalExit);
            QVERIFY2(first_forward.exitCode() == 0, first_forward_output.constData());
            QVERIFY2(containsProtocolLine(first_forward_output, forwarded_marker), first_forward_output.constData());
            QVERIFY(!containsProtocolLine(first_forward_output, backend_ready_marker));
        }

        const auto held_line =
            waitForStdoutLineWithPrefix(primary, import_held_prefix, primary_stdout, secondary_finish_timeout_ms);
        if (!held_line.has_value()) {
            const QByteArray output = diagnostics(primary, primary_stdout);
            QFAIL(qPrintable(
                QStringLiteral("the primary did not hold source A active: %1").arg(QString::fromUtf8(output))));
        }
        QVERIFY(containsProtocolLine(primary_stdout, ipc_restored_marker));
        QCOMPARE(primary.state(), QProcess::Running);

        QByteArray second_forward_output;
        {
            QProcess second_forward;
            RunningProcessGuard second_forward_guard{second_forward};
            configureShellProcess(second_forward, shell_executable_, config_root, secondary_working_directory,
                                  {QStringLiteral("--"), second_relative_audio});
            second_forward.start();
            QVERIFY2(second_forward.waitForStarted(process_start_timeout_ms), qPrintable(second_forward.errorString()));
            const bool finished = second_forward.waitForFinished(secondary_finish_timeout_ms);
            second_forward_output = diagnostics(second_forward);
            QVERIFY2(finished, second_forward_output.constData());
            QCOMPARE(second_forward.exitStatus(), QProcess::NormalExit);
            QVERIFY2(second_forward.exitCode() == 0, second_forward_output.constData());
            QVERIFY2(containsProtocolLine(second_forward_output, forwarded_marker), second_forward_output.constData());
            QVERIFY(!containsProtocolLine(second_forward_output, backend_ready_marker));
        }

        const auto draining_line =
            waitForStdoutLineWithPrefix(primary, exit_draining_prefix, primary_stdout, secondary_finish_timeout_ms);
        if (!draining_line.has_value()) {
            const QByteArray output = diagnostics(primary, primary_stdout);
            QFAIL(qPrintable(QStringLiteral("the primary did not enter source drain after ACKing B: %1")
                                 .arg(QString::fromUtf8(output))));
        }
        QCOMPARE(primary.state(), QProcess::Running);

        QByteArray rejected_output;
        {
            QProcess rejected;
            RunningProcessGuard rejected_guard{rejected};
            configureShellProcess(rejected, shell_executable_, config_root, secondary_working_directory,
                                  {QStringLiteral("--"), first_relative_audio});
            rejected.start();
            QVERIFY2(rejected.waitForStarted(process_start_timeout_ms), qPrintable(rejected.errorString()));
            const bool finished = rejected.waitForFinished(secondary_finish_timeout_ms);
            rejected_output = diagnostics(rejected);
            QVERIFY2(finished, rejected_output.constData());
            QCOMPARE(rejected.exitStatus(), QProcess::NormalExit);
            QCOMPARE(rejected.exitCode(), 2);
            QVERIFY2(rejected_output.contains("temporarilyUnavailable"), rejected_output.constData());
            QVERIFY(!containsProtocolLine(rejected_output, forwarded_marker));
            QVERIFY(!containsProtocolLine(rejected_output, backend_ready_marker));
        }

        QFile resume_barrier{QDir{config_root}.filePath(QStringLiteral("single-instance-resume-drain"))};
        QVERIFY2(resume_barrier.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 qPrintable(resume_barrier.errorString()));
        QCOMPARE(resume_barrier.write("resume"), qint64{6});
        resume_barrier.close();

        const bool primary_finished = primary.waitForFinished(primary_finish_timeout_ms);
        primary_stdout.append(primary.readAllStandardOutput());
        QByteArray primary_output{"stdout:\n"};
        primary_output.append(primary_stdout);
        primary_output.append("\nstderr:\n");
        primary_output.append(primary.readAllStandardError());
        QVERIFY2(primary_finished, primary_output.constData());
        QCOMPARE(primary.exitStatus(), QProcess::NormalExit);
        QVERIFY2(primary.exitCode() == 0, primary_output.constData());
        QVERIFY2(containsProtocolLine(primary_stdout, completed_marker), primary_output.constData());
        QCOMPARE(countProtocolLine(primary_stdout, backend_ready_marker), 1);
        QCOMPARE(countProtocolLine(primary_stdout, startup_hidden_marker), 1);
        QCOMPARE(countProtocolLine(primary_stdout, ipc_restored_marker), 1);
        QCOMPARE(countProtocolLinesWithPrefix(primary_stdout, primary_ready_prefix), 1);
        QCOMPARE(countProtocolLinesWithPrefix(primary_stdout, import_held_prefix), 1);
        QCOMPARE(countProtocolLinesWithPrefix(primary_stdout, exit_draining_prefix), 1);
        QCOMPARE(countProtocolLine(primary_stdout, completed_marker), 1);

        const auto persisted_sources = persistedSourcePaths(config_root);
        QVERIFY2(persisted_sources.has_value(), "the primary did not persist a valid source queue");
        QCOMPARE(persisted_sources->size(), 2);
        QStringList normalized_persisted_sources;
        normalized_persisted_sources.reserve(persisted_sources->size());
        for (const QString& source : *persisted_sources) {
            QVERIFY(QDir::isAbsolutePath(source));
            normalized_persisted_sources.push_back(QDir::cleanPath(QDir::fromNativeSeparators(source)));
        }
        QCOMPARE(normalized_persisted_sources,
                 QStringList({QDir::cleanPath(QDir::fromNativeSeparators(first_audio)),
                              QDir::cleanPath(QDir::fromNativeSeparators(normalized_second_audio))}));

        const qsizetype backend_count = countProtocolLine(primary_stdout, backend_ready_marker) +
                                        countProtocolLine(too_many_output, backend_ready_marker) +
                                        countProtocolLine(first_forward_output, backend_ready_marker) +
                                        countProtocolLine(second_forward_output, backend_ready_marker) +
                                        countProtocolLine(rejected_output, backend_ready_marker);
        QCOMPARE(backend_count, 1);
    }

  private:
    QString shell_executable_;
    QString audio_fixture_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 3) {
        qCritical("Expected the shell executable and the versioned WAV fixture.");
        return 2;
    }

    SingleInstanceExecutableTests tests{arguments.at(1), arguments.at(2)};
    int test_argc = 1;
    char* test_argv[] = {argv[0], nullptr};
    return QTest::qExec(&tests, test_argc, test_argv);
}

#include "SingleInstanceExecutableTests.moc"
