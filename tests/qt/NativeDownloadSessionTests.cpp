#include "adapters/NativeDownloadSession.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <functional>
#include <stdexcept>

using namespace music_player::m0;

namespace {

const QString origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";

QByteArray requestPayload() {
    return R"({"schemaVersion":1,"type":"download.request","requestId":"01234567-89ab-4cde-8fab-0123456789ab","items":[{"url":"https://media.example.test/a.mp3?secret=preserved","title":"Audio","kind":"audio"}]})";
}

QByteArray framePayload(const QByteArray& payload) {
    quint32 size = static_cast<quint32>(payload.size());
    QByteArray framed(reinterpret_cast<const char*>(&size), sizeof(size));
    framed.append(payload);
    return framed;
}

class Submission final : public DownloadSubmission {
  public:
    int calls{0};
    bool fail{false};
    std::function<void()> during_admission{};
    DownloadRequest received{};
    DownloadSubmissionResult result{.error = DownloadSubmissionError::None, .job_ids = {"job-1"}};

    DownloadSubmissionResult submit(const DownloadRequest& request) override {
        ++calls;
        received = request;
        if (during_admission)
            during_admission();
        if (fail)
            throw std::runtime_error("sensitive raw backend failure");
        return result;
    }
};

QJsonObject responseObject(const NativeSessionResult& reply) {
    return QJsonDocument::fromJson(reply.response_frame.mid(4)).object();
}

} // namespace

class NativeDownloadSessionTests final : public QObject {
    Q_OBJECT

  private slots:
    void acceptsOnlyAfterAuthorizedCompleteRequestAndReturnsCorrelatedReceipt() {
        Submission port;
        NativeDownloadSession session(origin, origin, port);
        const auto bytes = framePayload(requestPayload());
        QCOMPARE(session.receive(QByteArrayView(bytes).first(3), 1).status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.calls, 0);
        const auto reply = session.receive(QByteArrayView(bytes).sliced(3), 2);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(port.calls, 1);
        QCOMPARE(port.received.request_id, request_id);
        QCOMPARE(port.received.items.size(), std::size_t{1});
        QCOMPARE(port.received.items.front().url, std::string("https://media.example.test/a.mp3?secret=preserved"));
        const auto json = responseObject(reply);
        QCOMPARE(json.value("requestId").toString().toStdString(), request_id);
        QCOMPARE(json.value("status").toString(), QStringLiteral("accepted"));
        QCOMPARE(json.value("jobIds").toArray(), QJsonArray{QStringLiteral("job-1")});
        QCOMPARE(session.receive(bytes, 3).status, NativeSessionStatus::Closed);
        QCOMPARE(port.calls, 1);
    }

    void rejectsMissingMismatchedOrMalformedOriginBeforeAdmission_data() {
        QTest::addColumn<QString>("caller");
        QTest::addColumn<QString>("allowed");
        QTest::newRow("missing caller") << QString{} << origin;
        QTest::newRow("missing configuration") << origin << QString{};
        QTest::newRow("other extension") << QStringLiteral("chrome-extension://pppppppppppppppppppppppppppppppp/")
                                         << origin;
        QTest::newRow("prefix spoof") << origin + QStringLiteral("evil") << origin;
        QTest::newRow("query") << origin + QStringLiteral("?source=trusted") << origin;
        QTest::newRow("page origin") << QStringLiteral("https://page.example.test/") << origin;
        QTest::newRow("both invalid") << QStringLiteral("trusted") << QStringLiteral("trusted");
        QTest::newRow("wildcard") << QStringLiteral("chrome-extension://*/") << QStringLiteral("chrome-extension://*/");
    }

    void rejectsMissingMismatchedOrMalformedOriginBeforeAdmission() {
        QFETCH(QString, caller);
        QFETCH(QString, allowed);
        Submission port;
        NativeDownloadSession session(caller, allowed, port);
        const auto reply = session.receive(framePayload(requestPayload()), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Closed);
        QVERIFY(reply.response_frame.isEmpty());
        QCOMPARE(port.calls, 0);
    }

    void malformedOrPartiallyInvalidBatchNeverReachesAdmission() {
        for (const auto& bytes :
             {QByteArray("{invalid"), requestPayload().replace("a.mp3?secret=preserved", "a.mp3#fragment"),
              requestPayload().replace("\"kind\":\"audio\"", "\"kind\":\"audio\",\"command\":\"run\"")}) {
            Submission port;
            NativeDownloadSession session(origin, origin, port);
            const auto reply = session.receive(framePayload(bytes), 0);
            QVERIFY(reply.status != NativeSessionStatus::Awaiting);
            QCOMPARE(port.calls, 0);
            QVERIFY(reply.response_frame.isEmpty() || responseObject(reply).value("status").toString() == "rejected");
        }
    }

    void serviceRejectionHasNoSuccessfulAckOrLeakedDetails() {
        Submission port;
        port.result = {.error = DownloadSubmissionError::Busy};
        NativeDownloadSession session(origin, origin, port);
        const auto reply = session.receive(framePayload(requestPayload()), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(reply).value("status").toString(), QStringLiteral("rejected"));
        QCOMPARE(responseObject(reply).value("code").toString(), QStringLiteral("busy"));
        QVERIFY(!reply.response_frame.contains("secret"));
        QVERIFY(!reply.response_frame.contains("jobIds"));
    }

    void malformedAdmissionReceiptOrExceptionClosesWithoutFalseAck_data() {
        QTest::addColumn<int>("scenario");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }

    void malformedAdmissionReceiptOrExceptionClosesWithoutFalseAck() {
        QFETCH(int, scenario);
        Submission port;
        if (scenario == 0)
            port.result.job_ids.clear();
        if (scenario == 1)
            port.result.job_ids = {"one", "two"};
        if (scenario == 2)
            port.result.job_ids = {"../../path"};
        if (scenario == 3)
            port.fail = true;
        if (scenario == 4)
            port.result = {.error = DownloadSubmissionError::Busy, .job_ids = {"one"}};
        NativeDownloadSession session(origin, origin, port);
        const auto reply = session.receive(framePayload(requestPayload()), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Closed);
        QVERIFY(reply.response_frame.isEmpty());
        QCOMPARE(port.calls, 1);
        (void)session.receive(framePayload(requestPayload()), 1);
        QCOMPARE(port.calls, 1);
    }

    void totalDeadlineAndEofAreTerminalWithoutDispatch() {
        Submission port;
        NativeDownloadSession session(origin, origin, port, 100);
        QCOMPARE(session.receive(QByteArrayView("x", 1), 101).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.poll(5100).status, NativeSessionStatus::Expired);
        QVERIFY(session.receive(framePayload(requestPayload()), 5101).response_frame.isEmpty());
        QCOMPARE(port.calls, 0);
        NativeDownloadSession eof(origin, origin, port);
        QCOMPARE(eof.finishInput(0).status, NativeSessionStatus::Closed);
        QVERIFY(eof.receive(framePayload(requestPayload()), 1).response_frame.isEmpty());
    }

    void coalescedRequestsCannotCauseTwoAdmissions() {
        Submission port;
        NativeDownloadSession session(origin, origin, port);
        const auto bytes = framePayload(requestPayload());
        QCOMPARE(session.receive(bytes + bytes, 0).status, NativeSessionStatus::Closed);
        QCOMPARE(port.calls, 0);
    }

    void reentrantAdmissionCannotDispatchOrReplyTwice() {
        Submission port;
        NativeDownloadSession session(origin, origin, port);
        port.during_admission = [&] {
            const auto reentrant = session.receive(framePayload(requestPayload()), 1);
            QCOMPARE(reentrant.status, NativeSessionStatus::Closed);
            QVERIFY(reentrant.response_frame.isEmpty());
            QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
            QCOMPARE(session.finishInput(3).status, NativeSessionStatus::Closed);
        };
        const auto reply = session.receive(framePayload(requestPayload()), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(port.calls, 1);
        QVERIFY(!reply.response_frame.isEmpty());
    }
};

QTEST_APPLESS_MAIN(NativeDownloadSessionTests)
#include "NativeDownloadSessionTests.moc"
