#include "adapters/NativeYoutubeAudioSession.hpp"
#include "adapters/YoutubeAudioCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QtEndian>

#include <functional>
#include <limits>
#include <stdexcept>

using namespace music_player::m0;
namespace {
const QString origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";
QJsonObject item(const QString& video_id = QStringLiteral("aB0-_Cd1EF2")) {
    return {{"videoId", video_id},
            {"title", QStringLiteral("歌の名前 / Canção")},
            {"artist", QStringLiteral("作者 日本語")},
            {"lyricsPage", "https://www.letras.mus.br/naruto/silhouette/"},
            {"fetchLyrics", true}};
}
QByteArray payload(const QJsonArray& items = {item()}) {
    return QJsonDocument(QJsonObject{{"schemaVersion", 1},
                                     {"type", "youtube.audio.request"},
                                     {"requestId", QString::fromStdString(request_id)},
                                     {"items", items}})
        .toJson(QJsonDocument::Compact);
}
QByteArray prefix(const quint32 size) {
    QByteArray result(4, Qt::Uninitialized);
    qToLittleEndian(size, result.data());
    return result;
}
QByteArray framed(const QByteArray& body = payload()) { return encodeNativeMessageFrame(body); }
QJsonObject response(const NativeSessionResult& result) {
    if (result.response_frame.size() < 4)
        return {};
    return QJsonDocument::fromJson(result.response_frame.sliced(4)).object();
}
class Submission final : public AsyncYoutubeAudioSubmission {
  public:
    int begins{}, polls{};
    bool throw_begin{}, throw_poll{};
    std::function<void()> during_begin{}, during_poll{};
    YoutubeAudioRequest owned;
    std::vector<std::string> observed_ids;
    DownloadAdmissionResult admission{.state = DownloadAdmissionState::Pending};
    std::optional<DownloadSubmissionResult> completion;
    DownloadAdmissionResult begin(const YoutubeAudioRequest& request) override {
        ++begins;
        owned = request;
        if (during_begin)
            during_begin();
        if (throw_begin)
            throw std::runtime_error("private raw admission failure");
        return admission;
    }
    std::optional<DownloadSubmissionResult> poll(const std::string_view id) override {
        ++polls;
        observed_ids.emplace_back(id);
        if (during_poll)
            during_poll();
        if (throw_poll)
            throw std::runtime_error("private raw completion failure");
        return completion;
    }
};
} // namespace
class NativeYoutubeAudioSessionTests final : public QObject {
    Q_OBJECT
  private slots:
    void splitAuthorizedFrameDispatchesOnceAndPendingHasNoWireAcknowledgement() {
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port, 100);
        QCOMPARE(port.begins, 0);
        auto bytes = framed();
        QCOMPARE(session.receive(QByteArrayView(bytes).first(3), 101).status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.begins, 0);
        const auto pending = session.receive(QByteArrayView(bytes).sliced(3), 200);
        QCOMPARE(pending.status, NativeSessionStatus::Awaiting);
        QVERIFY(pending.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.owned.request_id, request_id);
        QCOMPARE(port.owned.items.size(), std::size_t{1});
        QCOMPARE(port.owned.items.front().video_id, std::string{"aB0-_Cd1EF2"});
        QCOMPARE(port.owned.items.front().title, QStringLiteral("歌の名前 / Canção").toStdString());
        QVERIFY(port.owned.items.front().fetch_lyrics);
        bytes.fill('X');
        QCOMPARE(port.owned.items.front().lyrics_page, std::string{"https://www.letras.mus.br/naruto/silhouette/"});
        const auto still_pending = session.poll(201);
        QCOMPARE(still_pending.status, NativeSessionStatus::Awaiting);
        QVERIFY(still_pending.response_frame.isEmpty());
        QCOMPARE(port.observed_ids, std::vector<std::string>{request_id});
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        const auto result = session.poll(202);
        QCOMPARE(result.status, NativeSessionStatus::Replied);
        QVERIFY(result.response_frame.size() > 4);
        QCOMPARE(qFromLittleEndian<quint32>(result.response_frame.constData()),
                 static_cast<quint32>(result.response_frame.size() - 4));
        QCOMPARE(response(result).value("type").toString(), QStringLiteral("youtube.audio.response"));
        QCOMPARE(response(result).value("requestId").toString().toStdString(), request_id);
        QCOMPARE(response(result).value("jobIds").toArray(), QJsonArray{"saved-1"});
        QVERIFY(!result.response_frame.contains("letras.mus.br"));
        QVERIFY(decodeYoutubeAudioResponse(result.response_frame.sliced(4), request_id, 1).has_value());
        QCOMPARE(session.poll(203).status, NativeSessionStatus::Closed);
        QVERIFY(session.receive(framed(), 204).response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 2);
    }
    void popupWireWithExplicitWatchUrlKeepsTheFullTypedRequest() {
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port);
        auto value = item();
        value.insert("videoUrl", "https://www.youtube.com/watch?v=aB0-_Cd1EF2");
        value.insert("fileName", QStringLiteral("Preferida 日本"));
        const auto result = session.receive(framed(payload({value})), 0);
        QCOMPARE(result.status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.begins, 1);
        const YoutubeAudioItem expected{.video_id = "aB0-_Cd1EF2",
                                        .title = "歌の名前 / Canção",
                                        .artist = "作者 日本語",
                                        .lyrics_page = "https://www.letras.mus.br/naruto/silhouette/",
                                        .fetch_lyrics = true,
                                        .file_name = "Preferida 日本"};
        QCOMPARE(port.owned.items, std::vector{expected});
    }
    void originMustMatchTheExactTrustedExtensionOrigin_data() {
        QTest::addColumn<QString>("caller");
        QTest::addColumn<QString>("allowed");
        QTest::newRow("other extension") << QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/")
                                         << origin;
        QTest::newRow("web origin") << QStringLiteral("https://www.youtube.com/") << origin;
        QTest::newRow("missing slash") << origin.chopped(1) << origin.chopped(1);
        QTest::newRow("query") << origin + "?trusted=1" << origin + "?trusted=1";
        QTest::newRow("upper scheme") << origin.toUpper() << origin.toUpper();
        QTest::newRow("invalid id alphabet") << QStringLiteral("chrome-extension://qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq/")
                                             << QStringLiteral("chrome-extension://qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq/");
        QTest::newRow("empty") << QString{} << QString{};
    }
    void originMustMatchTheExactTrustedExtensionOrigin() {
        QFETCH(QString, caller);
        QFETCH(QString, allowed);
        Submission port;
        NativeYoutubeAudioSession session(caller, allowed, port);
        const auto result = session.receive(framed(), 0);
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(session.poll(1).status, NativeSessionStatus::Closed);
        QCOMPARE(port.begins, 0);
        QCOMPARE(port.polls, 0);
    }
    void ordinaryOrPartiallyInvalidRequestsNeverReachTheYoutubePort_data() {
        QTest::addColumn<QByteArray>("body");
        auto principal = QJsonDocument::fromJson(payload()).object();
        principal.insert("type", "download.request");
        QTest::newRow("main protocol") << QJsonDocument(principal).toJson(QJsonDocument::Compact);
        QTest::newRow("invalid json") << QByteArray("not JSON");
        QTest::newRow("second invalid") << payload({item(), item("bad")});
        QTest::newRow("duplicate id") << payload({item(), item()});
        auto forged = QJsonDocument::fromJson(payload()).object();
        forged.insert("callerOrigin", origin);
        QTest::newRow("forged origin field") << QJsonDocument(forged).toJson(QJsonDocument::Compact);
        QTest::newRow("empty items") << payload({});
    }
    void ordinaryOrPartiallyInvalidRequestsNeverReachTheYoutubePort() {
        QFETCH(QByteArray, body);
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port);
        const auto result = session.receive(framed(body), 0);
        QVERIFY(result.status == NativeSessionStatus::Closed || result.status == NativeSessionStatus::Replied);
        if (!result.response_frame.isEmpty())
            QCOMPARE(response(result).value("status").toString(), QStringLiteral("rejected"));
        QCOMPARE(port.begins, 0);
        QCOMPARE(port.polls, 0);
    }
    void declaredPayloadIsBoundedByYoutubeBeforeItsBodyArrives_data() {
        QTest::addColumn<quint32>("size");
        QTest::newRow("zero") << quint32{0};
        QTest::newRow("over YouTube limit") << static_cast<quint32>(youtube_audio_max_payload_bytes + 1);
        QTest::newRow("maximum wire integer") << std::numeric_limits<quint32>::max();
    }
    void declaredPayloadIsBoundedByYoutubeBeforeItsBodyArrives() {
        QFETCH(quint32, size);
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port);
        const auto result = session.receive(prefix(size), 0);
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.begins, 0);
    }
    void theInclusivePayloadLimitCanCarryACompleteValidRequest() {
        Submission port;
        port.admission = {.state = DownloadAdmissionState::Completed,
                          .result = {.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}}};
        auto body = payload();
        body.append(static_cast<qsizetype>(youtube_audio_max_payload_bytes) - body.size(), ' ');
        NativeYoutubeAudioSession session(origin, origin, port);
        const auto result = session.receive(framed(body), 0);
        QCOMPARE(result.status, NativeSessionStatus::Replied);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, 0);
    }
    void typedRejectionIsCorrelatedForImmediateAndPendingAdmission_data() {
        QTest::addColumn<int>("error");
        QTest::newRow("invalid") << static_cast<int>(DownloadSubmissionError::InvalidRequest);
        QTest::newRow("unsupported") << static_cast<int>(DownloadSubmissionError::UnsupportedSource);
        QTest::newRow("busy") << static_cast<int>(DownloadSubmissionError::Busy);
        QTest::newRow("unavailable") << static_cast<int>(DownloadSubmissionError::NotAvailable);
        QTest::newRow("limit") << static_cast<int>(DownloadSubmissionError::LimitExceeded);
    }
    void typedRejectionIsCorrelatedForImmediateAndPendingAdmission() {
        QFETCH(int, error);
        const DownloadSubmissionResult rejection{.error = static_cast<DownloadSubmissionError>(error)};
        for (const bool immediate : {false, true}) {
            Submission port;
            if (immediate)
                port.admission = {.state = DownloadAdmissionState::Completed, .result = rejection};
            NativeYoutubeAudioSession session(origin, origin, port);
            auto result = session.receive(framed(), 0);
            if (!immediate) {
                QCOMPARE(result.status, NativeSessionStatus::Awaiting);
                port.completion = rejection;
                result = session.poll(1);
            }
            QCOMPARE(result.status, NativeSessionStatus::Replied);
            QVERIFY(result.response_frame.size() > 4);
            const auto decoded = decodeYoutubeAudioResponse(result.response_frame.sliced(4), request_id, 1);
            QVERIFY(decoded);
            QCOMPARE(decoded->error, rejection.error);
            QVERIFY(decoded->job_ids.empty());
            QCOMPARE(port.begins, 1);
        }
    }
    void contradictoryPendingNeverBecomesAnEarlyOrLaterAcknowledgement_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 4; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void contradictoryPendingNeverBecomesAnEarlyOrLaterAcknowledgement() {
        QFETCH(int, scenario);
        Submission port;
        if (scenario == 0)
            port.admission.result.error = DownloadSubmissionError::None;
        else if (scenario == 1)
            port.admission.result.error = DownloadSubmissionError::Busy;
        else if (scenario == 2)
            port.admission.result.job_ids = {"preview-1"};
        else
            port.admission.state = static_cast<DownloadAdmissionState>(99);
        NativeYoutubeAudioSession session(origin, origin, port);
        const auto result = session.receive(framed(), 0);
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(session.poll(1).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, 0);
    }
    void malformedOrPartialCompletionCannotAcknowledgeTheBatch_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 6; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void malformedOrPartialCompletionCannotAcknowledgeTheBatch() {
        QFETCH(int, scenario);
        DownloadSubmissionResult completion{.error = DownloadSubmissionError::None, .job_ids = {"one", "two"}};
        if (scenario == 0)
            completion.job_ids.clear();
        else if (scenario == 1)
            completion.job_ids = {"one"};
        else if (scenario == 2)
            completion.job_ids = {"same", "same"};
        else if (scenario == 3)
            completion.job_ids = {"valid", "https://private.invalid/"};
        else if (scenario == 4)
            completion.error = DownloadSubmissionError::Busy;
        else
            completion = {.error = static_cast<DownloadSubmissionError>(99)};
        for (const bool immediate : {false, true}) {
            Submission port;
            if (immediate)
                port.admission = {.state = DownloadAdmissionState::Completed, .result = completion};
            NativeYoutubeAudioSession session(origin, origin, port);
            auto result = session.receive(framed(payload({item(), item("bB0-_Cd1EF2")})), 0);
            if (!immediate) {
                QCOMPARE(result.status, NativeSessionStatus::Awaiting);
                port.completion = completion;
                result = session.poll(1);
            }
            QCOMPARE(result.status, NativeSessionStatus::Closed);
            QVERIFY(result.response_frame.isEmpty());
            QCOMPARE(port.begins, 1);
            QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
        }
    }
    void theWaitStartsAfterAssemblyAndPollingCannotRenewItsFiveSecondDeadline() {
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port, 100);
        const auto bytes = framed();
        QCOMPARE(session.receive(QByteArrayView(bytes).first(2), 101).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.receive(QByteArrayView(bytes).sliced(2), 4000).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.poll(4001).status, NativeSessionStatus::Awaiting);
        QCOMPARE(session.poll(8999).status, NativeSessionStatus::Awaiting);
        QCOMPARE(port.polls, 2);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        const auto result = session.poll(9000);
        QCOMPARE(result.status, NativeSessionStatus::Expired);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.polls, 2);
        QCOMPARE(port.begins, 1);
    }
    void backwardClocksAndOverflowSafeElapsedNeverConsumeAnInvalidCompletion() {
        Submission port;
        NativeYoutubeAudioSession backwards(origin, origin, port, 100);
        QCOMPARE(backwards.receive(framed(), 100).status, NativeSessionStatus::Awaiting);
        QCOMPARE(backwards.poll(99).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, 0);
        Submission second;
        const auto first = std::numeric_limits<qint64>::min();
        NativeYoutubeAudioSession extreme(origin, origin, second, first);
        QCOMPARE(extreme.receive(framed(), first + 1).status, NativeSessionStatus::Awaiting);
        QCOMPARE(extreme.poll(std::numeric_limits<qint64>::max()).status, NativeSessionStatus::Expired);
        QCOMPARE(second.polls, 0);
    }
    void incompleteFrameTimeoutOrEofNeverDispatches() {
        Submission port;
        NativeYoutubeAudioSession expired(origin, origin, port);
        QCOMPARE(expired.receive(prefix(10), 4999).status, NativeSessionStatus::Awaiting);
        QCOMPARE(expired.poll(5000).status, NativeSessionStatus::Expired);
        NativeYoutubeAudioSession eof(origin, origin, port);
        QCOMPARE(eof.receive(QByteArray("\x01", 1), 0).status, NativeSessionStatus::Awaiting);
        const auto result = eof.finishInput(1);
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.begins, 0);
    }
    void eofExtraBytesOrAnotherFrameOnlyCloseTheObserver_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("eof") << 0;
        QTest::newRow("extra bytes") << 1;
        QTest::newRow("another frame") << 2;
    }
    void eofExtraBytesOrAnotherFrameOnlyCloseTheObserver() {
        QFETCH(int, scenario);
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port);
        QCOMPARE(session.receive(framed(), 0).status, NativeSessionStatus::Awaiting);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        const auto result =
            scenario == 0 ? session.finishInput(1) : session.receive(scenario == 1 ? QByteArray("x") : framed(), 1);
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.owned.request_id, request_id);
        QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, 0);
        QCOMPARE(port.begins, 1);
    }
    void exceptionsLeaveAdmissionOwnershipUntouchedAndNeverLeakRawFailure_data() {
        QTest::addColumn<bool>("during_begin");
        QTest::newRow("begin") << true;
        QTest::newRow("poll") << false;
    }
    void exceptionsLeaveAdmissionOwnershipUntouchedAndNeverLeakRawFailure() {
        QFETCH(bool, during_begin);
        Submission port;
        port.throw_begin = during_begin;
        port.throw_poll = !during_begin;
        NativeYoutubeAudioSession session(origin, origin, port);
        auto result = session.receive(framed(), 0);
        if (!during_begin) {
            QCOMPARE(result.status, NativeSessionStatus::Awaiting);
            result = session.poll(1);
        }
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, during_begin ? 0 : 1);
        QCOMPARE(port.owned.request_id, request_id);
    }
    void reentrantInputPollAndEofIrreversiblyCloseOnlyTheObserver_data() {
        QTest::addColumn<bool>("during_begin");
        QTest::newRow("begin") << true;
        QTest::newRow("poll") << false;
    }
    void reentrantInputPollAndEofIrreversiblyCloseOnlyTheObserver() {
        QFETCH(bool, during_begin);
        Submission port;
        NativeYoutubeAudioSession session(origin, origin, port);
        const auto reentry = [&] {
            QCOMPARE(session.receive(framed(), 1).status, NativeSessionStatus::Closed);
            QCOMPARE(session.poll(2).status, NativeSessionStatus::Closed);
            QCOMPARE(session.finishInput(3).status, NativeSessionStatus::Closed);
        };
        if (during_begin)
            port.during_begin = reentry;
        else
            port.during_poll = reentry;
        auto result = session.receive(framed(), 0);
        if (!during_begin) {
            QCOMPARE(result.status, NativeSessionStatus::Awaiting);
            result = session.poll(1);
        }
        QCOMPARE(result.status, NativeSessionStatus::Closed);
        QVERIFY(result.response_frame.isEmpty());
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, during_begin ? 0 : 1);
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"saved-1"}};
        QCOMPARE(session.poll(4).status, NativeSessionStatus::Closed);
        QCOMPARE(port.polls, during_begin ? 0 : 1);
    }
};
QTEST_GUILESS_MAIN(NativeYoutubeAudioSessionTests)
#include "NativeYoutubeAudioSessionTests.moc"
