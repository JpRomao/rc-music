#include "adapters/NativeYoutubeAudioSession.hpp"
#include "adapters/YoutubeAudioCodec.hpp"
#include "app/NativeHostInputStep.hpp"

#include <QTest>

using namespace music_player::m0;
namespace {
class Submission final : public AsyncDownloadSubmission {
  public:
    int begins{0};
    DownloadAdmissionResult begin(const DownloadRequest&) override {
        ++begins;
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override { return std::nullopt; }
};
QByteArray request() {
    return encodeNativeMessageFrame(
        R"({"schemaVersion":1,"type":"download.request","requestId":"11111111-1111-4111-8111-111111111111","items":[{"kind":"audio","url":"https://fixture.invalid/synthetic.mp3","title":"Synthetic"}]})");
}
class YoutubeSubmission final : public AsyncYoutubeAudioSubmission {
  public:
    int begins{}, polls{};
    DownloadAdmissionResult begin(const YoutubeAudioRequest&) override {
        ++begins;
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override {
        ++polls;
        return std::nullopt;
    }
};
QByteArray youtubeRequest() {
    return encodeNativeMessageFrame(encodeYoutubeAudioRequest(
        {"11111111-1111-4111-8111-111111111111", {{"testVideo01", "Synthetic", "Artist", "", false}}}));
}
} // namespace
class NativeHostInputStepTests final : public QObject {
    Q_OBJECT
  private slots:
    void inputReturningAfterAssemblyDeadlineCannotAdmit() {
        Submission submission;
        const auto origin = QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/");
        NativeDownloadSession session{origin, origin, submission};
        qint64 now = 4'900;
        const auto response = readNativeHostInput(
            session,
            [&] {
                now = 5'000; // Exact deadline crossed inside the synchronous read.
                return NativeStdioInput{NativeStdioInputState::Data, request()};
            },
            [&] { return now; });
        QVERIFY(response);
        QCOMPARE(response->status, NativeSessionStatus::Expired);
        QCOMPARE(submission.begins, 0);
    }
    void inputReturningBeforeDeadlineCanAdmit() {
        Submission submission;
        const auto origin = QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/");
        NativeDownloadSession session{origin, origin, submission};
        qint64 now = 1;
        const auto response = readNativeHostInput(
            session,
            [&] {
                now = 4'999;
                return NativeStdioInput{NativeStdioInputState::Data, request()};
            },
            [&] { return now; });
        QVERIFY(response);
        QCOMPARE(response->status, NativeSessionStatus::Awaiting);
        QCOMPARE(submission.begins, 1);
    }
    void youtubeReadCompletingAtAssemblyDeadlineCannotAdmit() {
        YoutubeSubmission submission;
        const auto origin = QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/");
        NativeYoutubeAudioSession session{origin, origin, submission};
        qint64 now = 4'999;
        const auto response = readNativeHostInput(
            session,
            [&] {
                now = 5'000;
                return NativeStdioInput{NativeStdioInputState::Data, youtubeRequest()};
            },
            [&] { return now; });
        QVERIFY(response);
        QCOMPARE(response->status, NativeSessionStatus::Expired);
        QCOMPARE(submission.begins, 0);
    }
    void youtubeReadBeforeDeadlineAdmitsOnceAndEofClosesOnlyTheObserver() {
        YoutubeSubmission submission;
        const auto origin = QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/");
        NativeYoutubeAudioSession session{origin, origin, submission};
        const auto response = readNativeHostInput(
            session, [] { return NativeStdioInput{NativeStdioInputState::Data, youtubeRequest()}; },
            [] { return qint64{4'999}; });
        QVERIFY(response);
        QCOMPARE(response->status, NativeSessionStatus::Awaiting);
        QCOMPARE(submission.begins, 1);
        const auto eof = readNativeHostInput(
            session, [] { return NativeStdioInput{NativeStdioInputState::Closed}; }, [] { return qint64{5'000}; });
        QVERIFY(eof);
        QCOMPARE(eof->status, NativeSessionStatus::Closed);
        QCOMPARE(submission.begins, 1);
        QCOMPARE(submission.polls, 0);
    }
    void youtubeWaitingReadUsesItsCompletionClockForAdmissionTimeout() {
        YoutubeSubmission submission;
        const auto origin = QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/");
        NativeYoutubeAudioSession session{origin, origin, submission};
        QCOMPARE(session.receive(youtubeRequest(), 1).status, NativeSessionStatus::Awaiting);
        qint64 now = 4'999;
        const auto response = readNativeHostInput(
            session,
            [&] {
                now = 5'001;
                return NativeStdioInput{NativeStdioInputState::Waiting};
            },
            [&] { return now; });
        QVERIFY(response);
        QCOMPARE(response->status, NativeSessionStatus::Expired);
        QCOMPARE(submission.begins, 1);
        QCOMPARE(submission.polls, 0);
    }
};
QTEST_GUILESS_MAIN(NativeHostInputStepTests)
#include "NativeHostInputStepTests.moc"
