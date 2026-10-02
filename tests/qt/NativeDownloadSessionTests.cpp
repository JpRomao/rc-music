#include "adapters/NativeDownloadSession.hpp"
#include "m0/application/DownloadAdmissionController.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <functional>
#include <limits>
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

class AsyncSubmission final : public AsyncDownloadSubmission {
  public:
    int begins{0};
    int polls{0};
    bool throw_begin{false};
    bool throw_poll{false};
    std::function<void()> during_begin{};
    std::function<void()> during_poll{};
    DownloadRequest owned{};
    std::vector<std::string> observed_ids{};
    DownloadAdmissionResult admission{.state = DownloadAdmissionState::Pending};
    std::optional<DownloadSubmissionResult> completion{};

    DownloadAdmissionResult begin(const DownloadRequest& request) override {
        ++begins;
        owned = request;
        if (during_begin)
            during_begin();
        if (throw_begin)
            throw std::runtime_error("private raw request in uncertain admission");
        return admission;
    }
    std::optional<DownloadSubmissionResult> poll(const std::string_view id) override {
        ++polls;
        observed_ids.emplace_back(id);
        if (during_poll)
            during_poll();
        if (throw_poll)
            throw std::runtime_error("private raw completion in uncertain admission");
        return completion;
    }
};

class Execution final : public DownloadExecution {
  public:
    int starts{0};
    int polls{0};
    int cancels{0};
    DownloadExecutionUpdate update{};
    bool start(const std::string& id, const DownloadItem&) override {
        ++starts;
        update = {.job_id = id};
        return true;
    }
    void cancel(const std::string_view) override {
        ++cancels;
        update.state = DownloadExecutionState::Cancelled;
    }
    DownloadExecutionUpdate poll(const std::string_view) override {
        ++polls;
        return update;
    }
};

class Importer final : public DownloadImport {
  public:
    int calls{0};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};

class Identity final : public DownloadRequestIdentity {
  public:
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};

class CheckpointIo final : public DownloadCheckpointIo {
  public:
    int loads{0};
    int saves{0};
    bool closing{false};
    std::optional<std::uint64_t> active{};
    DownloadCheckpointOperation operation{DownloadCheckpointOperation::Load};
    std::optional<DownloadCheckpointIoResult> completion{};
    DownloadCheckpoint requested{};
    DownloadCheckpoint persisted{};

    DownloadCheckpointIoCommand startLoad(const std::uint64_t id) override {
        if (active || closing)
            return DownloadCheckpointIoCommand::Busy;
        ++loads;
        active = id;
        operation = DownloadCheckpointOperation::Load;
        return DownloadCheckpointIoCommand::Accepted;
    }
    DownloadCheckpointIoCommand startSave(const std::uint64_t id, const DownloadCheckpoint& checkpoint) override {
        if (active || closing)
            return DownloadCheckpointIoCommand::Busy;
        ++saves;
        active = id;
        operation = DownloadCheckpointOperation::Save;
        requested = checkpoint;
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(const std::uint64_t id) override {
        if (!active || *active != id || !completion)
            return std::nullopt;
        auto result = std::move(completion);
        completion.reset();
        active.reset();
        return result;
    }
    void shutdown() override { closing = true; }
    bool stopped() const override { return closing && !active; }
    void completeMissingLoad() {
        completion = DownloadCheckpointIoResult{.operation_id = active.value_or(0),
                                                .operation = DownloadCheckpointOperation::Load,
                                                .load = {.status = DownloadCheckpointLoadStatus::Missing}};
    }
    void completeSave(const DownloadCheckpointSaveStatus status) {
        // Failed can be reported after the exact bytes were committed. This
        // evidence is not a Saved acknowledgement and is never promoted by readback.
        persisted = requested;
        completion = DownloadCheckpointIoResult{
            .operation_id = active.value_or(0), .operation = DownloadCheckpointOperation::Save, .save_status = status};
    }
};

struct ControlledAdmission final {
    Execution execution;
    Importer importer;
    Identity identity;
    CheckpointIo io;
    DownloadService service{execution, importer, "native", {}, &identity};
    DownloadAdmissionController controller{service, io};
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

    void asyncPendingHasNoWireReceiptUntilOwnerCompletesDurably() {
        AsyncSubmission port;
        NativeDownloadSession session(origin, origin, port, 100);
        const auto bytes = framePayload(requestPayload());
        QCOMPARE(session.receive(QByteArrayView(bytes).first(3), 101).status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.begins, 0);
        const auto pending = session.receive(QByteArrayView(bytes).sliced(3), 200);
        QCOMPARE(pending.status, NativeSessionStatus::Awaiting);
        QVERIFY(pending.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.owned.request_id, request_id);
        QCOMPARE(port.owned.items.size(), std::size_t{1});
        const auto still_pending = session.poll(201);
        QCOMPARE(still_pending.status, NativeSessionStatus::Awaiting);
        QVERIFY(still_pending.response_frame.isEmpty());
        QCOMPARE(port.observed_ids, std::vector<std::string>{request_id});
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        const auto reply = session.poll(202);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        const auto object = responseObject(reply);
        QCOMPARE(object.value("schemaVersion"), QJsonValue{1});
        QCOMPARE(object.value("type").toString(), QStringLiteral("download.response"));
        QCOMPARE(object.value("requestId").toString().toStdString(), request_id);
        QCOMPARE(object.value("status").toString(), QStringLiteral("accepted"));
        QCOMPARE(object.value("jobIds").toArray(), QJsonArray{QStringLiteral("saved-1")});
        QVERIFY(!reply.response_frame.contains("secret"));
        QVERIFY(!reply.response_frame.contains("pending"));
        QCOMPARE(session.poll(203).status, NativeSessionStatus::Closed);
        QCOMPARE(session.receive(bytes, 204).status, NativeSessionStatus::Closed);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 2);
    }

    void asyncOriginAndInvalidRequestNeverDispatch() {
        AsyncSubmission port;
        NativeDownloadSession unauthorized(QStringLiteral("https://page.example.test/"), origin, port);
        QCOMPARE(unauthorized.receive(framePayload(requestPayload()), 0).status, NativeSessionStatus::Closed);
        QCOMPARE(port.begins, 0);
        const auto invalid = requestPayload().replace("a.mp3?secret=preserved", "a.mp3#fragment");
        NativeDownloadSession malformed(origin, origin, port);
        const auto reply = malformed.receive(framePayload(invalid), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(reply).value("status").toString(), QStringLiteral("rejected"));
        QCOMPARE(responseObject(reply).value("code").toString(), QStringLiteral("unsupported_source"));
        QCOMPARE(port.begins, 0);
        QCOMPARE(port.polls, 0);
    }

    void asyncTypedRejectionsUseTheExistingVersionOneWire_data() {
        QTest::addColumn<int>("error");
        QTest::addColumn<QString>("code");
        QTest::newRow("invalid") << static_cast<int>(DownloadSubmissionError::InvalidRequest)
                                 << QStringLiteral("invalid_request");
        QTest::newRow("unsupported") << static_cast<int>(DownloadSubmissionError::UnsupportedSource)
                                     << QStringLiteral("unsupported_source");
        QTest::newRow("busy") << static_cast<int>(DownloadSubmissionError::Busy) << QStringLiteral("busy");
        QTest::newRow("unavailable") << static_cast<int>(DownloadSubmissionError::NotAvailable)
                                     << QStringLiteral("not_available");
        QTest::newRow("limit") << static_cast<int>(DownloadSubmissionError::LimitExceeded)
                               << QStringLiteral("limit_exceeded");
    }

    void asyncTypedRejectionsUseTheExistingVersionOneWire() {
        QFETCH(int, error);
        QFETCH(QString, code);
        for (const auto immediate : {false, true}) {
            AsyncSubmission port;
            const DownloadSubmissionResult rejection{.error = static_cast<DownloadSubmissionError>(error)};
            if (immediate)
                port.admission = {.state = DownloadAdmissionState::Completed, .result = rejection};
            else
                port.completion = rejection;
            NativeDownloadSession session(origin, origin, port);
            const auto initial = session.receive(framePayload(requestPayload()), 0);
            const auto reply = immediate ? initial : session.poll(1);
            QCOMPARE(reply.status, NativeSessionStatus::Replied);
            QCOMPARE(responseObject(reply).value("requestId").toString().toStdString(), request_id);
            QCOMPARE(responseObject(reply).value("status").toString(), QStringLiteral("rejected"));
            QCOMPARE(responseObject(reply).value("code").toString(), code);
            QVERIFY(!reply.response_frame.contains("jobIds"));
            QCOMPARE(port.begins, 1);
            QCOMPARE(port.polls, immediate ? 0 : 1);
        }
    }

    void asyncImmediateCompletedReceiptCanReplyWithoutPolling() {
        AsyncSubmission port;
        port.admission = {.state = DownloadAdmissionState::Completed,
                          .result = {.error = DownloadSubmissionError::None, .job_ids = {"durable-1"}}};
        NativeDownloadSession session(origin, origin, port);
        const auto reply = session.receive(framePayload(requestPayload()), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(reply).value("jobIds").toArray(), QJsonArray{QStringLiteral("durable-1")});
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
    }

    void asyncPendingCannotContainAnEarlyOrContradictoryReceipt_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 5; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }

    void asyncPendingCannotContainAnEarlyOrContradictoryReceipt() {
        QFETCH(int, scenario);
        AsyncSubmission port;
        if (scenario == 0)
            port.admission.result.error = DownloadSubmissionError::None;
        if (scenario == 1)
            port.admission.result.job_ids = {"preview-1"};
        if (scenario == 2)
            port.admission.result.error = DownloadSubmissionError::Busy;
        if (scenario == 3)
            port.admission.state = static_cast<DownloadAdmissionState>(42);
        if (scenario == 4)
            port.throw_begin = true;
        NativeDownloadSession session(origin, origin, port);
        const auto reply = session.receive(framePayload(requestPayload()), 0);
        QCOMPARE(reply.status, NativeSessionStatus::Closed);
        QVERIFY(reply.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(session.poll(1).status, NativeSessionStatus::Closed);
        QCOMPARE(session.receive(framePayload(requestPayload()), 2).status, NativeSessionStatus::Closed);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
    }

    void asyncMalformedCompletionOrExceptionClosesWithoutAck_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 8; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }

    void asyncMalformedCompletionOrExceptionClosesWithoutAck() {
        QFETCH(int, scenario);
        AsyncSubmission port;
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        if (scenario == 0)
            port.completion->job_ids.clear();
        if (scenario == 1)
            port.completion->job_ids = {"one", "two"};
        if (scenario == 2)
            port.completion->job_ids = {"../file.mp3"};
        if (scenario == 3)
            port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::Busy, .job_ids = {"one"}};
        if (scenario == 4)
            port.completion->error = static_cast<DownloadSubmissionError>(42);
        if (scenario == 5)
            port.completion->job_ids = {std::string(81, 'a')};
        if (scenario == 6)
            port.throw_poll = true;
        if (scenario == 7)
            port.completion->job_ids = {""};
        NativeDownloadSession session(origin, origin, port);
        QCOMPARE(session.receive(framePayload(requestPayload()), 0).status, NativeSessionStatus::Awaiting);
        const auto reply = session.poll(1);
        QCOMPARE(reply.status, NativeSessionStatus::Closed);
        QVERIFY(reply.response_frame.isEmpty());
        QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
        QCOMPARE(session.receive(framePayload(requestPayload()), 3).status, NativeSessionStatus::Closed);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 1);
    }

    void asyncReceiptRequiresDistinctIdsForEveryExpectedItem() {
        auto two_items = QJsonDocument::fromJson(requestPayload()).object();
        auto items = two_items.value("items").toArray();
        auto other = items.at(0).toObject();
        other.insert("url", "https://media.example.test/b.mp4");
        other.insert("kind", "video");
        items.append(other);
        two_items.insert("items", items);
        AsyncSubmission port;
        port.completion =
            DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"duplicate", "duplicate"}};
        NativeDownloadSession session(origin, origin, port);
        QCOMPARE(session.receive(framePayload(QJsonDocument{two_items}.toJson(QJsonDocument::Compact)), 0).status,
                 NativeSessionStatus::Awaiting);
        const auto reply = session.poll(1);
        QCOMPARE(reply.status, NativeSessionStatus::Closed);
        QVERIFY(reply.response_frame.isEmpty());
        QCOMPARE(port.owned.items.size(), std::size_t{2});
    }

    void asyncWaitDeadlineStartsAfterCompleteAndNeverRenews() {
        AsyncSubmission port;
        NativeDownloadSession session(origin, origin, port, 100);
        const auto bytes = framePayload(requestPayload());
        QCOMPARE(session.receive(QByteArrayView(bytes).first(3), 101).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.receive(QByteArrayView(bytes).sliced(3), 5099).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.poll(5100).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.poll(9000).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.poll(10098).status, NativeSessionStatus::Awaiting);
        const auto expired = session.poll(10099);
        QCOMPARE(expired.status, NativeSessionStatus::Expired);
        QVERIFY(expired.response_frame.isEmpty());
        QCOMPARE(port.polls, 3);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"late-1"}};
        QCOMPARE(session.poll(10100).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, 3);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.owned.request_id, request_id);
    }

    void asyncWaitRejectsBackClockAndUsesOverflowSafeElapsed() {
        AsyncSubmission backwards;
        NativeDownloadSession regression(origin, origin, backwards, 100);
        QCOMPARE(regression.receive(framePayload(requestPayload()), 101).status, NativeSessionStatus::Awaiting);
        QCOMPARE(regression.poll(200).status, NativeSessionStatus::Awaiting);
        QCOMPARE(regression.poll(199).status, NativeSessionStatus::Closed);
        QCOMPARE(backwards.polls, 1);
        AsyncSubmission port;
        constexpr auto start = std::numeric_limits<qint64>::min();
        NativeDownloadSession crossing(origin, origin, port, start);
        QCOMPARE(crossing.receive(framePayload(requestPayload()), start + 1).status, NativeSessionStatus::Awaiting);
        QCOMPARE(crossing.poll(std::numeric_limits<qint64>::max()).status, NativeSessionStatus::Expired);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
    }

    void asyncEofOrExtraInputClosesOnlyTheObserver_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("eof") << 0;
        QTest::newRow("one-extra-byte") << 1;
        QTest::newRow("second-frame") << 2;
    }

    void asyncEofOrExtraInputClosesOnlyTheObserver() {
        QFETCH(int, scenario);
        AsyncSubmission port;
        NativeDownloadSession session(origin, origin, port);
        QCOMPARE(session.receive(framePayload(requestPayload()), 0).status, NativeSessionStatus::Awaiting);
        const auto closed = scenario == 0
                                ? session.finishInput(1)
                                : session.receive(scenario == 1 ? QByteArray{"x"} : framePayload(requestPayload()), 1);
        QCOMPARE(closed.status, NativeSessionStatus::Closed);
        QVERIFY(closed.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.owned.request_id, request_id);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.begins, 1);
    }

    void asyncEofNeverConsumesACompletionOrFabricatesAck() {
        AsyncSubmission port;
        NativeDownloadSession session(origin, origin, port);
        QCOMPARE(session.receive(framePayload(requestPayload()), 0).status, NativeSessionStatus::Awaiting);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        const auto closed = session.finishInput(1);
        QCOMPARE(closed.status, NativeSessionStatus::Closed);
        QVERIFY(closed.response_frame.isEmpty());
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.owned.request_id, request_id);
    }

    void asyncReentryCannotBeginOrPollOrReplyTwice() {
        AsyncSubmission port;
        NativeDownloadSession session(origin, origin, port);
        port.during_begin = [&] {
            QCOMPARE(session.receive(framePayload(requestPayload()), 1).status, NativeSessionStatus::Closed);
            QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
            QCOMPARE(session.finishInput(3).status, NativeSessionStatus::Closed);
        };
        QCOMPARE(session.receive(framePayload(requestPayload()), 0).status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
        port.during_poll = [&] {
            const auto nested = session.poll(1000);
            QCOMPARE(nested.status, NativeSessionStatus::Closed);
            QVERIFY(nested.response_frame.isEmpty());
            QCOMPARE(session.receive(framePayload(requestPayload()), 1001).status, NativeSessionStatus::Closed);
            QCOMPARE(session.finishInput(1002).status, NativeSessionStatus::Closed);
        };
        QCOMPARE(session.poll(1).status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.polls, 1);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        const auto reply = session.poll(2);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(reply).value("jobIds").toArray(), QJsonArray{QStringLiteral("saved-1")});
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 2);
    }

    void realControllerSavedCheckpointPrecedesAckAndReplaysAcrossObservers() {
        ControlledAdmission h;
        QVERIFY(h.controller.start());
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Loading);
        QCOMPARE(h.io.loads, 1);
        h.io.completeMissingLoad();
        h.controller.tick(0);
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Ready);
        NativeDownloadSession first(origin, origin, h.controller);
        const auto pending = first.receive(framePayload(requestPayload()), 1);
        QCOMPARE(pending.status, NativeSessionStatus::Awaiting);
        QVERIFY(pending.response_frame.isEmpty());
        QCOMPARE(h.io.saves, 1);
        QCOMPARE(h.execution.starts, 0);
        h.controller.tick(2);
        const auto still_pending = first.poll(2);
        QCOMPARE(still_pending.status, NativeSessionStatus::Awaiting);
        QVERIFY(still_pending.response_frame.isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.io.saves, 1);
        h.io.completeSave(DownloadCheckpointSaveStatus::Saved);
        h.controller.tick(3);
        const auto first_reply = first.poll(3);
        QCOMPARE(first_reply.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(first_reply).value("status").toString(), QStringLiteral("accepted"));
        QCOMPARE(h.io.persisted.receipts.size(), std::size_t{1});
        const auto receipt = responseObject(first_reply).value("jobIds").toArray();
        QCOMPARE(receipt.size(), 1);
        h.controller.tick(4);
        QCOMPARE(h.execution.starts, 1);
        const auto saves_before_replay = h.io.saves;
        NativeDownloadSession second(origin, origin, h.controller);
        const auto replay = second.receive(framePayload(requestPayload()), 5);
        QCOMPARE(replay.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(replay).value("jobIds").toArray(), receipt);
        h.controller.tick(6);
        QCOMPARE(h.io.saves, saves_before_replay);
        QCOMPARE(h.execution.starts, 1);
        QCOMPARE(h.importer.calls, 0);
        QCOMPARE(first.poll(6).status, NativeSessionStatus::Closed);
    }

    void realControllerFailedAfterCommitNeverBecomesAnAcceptedAck() {
        ControlledAdmission h;
        QVERIFY(h.controller.start());
        h.io.completeMissingLoad();
        h.controller.tick(0);
        NativeDownloadSession session(origin, origin, h.controller);
        QCOMPARE(session.receive(framePayload(requestPayload()), 1).status, NativeSessionStatus::Awaiting);
        QCOMPARE(h.io.saves, 1);
        h.io.completeSave(DownloadCheckpointSaveStatus::Failed);
        h.controller.tick(2);
        QVERIFY(h.io.persisted == h.io.requested);
        const auto observed = session.poll(2);
        QCOMPARE(observed.status, NativeSessionStatus::Replied);
        QCOMPARE(responseObject(observed).value("status").toString(), QStringLiteral("rejected"));
        QCOMPARE(responseObject(observed).value("code").toString(), QStringLiteral("not_available"));
        QVERIFY(!observed.response_frame.contains("jobIds"));
        h.controller.tick(3);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.io.loads, 1);
        QCOMPARE(h.io.saves, 1);
        QCOMPARE(h.importer.calls, 0);
    }
};

QTEST_APPLESS_MAIN(NativeDownloadSessionTests)
#include "NativeDownloadSessionTests.moc"
