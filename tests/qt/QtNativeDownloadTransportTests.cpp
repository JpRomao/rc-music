#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/QtNativeDownloadClient.hpp"
#include "adapters/QtNativeDownloadServer.hpp"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QtEndian>

#include <functional>
#include <memory>
#include <stdexcept>

using namespace music_player::m0;
namespace {
const QString origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";
DownloadRequest request() {
    return {.request_id = request_id,
            .items = {{"https://fixture.example.test/music.mp3?private=value", "Áudio", DownloadMediaKind::Audio}}};
}
QByteArray payload() {
    return R"({"schemaVersion":1,"type":"download.request","requestId":"01234567-89ab-4cde-8fab-0123456789ab","items":[{"url":"https://fixture.example.test/music.mp3?private=value","title":"Áudio","kind":"audio"}]})";
}
NativeHostConfiguration configuration() {
    return {origin, QStringLiteral("rc-music-native-transport-") + QUuid::createUuid().toString(QUuid::Id128)};
}
QByteArray framedRequest(const QString& caller = origin) {
    return native_download_ipc::preamble(caller) + encodeNativeMessageFrame(payload());
}
class Submission final : public AsyncDownloadSubmission {
  public:
    int begins{0};
    int polls{0};
    DownloadRequest received{};
    DownloadAdmissionResult result{.state = DownloadAdmissionState::Pending};
    std::optional<DownloadSubmissionResult> completion{};
    std::function<void()> during_begin{};
    bool throw_begin{false};
    bool throw_poll{false};
    DownloadAdmissionResult begin(const DownloadRequest& value) override {
        ++begins;
        received = value;
        if (during_begin)
            during_begin();
        if (throw_begin)
            throw std::runtime_error{"private admission failure"};
        return result;
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view id) override {
        ++polls;
        if (id != request_id)
            throw std::runtime_error{"unmatched request"};
        if (throw_poll)
            throw std::runtime_error{"private observation failure"};
        return completion;
    }
};
void pump() { QCoreApplication::processEvents(); }
} // namespace

class QtNativeDownloadTransportTests final : public QObject {
    Q_OBJECT
  private slots:
    void pendingHasNoWireAcknowledgementThenMatchingCompletionReplies() {
        Submission owner;
        qint64 now = 0;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.now_ms = [&] { return now; }, .automatic_polling = false}};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto bytes = framedRequest();
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(owner.begins, 1);
        server.poll();
        pump();
        QCOMPARE(socket.bytesAvailable(), qint64{0});
        QCOMPARE(owner.received.request_id, request_id);
        owner.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"job-1"}};
        server.poll();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        const auto response = socket.readAll();
        QVERIFY(response.size() > 4);
        const auto decoded = decodeNativeDownloadResponse(response.mid(4), request_id, 1);
        QVERIFY(decoded);
        QCOMPARE(decoded->job_ids, std::vector<std::string>{"job-1"});
        QCOMPARE(owner.begins, 1);
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void clientConnectsOnceAndWaitsForDurableReceipt() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner};
        QVERIFY(server.start());
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE(owner.begins, 1);
        QVERIFY(!client.poll(request_id));
        owner.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"job-1"}};
        std::optional<DownloadSubmissionResult> answer;
        QTRY_VERIFY((answer = client.poll(request_id)).has_value());
        QCOMPARE(answer->error, DownloadSubmissionError::None);
        QCOMPARE(answer->job_ids, std::vector<std::string>{"job-1"});
        QCOMPARE(owner.begins, 1);
        client.shutdown();
        QVERIFY(client.stopped());
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void fragmentedPreambleAndBodyDispatchOnlyCompleteRequest() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto bytes = framedRequest();
        for (qsizetype i = 0; i < bytes.size(); ++i) {
            QCOMPARE(socket.write(bytes.mid(i, 1)), qint64{1});
            pump();
            if (i + 1 < bytes.size())
                QCOMPARE(owner.begins, 0);
        }
        QTRY_COMPARE(owner.begins, 1);
        server.beginShutdown();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(server.stopped());
    }
    void wrongOriginAndCoalescedFramesNeverDispatch() {
        for (int variant = 0; variant < 2; ++variant) {
            Submission owner;
            const auto config = configuration();
            QtNativeDownloadServer server{config, owner};
            QVERIFY(server.start());
            QLocalSocket socket;
            socket.connectToServer(config.endpoint_name);
            QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
            const auto bytes =
                variant == 0 ? framedRequest(QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"))
                             : framedRequest() + encodeNativeMessageFrame(payload());
            QCOMPARE(socket.write(bytes), bytes.size());
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
            QCOMPARE(owner.begins, 0);
            QVERIFY(socket.readAll().isEmpty());
            server.beginShutdown();
        }
    }
    void deadlineIsTotalAndShutdownDropsObserverOnly() {
        Submission owner;
        qint64 now = 0;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.now_ms = [&] { return now; }, .automatic_polling = false}};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 1);
        now = 4'999;
        server.poll();
        pump();
        QCOMPARE(socket.state(), QLocalSocket::ConnectedState);
        now = 5'000;
        server.poll();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(owner.begins, 1);
        owner.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"job-1"}};
        server.beginShutdown();
        QVERIFY(server.stopped());
        QCOMPARE(owner.completion->job_ids.front(), std::string{"job-1"});
    }
    void falseOrReentrantAvailabilityDoesNotDispatch() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer* ingress = nullptr;
        QtNativeDownloadServer server{config, owner, {.available = [&] {
                                          ingress->beginShutdown();
                                          return true;
                                      }}};
        ingress = &server;
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(owner.begins, 0);
        QVERIFY(server.stopped());
    }
    void capsAndObserverDisconnectPreserveOwnedPendingBatch() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.max_active_connections = 1}};
        QVERIFY(server.start());
        QLocalSocket first;
        first.connectToServer(config.endpoint_name);
        QTRY_COMPARE(first.state(), QLocalSocket::ConnectedState);
        QCOMPARE(first.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 1);
        QLocalSocket excess;
        excess.connectToServer(config.endpoint_name);
        QTRY_COMPARE(excess.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(server.activeConnectionCount(), qsizetype{1});
        first.abort();
        QTRY_COMPARE(server.activeConnectionCount(), qsizetype{0});
        QCOMPARE(owner.begins, 1);
        QCOMPARE(owner.received.request_id, request_id);
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void malformedDoubleAndMismatchedRepliesRemainUncertainWithoutReconnect() {
        for (int variant = 0; variant < 3; ++variant) {
            const auto config = configuration();
            QLocalServer server;
            QVERIFY(server.listen(config.endpoint_name));
            QSignalSpy arrivals{&server, &QLocalServer::newConnection};
            QtNativeDownloadClient client{config};
            QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
            QTRY_VERIFY(server.hasPendingConnections());
            std::unique_ptr<QLocalSocket> socket{server.nextPendingConnection()};
            QTRY_VERIFY(socket->bytesAvailable() > 0);
            socket->readAll();
            QByteArray bytes;
            if (variant == 0)
                bytes = encodeNativeMessageFrame("not json");
            else {
                const auto id = variant == 2 ? std::string{"11111111-1111-4111-8111-111111111111"} : request_id;
                bytes = encodeNativeMessageFrame(
                    encodeNativeDownloadResponse(id, {.error = DownloadSubmissionError::None, .job_ids = {"job-1"}}));
                if (variant == 1)
                    bytes += bytes;
            }
            QCOMPARE(socket->write(bytes), bytes.size());
            socket->disconnectFromServer();
            bool uncertain = false;
            QTRY_VERIFY(([&] {
                try {
                    static_cast<void>(client.poll(request_id));
                } catch (...) {
                    uncertain = true;
                }
                return uncertain;
            })());
            QCOMPARE(arrivals.size(), 1);
            QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
            pump();
            QCOMPARE(arrivals.size(), 1);
            client.shutdown();
            QVERIFY(client.stopped());
        }
    }
    void hostilePreambleAndOversizedPrefixNeverDispatch() {
        for (int variant = 0; variant < 4; ++variant) {
            Submission owner;
            const auto config = configuration();
            QtNativeDownloadServer server{config, owner};
            QVERIFY(server.start());
            QLocalSocket socket;
            socket.connectToServer(config.endpoint_name);
            QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
            auto bytes = framedRequest();
            if (variant == 0)
                bytes[0] = 'X';
            else if (variant == 1)
                bytes[8] = 2;
            else {
                bytes = native_download_ipc::preamble(origin);
                QByteArray prefix(4, Qt::Uninitialized);
                qToLittleEndian<quint32>(variant == 2 ? 0 : native_message_max_payload_bytes + 1, prefix.data());
                bytes += prefix;
            }
            QCOMPARE(socket.write(bytes), bytes.size());
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
            QCOMPARE(owner.begins, 0);
            QVERIFY(socket.readAll().isEmpty());
            server.beginShutdown();
        }
    }
    void unavailableOwnerProducesOnlyCorrelatedRejection() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.available = [] { return false; }}};
        QVERIFY(server.start());
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        std::optional<DownloadSubmissionResult> answer;
        QTRY_VERIFY((answer = client.poll(request_id)).has_value());
        QCOMPARE(answer->error, DownloadSubmissionError::NotAvailable);
        QVERIFY(answer->job_ids.empty());
        QCOMPARE(owner.begins, 0);
        server.beginShutdown();
        client.shutdown();
    }
    void reentrantOwnerShutdownDoesNotDestroyActiveSessionStack() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner};
        owner.during_begin = [&] { server.beginShutdown(); };
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 1);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(socket.readAll().isEmpty());
        QVERIFY(server.stopped());
        QCOMPARE(owner.received.request_id, request_id);
    }
    void regressiveClockFailsClosedAndInvalidConfigurationDoesNotListen() {
        Submission owner;
        const auto config = configuration();
        qint64 now = 10;
        QtNativeDownloadServer server{config, owner, {.now_ms = [&] { return now; }, .automatic_polling = false}};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        now = 9;
        server.poll();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(server.stopped());
        QCOMPARE(owner.begins, 0);
        QtNativeDownloadServer disabled{{}, owner};
        QVERIFY(!disabled.start());
        disabled.beginShutdown();
        QVERIFY(disabled.stopped());
    }
    void clientUnavailableBeforeWriteIsNotAvailableWithoutRetry() {
        const auto config = configuration();
        QtNativeDownloadClient client{config};
        const auto initial = client.begin(request());
        std::optional<DownloadSubmissionResult> answer;
        if (initial.state == DownloadAdmissionState::Completed)
            answer = initial.result;
        else
            QTRY_VERIFY((answer = client.poll(request_id)).has_value());
        QCOMPARE(answer->error, DownloadSubmissionError::NotAvailable);
        QVERIFY(answer->job_ids.empty());
        QLocalServer late_server;
        QVERIFY(late_server.listen(config.endpoint_name));
        QSignalSpy arrivals{&late_server, &QLocalServer::newConnection};
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        pump();
        QCOMPARE(arrivals.size(), 0);
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void clientPostWriteTimeoutDoesNotClaimRejectionOrReconnect() {
        const auto config = configuration();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        qint64 now = 0;
        QtNativeDownloadClient client{config, {.now_ms = [&] { return now; }}};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket{server.nextPendingConnection()};
        QTRY_VERIFY(socket->bytesAvailable() > 0);
        now = 4'999;
        QVERIFY(!client.poll(request_id));
        now = 5'000;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(client.poll(request_id)));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(client.poll(request_id)));
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        pump();
        QVERIFY(!server.hasPendingConnections());
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void clientSealsOnlyOneBoundedReplyAfterPeerClosure() {
        const auto config = configuration();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket{server.nextPendingConnection()};
        QTRY_VERIFY(socket->bytesAvailable() > 0);
        const auto frame = encodeNativeMessageFrame(
            encodeNativeDownloadResponse(request_id, {.error = DownloadSubmissionError::None, .job_ids = {"job-1"}}));
        QCOMPARE(socket->write(frame), frame.size());
        pump();
        QVERIFY(!client.poll(request_id));
        socket->disconnectFromServer();
        std::optional<DownloadSubmissionResult> answer;
        QTRY_VERIFY((answer = client.poll(request_id)).has_value());
        QCOMPARE(answer->job_ids, std::vector<std::string>{"job-1"});
        QVERIFY(!client.poll("11111111-1111-4111-8111-111111111111"));
        client.shutdown();
    }
    void maximumFrameIsAdmittedWithoutRelaxingLimit() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        auto body = payload();
        body += QByteArray(native_message_max_payload_bytes - body.size(), ' ');
        QCOMPARE(body.size(), native_message_max_payload_bytes);
        const auto bytes = native_download_ipc::preamble(origin) + encodeNativeMessageFrame(body);
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(owner.begins, 1);
        QCOMPARE(owner.received.request_id, request_id);
        QCOMPARE(socket.bytesAvailable(), qint64{0});
        server.beginShutdown();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(server.stopped());
    }
    void partialPreambleAndBodyCannotRenewAssemblyDeadline() {
        Submission owner;
        qint64 now = 0;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.now_ms = [&] { return now; }, .automatic_polling = false}};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QTRY_COMPARE(server.activeConnectionCount(), qsizetype{1});
        const auto bytes = framedRequest();
        QCOMPARE(socket.write(bytes.first(10)), qint64{10});
        pump();
        now = 4'999;
        QCOMPARE(socket.write(bytes.mid(10, native_download_ipc::preamble_bytes - 10 + 5)),
                 native_download_ipc::preamble_bytes - 10 + 5);
        pump();
        server.poll();
        QCOMPARE(owner.begins, 0);
        now = 5'000;
        server.poll();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(owner.begins, 0);
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void ownerExceptionsNeverEmitAcknowledgementOrSensitiveError() {
        for (int variant = 0; variant < 2; ++variant) {
            Submission owner;
            owner.throw_begin = variant == 0;
            owner.throw_poll = variant == 1;
            const auto config = configuration();
            QtNativeDownloadServer server{config, owner};
            QVERIFY(server.start());
            QLocalSocket socket;
            socket.connectToServer(config.endpoint_name);
            QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
            QCOMPARE(socket.write(framedRequest()), framedRequest().size());
            QTRY_COMPARE(owner.begins, 1);
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
            QVERIFY(socket.readAll().isEmpty());
            server.beginShutdown();
        }
    }
    void responseOversizedPrefixIsUncertainBeforeBodyAllocation() {
        const auto config = configuration();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket{server.nextPendingConnection()};
        QTRY_VERIFY(socket->bytesAvailable() > 0);
        QByteArray prefix(4, Qt::Uninitialized);
        qToLittleEndian<quint32>(native_download_ipc::response_max_payload_bytes + 1, prefix.data());
        QCOMPARE(socket->write(prefix), qint64{4});
        bool uncertain = false;
        QTRY_VERIFY(([&] {
            try {
                static_cast<void>(client.poll(request_id));
            } catch (const std::runtime_error&) {
                uncertain = true;
            }
            return uncertain;
        })());
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(!server.hasPendingConnections());
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void connectionLimitsOutsideOneToEightRefuseListening() {
        for (const qsizetype limit : {qsizetype{0}, qsizetype{9}}) {
            Submission owner;
            const auto config = configuration();
            QtNativeDownloadServer server{config, owner, {.max_active_connections = limit}};
            QVERIFY(!server.start());
            QCOMPARE(owner.begins, 0);
            server.beginShutdown();
            QVERIFY(server.stopped());
        }
    }
    void nestedPollDuringAdmissionCannotConsumeTheActiveSession() {
        Submission owner;
        owner.result = {.state = DownloadAdmissionState::Completed,
                        .result = {.error = DownloadSubmissionError::None, .job_ids = {"job-1"}}};
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner};
        owner.during_begin = [&] { server.poll(); };
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 1);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        const auto response = socket.readAll();
        QVERIFY(response.size() > 4);
        const auto decoded = decodeNativeDownloadResponse(response.mid(4), request_id, 1);
        QVERIFY(decoded);
        QCOMPARE(decoded->error, DownloadSubmissionError::None);
        QCOMPARE(owner.polls, 0);
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void shutdownFromClockCannotLaunchAConnectionAfterClosure() {
        const auto config = configuration();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QSignalSpy arrivals{&server, &QLocalServer::newConnection};
        QtNativeDownloadClient* observer = nullptr;
        QtNativeDownloadClient client{config, {.now_ms = [&] {
                                          observer->shutdown();
                                          return qint64{0};
                                      }}};
        observer = &client;
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Completed);
        QVERIFY(client.stopped());
        pump();
        QCOMPARE(arrivals.size(), 0);
        QVERIFY(!client.poll(request_id));
    }
    void nestedServerPollFromClockObservesTheClockOnlyOnce() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer* ingress = nullptr;
        bool nest_once = false;
        int clock_reads = 0;
        QtNativeDownloadServer server{config,
                                      owner,
                                      {.now_ms =
                                           [&] {
                                               ++clock_reads;
                                               if (nest_once) {
                                                   nest_once = false;
                                                   ingress->poll();
                                               }
                                               return qint64{0};
                                           },
                                       .automatic_polling = false}};
        ingress = &server;
        QVERIFY(server.start());
        clock_reads = 0;
        nest_once = true;
        server.poll();
        QCOMPARE(clock_reads, 1);
        QCOMPARE(owner.begins, 0);
        QCOMPARE(owner.polls, 0);
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void shutdownDuringAdmissionIsNotDrainedUntilCallbackUnwinds() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.automatic_polling = false}};
        bool observed_stopped_in_callback = true;
        owner.during_begin = [&] {
            server.beginShutdown();
            observed_stopped_in_callback = server.stopped();
        };
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 1);
        QVERIFY(!observed_stopped_in_callback);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QTRY_VERIFY(server.stopped());
        QCOMPARE(owner.received.request_id, request_id);
        QCOMPARE(owner.polls, 0);
        QVERIFY(socket.readAll().isEmpty());
    }
    void shutdownDuringPollClockIsNotDrainedUntilPollUnwinds() {
        Submission owner;
        const auto config = configuration();
        QtNativeDownloadServer* ingress = nullptr;
        bool close_during_clock = false;
        bool observed_stopped_in_clock = true;
        QtNativeDownloadServer server{config,
                                      owner,
                                      {.now_ms =
                                           [&] {
                                               if (close_during_clock) {
                                                   close_during_clock = false;
                                                   ingress->beginShutdown();
                                                   observed_stopped_in_clock = ingress->stopped();
                                               }
                                               return qint64{0};
                                           },
                                       .automatic_polling = false}};
        ingress = &server;
        QVERIFY(server.start());
        close_during_clock = true;
        server.poll();
        QVERIFY(!observed_stopped_in_clock);
        QVERIFY(server.stopped());
        QCOMPARE(owner.begins, 0);
        QCOMPARE(owner.polls, 0);
    }
};
QTEST_GUILESS_MAIN(QtNativeDownloadTransportTests)
#include "QtNativeDownloadTransportTests.moc"
