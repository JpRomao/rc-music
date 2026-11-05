#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/NativeYoutubeAudioIpcProtocol.hpp"
#include "adapters/NativeYoutubeAudioSession.hpp"
#include "adapters/QtNativeYoutubeAudioClient.hpp"
#include "adapters/YoutubeAudioCodec.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QtEndian>
#include <qt_windows.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
const QString origin{QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/")};
const std::string request_id{"01234567-89ab-4cde-8fab-0123456789ab"};
YoutubeAudioRequest request() {
    return {request_id,
            {{"testVideo01", "曲 😀", "Synthetic artist", "https://www.letras.mus.br/naruto/silhouette/", true}}};
}
NativeHostConfiguration configuration() {
    const auto image = QCoreApplication::applicationFilePath();
    return {origin,
            QStringLiteral("rc-music-youtube-client-") + QUuid::createUuid().toString(QUuid::Id128),
            {image},
            {image}};
}
QByteArray expectedPreamble() {
    QByteArray result{QByteArrayLiteral("RCMYTIPC")};
    result.append(char{1});
    result.append(origin.toUtf8());
    return result;
}
QByteArray accepted(const std::string& id = request_id, const std::vector<std::string>& jobs = {"youtube-job-1"}) {
    return encodeNativeMessageFrame(
        encodeYoutubeAudioResponse(id, {.error = DownloadSubmissionError::None, .job_ids = jobs}));
}
struct PeerState final {
    NativeDownloadPeerState decision{NativeDownloadPeerState::Eligible};
    bool released{true}, throw_stopped{}, inside_call{}, destroyed_inside_call{}, hook_threw{};
    int begins{}, polls{}, shutdowns{}, stopped_calls{};
    NativeDownloadPeerSide side{NativeDownloadPeerSide::Client};
    QStringList images{};
    std::function<void()> during_shutdown{}, during_stopped{};
};
class Peer final : public NativeDownloadPeerCheck {
  public:
    explicit Peer(std::shared_ptr<PeerState> state) : state_(std::move(state)) {}
    ~Peer() override { state_->destroyed_inside_call = state_->destroyed_inside_call || state_->inside_call; }
    bool begin(qintptr pipe, NativeDownloadPeerSide side, const QStringList& images) override {
        ++state_->begins;
        state_->side = side;
        state_->images = images;
        return GetFileType(reinterpret_cast<HANDLE>(pipe)) == FILE_TYPE_PIPE;
    }
    NativeDownloadPeerState poll() override {
        ++state_->polls;
        return state_->decision;
    }
    void shutdown() noexcept override {
        state_->inside_call = true;
        ++state_->shutdowns;
        try {
            const auto hook = std::exchange(state_->during_shutdown, {});
            if (hook)
                hook();
        } catch (...) {
            state_->hook_threw = true;
        }
        state_->inside_call = false;
    }
    bool stopped() override {
        state_->inside_call = true;
        ++state_->stopped_calls;
        try {
            const auto hook = std::exchange(state_->during_stopped, {});
            if (hook)
                hook();
            if (state_->throw_stopped)
                throw std::runtime_error{"private peer-retirement diagnostic"};
            state_->inside_call = false;
            return state_->released;
        } catch (...) {
            state_->inside_call = false;
            throw;
        }
    }

  private:
    std::shared_ptr<PeerState> state_;
};
struct Harness final {
    NativeHostConfiguration config{configuration()};
    QLocalServer server{};
    std::shared_ptr<PeerState> peer{std::make_shared<PeerState>()};
    qint64 now{};
    std::unique_ptr<QLocalSocket> socket{};
    QtNativeYoutubeAudioClientOptions options() {
        return {.now_ms = [this] { return now; },
                .peer_check_factory = [state = peer] { return std::make_unique<Peer>(state); }};
    }
    bool takeConnection() {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!server.hasPendingConnections() && elapsed.elapsed() < 3'000) {
            QCoreApplication::processEvents();
            QTest::qWait(1);
        }
        socket.reset(server.nextPendingConnection());
        return static_cast<bool>(socket);
    }
    bool requestArrived(const YoutubeAudioRequest& source = request()) {
        const auto expected = expectedPreamble() + encodeNativeMessageFrame(encodeYoutubeAudioRequest(source));
        QElapsedTimer elapsed;
        elapsed.start();
        while (socket->bytesAvailable() < expected.size() && elapsed.elapsed() < 3'000) {
            QCoreApplication::processEvents();
            QTest::qWait(1);
        }
        return socket->readAll() == expected;
    }
};
bool uncertain(QtNativeYoutubeAudioClient& client) {
    try {
        static_cast<void>(client.poll(request_id));
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}
class PendingAdmission final : public AsyncYoutubeAudioSubmission {
  public:
    int begins{};
    bool saved{};
    YoutubeAudioRequest received{};
    DownloadAdmissionResult begin(const YoutubeAudioRequest& value) override {
        ++begins;
        received = value;
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view id) override {
        if (!saved || id != request_id)
            return {};
        return DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"youtube-job-1"}};
    }
};
} // namespace

class QtNativeYoutubeAudioClientTests final : public QObject {
    Q_OBJECT
  private slots:
    void dedicatedPreambleHasItsOwnFixedMagicAndExactOriginBytes() {
        QCOMPARE(native_youtube_audio_ipc::preamble(origin), expectedPreamble());
        QCOMPARE(native_youtube_audio_ipc::preamble_bytes, qsizetype{61});
        QCOMPARE(native_youtube_audio_ipc::response_max_payload_bytes, qsizetype{4096});
        QVERIFY(native_youtube_audio_ipc::preamble(origin) != native_download_ipc::preamble(origin));
        QVERIFY(native_youtube_audio_ipc::preamble(QString{}).isEmpty());
        QVERIFY(native_youtube_audio_ipc::preamble(origin + '/').isEmpty());
    }
    void disabledOrInvalidConfigurationNeverConnects_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("disabled") << 0;
        QTest::newRow("foreign origin") << 1;
        QTest::newRow("endpoint path") << 2;
        QTest::newRow("missing owner images") << 3;
    }
    void disabledOrInvalidConfigurationNeverConnects() {
        QFETCH(int, mode);
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        auto config = h.config;
        if (mode == 0)
            config = {};
        else if (mode == 1)
            config.allowed_origin = QStringLiteral("https://example.invalid/");
        else if (mode == 2)
            config.endpoint_name += QStringLiteral("/other");
        else
            config.expected_owner_images.clear();
        QtNativeYoutubeAudioClient client{config, h.options()};
        const auto result = client.begin(request());
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(result.result.job_ids.empty());
        QCoreApplication::processEvents();
        QVERIFY(!h.server.hasPendingConnections());
        QCOMPARE(h.peer->begins, 0);
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void invalidTypedRequestIsRejectedLocallyWithoutAnIpcConnection_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("invalid UUID") << 0;
        QTest::newRow("URL instead of video ID") << 1;
        QTest::newRow("empty title") << 2;
        QTest::newRow("foreign lyrics page") << 3;
        QTest::newRow("too many items") << 4;
        QTest::newRow("oversized metadata") << 5;
    }
    void invalidTypedRequestIsRejectedLocallyWithoutAnIpcConnection() {
        QFETCH(int, mode);
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        auto input = request();
        if (mode == 0)
            input.request_id = "not-a-UUID";
        else if (mode == 1)
            input.items[0].video_id = "https://youtube.com/watch?v=testVideo01";
        else if (mode == 2)
            input.items[0].title.clear();
        else if (mode == 3)
            input.items[0].lyrics_page = "https://example.invalid/lyrics";
        else if (mode == 4)
            input.items.resize(21, input.items.front());
        else
            input.items[0].title.assign(65'537, 'x');
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        const auto result = client.begin(input);
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(result.result.job_ids.empty());
        QCoreApplication::processEvents();
        QVERIFY(!h.server.hasPendingConnections());
        QCOMPARE(h.peer->begins, 0);
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void pendingPeerPreventsAllRequestBytesUntilEligibility() {
        Harness h;
        h.peer->decision = NativeDownloadPeerState::Pending;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QVERIFY(h.takeConnection());
        QTRY_VERIFY(h.peer->polls > 1);
        QCOMPARE(h.socket->bytesAvailable(), qint64{0});
        QVERIFY(!client.poll(request_id));
        h.peer->decision = NativeDownloadPeerState::Eligible;
        QVERIFY(h.requestArrived());
        QCOMPARE(h.peer->side, NativeDownloadPeerSide::Server);
        QCOMPARE(h.peer->images, h.config.expected_owner_images);
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void typedMetadataIsCapturedAndPendingHasNoAckBeforeSavedAndPeerEof() {
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        auto input = request();
        const auto original = input;
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        const auto begun = client.begin(input);
        QCOMPARE(begun.state, DownloadAdmissionState::Pending);
        QCOMPARE(begun.result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(begun.result.job_ids.empty());
        input.items[0].title = "changed after begin";
        input.items[0].artist.clear();
        input.items[0].fetch_lyrics = false;
        QVERIFY(h.takeConnection());
        QVERIFY(h.requestArrived(original));
        PendingAdmission owner;
        NativeYoutubeAudioSession observer{origin, origin, owner};
        const auto frame = encodeNativeMessageFrame(encodeYoutubeAudioRequest(original));
        QCOMPARE(observer.receive(frame, 0).status, NativeSessionStatus::Awaiting);
        QCOMPARE(owner.begins, 1);
        QVERIFY(owner.received == original);
        QVERIFY(!client.poll(request_id));
        QCOMPARE(observer.poll(1).status, NativeSessionStatus::Awaiting);
        owner.saved = true;
        const auto reply = observer.poll(2);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        QCOMPARE(h.socket->write(reply.response_frame), reply.response_frame.size());
        QTest::qWait(20);
        QVERIFY(!client.poll(request_id));
        h.socket->disconnectFromServer();
        std::optional<DownloadSubmissionResult> result;
        QTRY_VERIFY((result = client.poll(request_id)).has_value());
        QCOMPARE(result->error, DownloadSubmissionError::None);
        QCOMPARE(result->job_ids, std::vector<std::string>{"youtube-job-1"});
        QVERIFY(!client.poll("11111111-1111-4111-8111-111111111111"));
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void malformedCrossFamilyAndUncorrelatedRepliesRemainUncertain_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("main family") << 0;
        QTest::newRow("wrong UUID") << 1;
        QTest::newRow("wrong item count") << 2;
        QTest::newRow("pipelined replies") << 3;
        QTest::newRow("trailing byte") << 4;
        QTest::newRow("invalid JSON") << 5;
        QTest::newRow("extra field") << 6;
        QTest::newRow("partial body at EOF") << 7;
        QTest::newRow("invalid UTF8") << 8;
    }
    void malformedCrossFamilyAndUncorrelatedRepliesRemainUncertain() {
        QFETCH(int, mode);
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QSignalSpy arrivals{&h.server, &QLocalServer::newConnection};
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QVERIFY(h.takeConnection());
        QVERIFY(h.requestArrived());
        auto frame = accepted();
        if (mode == 0)
            frame = encodeNativeMessageFrame(encodeNativeDownloadResponse(
                request_id, {.error = DownloadSubmissionError::None, .job_ids = {"youtube-job-1"}}));
        else if (mode == 1)
            frame = accepted("11111111-1111-4111-8111-111111111111");
        else if (mode == 2)
            frame = accepted(request_id, {"youtube-job-1", "youtube-job-2"});
        else if (mode == 3)
            frame += frame;
        else if (mode == 4)
            frame += 'x';
        else if (mode == 5)
            frame = encodeNativeMessageFrame("not JSON");
        else if (mode == 6) {
            auto object = QJsonDocument::fromJson(frame.mid(4)).object();
            object.insert(QStringLiteral("extra"), true);
            frame = encodeNativeMessageFrame(QJsonDocument{object}.toJson(QJsonDocument::Compact));
        } else if (mode == 7)
            frame.chop(1);
        else
            frame = encodeNativeMessageFrame(QByteArray{"\xff", 1});
        QCOMPARE(h.socket->write(frame), frame.size());
        h.socket->disconnectFromServer();
        QTRY_VERIFY(uncertain(client));
        QVERIFY(uncertain(client));
        const auto retry = client.begin(request());
        QCOMPARE(retry.state, DownloadAdmissionState::Completed);
        QCOMPARE(retry.result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(retry.result.job_ids.empty());
        QCoreApplication::processEvents();
        QCOMPARE(arrivals.size(), 1);
        QVERIFY(!h.server.hasPendingConnections());
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void oversizedResponseLengthFailsBeforeItsBodyOrEof() {
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QVERIFY(h.takeConnection());
        QVERIFY(h.requestArrived());
        std::array<char, 4> prefix{};
        qToLittleEndian<quint32>(4097, prefix.data());
        QCOMPARE(h.socket->write(prefix.data(), static_cast<qint64>(prefix.size())), qint64{4});
        QTRY_VERIFY(uncertain(client));
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void postWriteTimeoutAndClockRegressionDoNotClaimRejection_data() {
        QTest::addColumn<bool>("backward");
        QTest::newRow("inclusive deadline") << false;
        QTest::newRow("regressed clock") << true;
    }
    void postWriteTimeoutAndClockRegressionDoNotClaimRejection() {
        QFETCH(bool, backward);
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QVERIFY(h.takeConnection());
        QVERIFY(h.requestArrived());
        h.now = backward ? 10 : 4'999;
        QVERIFY(!client.poll(request_id));
        h.now = backward ? 9 : 5'000;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(client.poll(request_id)));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(client.poll(request_id)));
        QVERIFY(!h.server.hasPendingConnections());
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void missingOwnerBeforeWriteCompletesUnavailableWithoutAutomaticRetry() {
        Harness h;
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        const auto result = client.begin(request());
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(result.result.job_ids.empty());
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        QCoreApplication::processEvents();
        QVERIFY(!h.server.hasPendingConnections());
        client.shutdown();
        QVERIFY(client.stopped());
    }
    void shutdownWaitsForPeerRetirementAndGuardsReentrantObservation() {
        Harness h;
        h.peer->decision = NativeDownloadPeerState::Pending;
        h.peer->released = false;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QtNativeYoutubeAudioClient client{h.config, h.options()};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QVERIFY(h.takeConnection());
        QTRY_COMPARE(h.peer->begins, 1);
        bool nested_stopped{true};
        int nested_checks{-1};
        h.peer->during_shutdown = [&] {
            client.shutdown();
            const auto previous = h.peer->stopped_calls;
            nested_stopped = client.stopped();
            nested_checks = h.peer->stopped_calls - previous;
        };
        client.shutdown();
        QVERIFY(!nested_stopped);
        QCOMPARE(nested_checks, 0);
        QVERIFY(!client.stopped());
        QVERIFY(!h.peer->hook_threw);
        QVERIFY(!h.peer->destroyed_inside_call);
        h.peer->during_stopped = [&] {
            const auto previous = h.peer->stopped_calls;
            nested_stopped = client.stopped();
            nested_checks = h.peer->stopped_calls - previous;
        };
        QVERIFY(!client.stopped());
        QVERIFY(!nested_stopped);
        QCOMPARE(nested_checks, 0);
        h.peer->throw_stopped = true;
        QVERIFY(!client.stopped());
        h.peer->throw_stopped = false;
        h.peer->released = true;
        QVERIFY(client.stopped());
        QVERIFY(!client.poll(request_id));
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
    }
    void shutdownFromClockCannotConnectOrEscapeItsCallback() {
        Harness h;
        QVERIFY(h.server.listen(h.config.endpoint_name));
        QtNativeYoutubeAudioClient* current{};
        bool stopped_inside_callback{true};
        auto options = h.options();
        options.now_ms = [&] {
            current->shutdown();
            stopped_inside_callback = current->stopped();
            return qint64{0};
        };
        QtNativeYoutubeAudioClient client{h.config, std::move(options)};
        current = &client;
        const auto result = client.begin(request());
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(!stopped_inside_callback);
        QCoreApplication::processEvents();
        QVERIFY(!h.server.hasPendingConnections());
        QCOMPARE(h.peer->begins, 0);
        QVERIFY(client.stopped());
    }
};

QTEST_GUILESS_MAIN(QtNativeYoutubeAudioClientTests)
#include "QtNativeYoutubeAudioClientTests.moc"
