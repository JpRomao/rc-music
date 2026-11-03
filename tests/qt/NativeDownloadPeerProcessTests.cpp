#include "adapters/QtNativeDownloadClient.hpp"
#include "adapters/QtNativeDownloadServer.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace music_player::m0;
namespace {
QString helper_executable;
const QString origin = QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/");
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";

QString endpoint() { return QStringLiteral("rc-music-native-peer-") + QUuid::createUuid().toString(QUuid::Id128); }

DownloadRequest request() {
    return {.request_id = request_id,
            .items = {{"https://fixture.invalid/synthetic.mp3", "Synthetic", DownloadMediaKind::Audio}}};
}

NativeHostConfiguration configuration(const QString& name, const QString& owner_image, const QString& host_image) {
    NativeHostConfiguration result{origin, name};
    result.expected_owner_images = {owner_image};
    result.expected_host_images = {host_image};
    return result;
}

QByteArray fileHash(const QString& path) {
    QFile file{path};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result() : QByteArray{};
}

class Child final {
  public:
    QProcess process;
    QByteArray output;
    QByteArray errors;
    bool overflow{false};

    Child() {
        process.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &process,
                         [this] { collect(QProcess::StandardOutput, output); });
        QObject::connect(&process, &QProcess::readyReadStandardError, &process,
                         [this] { collect(QProcess::StandardError, errors); });
    }
    ~Child() {
        // Only this QProcess belongs to the test. Never target a browser,
        // persistent player, supplied PID or unrelated process.
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            static_cast<void>(process.waitForFinished(2'000));
        }
        QObject::disconnect(&process, nullptr, &process, nullptr);
    }
    bool start(const QString& name, const QString& mode, const QString& expected_image, const QString& program = {}) {
        const auto image = program.isEmpty() ? helper_executable : program;
        process.setWorkingDirectory(QFileInfo{image}.absolutePath());
        process.start(image, {name, mode, expected_image});
        return process.waitForStarted(2'000);
    }
    bool command(const QByteArray& value) { return process.write(value) == value.size(); }
    qsizetype count(const QByteArray& value) const { return output.split('\n').count(value); }
    bool cleanOutput() {
        collect(QProcess::StandardOutput, output);
        collect(QProcess::StandardError, errors);
        if (overflow || !errors.isEmpty() || (!output.isEmpty() && !output.endsWith('\n')))
            return false;
        for (const auto& line : output.split('\n')) {
            if (!line.isEmpty() && line != "READY" && line != "REFUSED" && line != "CONNECTED" && line != "ADMITTED" &&
                line != "COMPLETED" && line != "RECEIVED" && line != "REPLIED" && line != "CLOSED" && line != "ZERO" &&
                line != "NONZERO")
                return false;
        }
        return true;
    }
    bool finish() {
        if (process.state() != QProcess::NotRunning && (!command("quit\n") || !process.waitForFinished(3'000)))
            return false;
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && cleanOutput();
    }

  private:
    void collect(const QProcess::ProcessChannel channel, QByteArray& bytes) {
        constexpr qsizetype limit = 2'048;
        process.setReadChannel(channel);
        if (bytes.size() <= limit)
            bytes += process.read(limit + 1 - bytes.size());
        overflow = overflow || bytes.size() > limit || process.bytesAvailable() > 0;
        process.setReadChannel(QProcess::StandardOutput);
    }
};

class Submission final : public AsyncDownloadSubmission {
  public:
    int begins{0};
    DownloadAdmissionResult begin(const DownloadRequest&) override {
        ++begins;
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override { return std::nullopt; }
};

bool observe(QtNativeDownloadClient& client, std::optional<DownloadSubmissionResult>& answer, bool& uncertain) {
    try {
        answer = client.poll(request_id);
    } catch (const std::runtime_error&) {
        uncertain = true;
    }
    return answer.has_value() || uncertain;
}
} // namespace

class NativeDownloadPeerProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QVERIFY(QFileInfo{helper_executable}.isFile());
        QVERIFY(QFileInfo{QCoreApplication::applicationFilePath()}.isFile());
        QVERIFY(helper_executable != QCoreApplication::applicationFilePath());
    }

    void anotherExecutableWithCorrectPreambleNeverReachesOwnerAdmission() {
        const auto name = endpoint();
        const auto self = QCoreApplication::applicationFilePath();
        Submission owner;
        QtNativeDownloadServer server{configuration(name, self, self), owner};
        QVERIFY(server.start());
        Child peer;
        QVERIFY(peer.start(name, QStringLiteral("raw-host"), self));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("READY"), 1, 3'000);
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("CONNECTED"), 1, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(peer.count("CLOSED") != 0 || owner.begins != 0, 3'000);
        QCOMPARE(owner.begins, 0);
        QCOMPARE(peer.count("REPLIED"), 0);
        QCOMPARE(peer.count("CLOSED"), 1);
        QTRY_COMPARE_WITH_TIMEOUT(server.activeConnectionCount(), qsizetype{0}, 3'000);
        QVERIFY(peer.finish());
        server.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(server.stopped(), 3'000);
    }

    void anotherExecutableServingTheEndpointReceivesNoRequestBytes() {
        const auto name = endpoint();
        const auto self = QCoreApplication::applicationFilePath();
        Child peer;
        QVERIFY(peer.start(name, QStringLiteral("raw-owner"), self));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("READY"), 1, 3'000);
        QtNativeDownloadClient client{configuration(name, self, self)};
        const auto initial = client.begin(request());
        std::optional<DownloadSubmissionResult> answer;
        bool uncertain = false;
        if (initial.state == DownloadAdmissionState::Completed)
            answer = initial.result;
        QTRY_VERIFY_WITH_TIMEOUT(
            answer.has_value() || observe(client, answer, uncertain) || peer.count("RECEIVED") != 0, 3'000);
        QCOMPARE(peer.count("RECEIVED"), 0);
        QVERIFY(!uncertain);
        QVERIFY(answer);
        QCOMPARE(answer->error, DownloadSubmissionError::NotAvailable);
        QVERIFY(answer->job_ids.empty());
        QVERIFY(peer.command("probe\n"));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("ZERO"), 1, 3'000);
        QCOMPARE(peer.count("NONZERO"), 0);
        client.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(client.stopped(), 3'000);
        QVERIFY(peer.finish());
    }

    void permittedExecutablesWaitForExplicitCompletionBeforeAcknowledgement() {
        const auto name = endpoint();
        const auto self = QCoreApplication::applicationFilePath();
        Child peer;
        QVERIFY(peer.start(name, QStringLiteral("owner"), self));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("READY"), 1, 3'000);
        QtNativeDownloadClient client{configuration(name, helper_executable, self)};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("ADMITTED"), 1, 3'000);
        QVERIFY(!client.poll(request_id));
        QCOMPARE(peer.count("COMPLETED"), 0);
        QVERIFY(peer.command("complete\n"));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("COMPLETED"), 1, 3'000);
        std::optional<DownloadSubmissionResult> answer;
        bool uncertain = false;
        QTRY_VERIFY_WITH_TIMEOUT(observe(client, answer, uncertain), 3'000);
        QVERIFY(!uncertain);
        QVERIFY(answer);
        QCOMPARE(answer->error, DownloadSubmissionError::None);
        QCOMPARE(answer->job_ids, std::vector<std::string>{"peer-job-1"});
        QCOMPARE(peer.count("ADMITTED"), 1);
        client.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(client.stopped(), 3'000);
        QVERIFY(peer.finish());
    }

    void identicalExecutableBytesAtAnotherPathDoNotAuthorizeTheHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto copied = directory.filePath(QStringLiteral("cópia-peer.exe"));
        QVERIFY(QFile::copy(helper_executable, copied));
        const auto expected_hash = fileHash(helper_executable);
        QVERIFY(!expected_hash.isEmpty());
        QCOMPARE(fileHash(copied), expected_hash);
        const auto name = endpoint();
        const auto self = QCoreApplication::applicationFilePath();
        Submission owner;
        QtNativeDownloadServer server{configuration(name, self, helper_executable), owner};
        QVERIFY(server.start());
        Child peer;
        QVERIFY(peer.start(name, QStringLiteral("raw-host"), self, copied));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("READY"), 1, 3'000);
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("CONNECTED"), 1, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(peer.count("CLOSED") != 0 || owner.begins != 0, 3'000);
        QCOMPARE(owner.begins, 0);
        QCOMPARE(peer.count("REPLIED"), 0);
        QVERIFY(peer.finish());
        server.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(server.stopped(), 3'000);
    }

    void missingExpectedOwnerImageFailsClosedBeforeAnyBytes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto missing = directory.filePath(QStringLiteral("missing-owner.exe"));
        QVERIFY(!QFileInfo::exists(missing));
        const auto name = endpoint();
        const auto self = QCoreApplication::applicationFilePath();
        Child peer;
        QVERIFY(peer.start(name, QStringLiteral("raw-owner"), self));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("READY"), 1, 3'000);
        QtNativeDownloadClient client{configuration(name, missing, self)};
        const auto initial = client.begin(request());
        std::optional<DownloadSubmissionResult> answer;
        bool uncertain = false;
        if (initial.state == DownloadAdmissionState::Completed)
            answer = initial.result;
        QTRY_VERIFY_WITH_TIMEOUT(
            answer.has_value() || observe(client, answer, uncertain) || peer.count("RECEIVED") != 0, 3'000);
        QCOMPARE(peer.count("RECEIVED"), 0);
        QVERIFY(!uncertain);
        QVERIFY(answer);
        QCOMPARE(answer->error, DownloadSubmissionError::NotAvailable);
        QVERIFY(peer.command("probe\n"));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("ZERO"), 1, 3'000);
        QCOMPARE(peer.count("NONZERO"), 0);
        client.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(client.stopped(), 3'000);
        QVERIFY(peer.finish());
    }

    void exitedPeerCannotProduceAcceptanceOrTriggerAReconnect() {
        const auto name = endpoint();
        const auto self = QCoreApplication::applicationFilePath();
        Child peer;
        QVERIFY(peer.start(name, QStringLiteral("exiting-owner"), self));
        QTRY_COMPARE_WITH_TIMEOUT(peer.count("READY"), 1, 3'000);
        QtNativeDownloadClient client{configuration(name, helper_executable, self)};
        const auto initial = client.begin(request());
        std::optional<DownloadSubmissionResult> answer;
        bool uncertain = false;
        if (initial.state == DownloadAdmissionState::Completed)
            answer = initial.result;
        QTRY_COMPARE_WITH_TIMEOUT(peer.process.state(), QProcess::NotRunning, 3'000);
        QCOMPARE(peer.count("CONNECTED"), 1);
        QTRY_VERIFY_WITH_TIMEOUT(answer.has_value() || observe(client, answer, uncertain), 3'000);
        QVERIFY(uncertain ||
                (answer && answer->error == DownloadSubmissionError::NotAvailable && answer->job_ids.empty()));
        QLocalServer late_owner;
        QVERIFY(late_owner.listen(name));
        QSignalSpy arrivals{&late_owner, &QLocalServer::newConnection};
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        QCoreApplication::processEvents();
        QCOMPARE(arrivals.size(), 0);
        client.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(client.stopped(), 3'000);
        QVERIFY(peer.finish());
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    auto arguments = application.arguments();
    if (arguments.size() < 2)
        return 2;
    helper_executable = QFileInfo{arguments.takeAt(1)}.absoluteFilePath();
    NativeDownloadPeerProcessTests tests;
    return QTest::qExec(&tests, arguments);
}

#include "NativeDownloadPeerProcessTests.moc"
