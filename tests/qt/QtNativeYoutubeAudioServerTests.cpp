#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/NativeYoutubeAudioIpcProtocol.hpp"
#include "adapters/QtNativeYoutubeAudioServer.hpp"
#include "adapters/WindowsNativeDownloadLease.hpp"
#include "adapters/YoutubeAudioCodec.hpp"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QTest>
#include <QUuid>
#include <QtEndian>

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
const QString origin{QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/")};
const std::string request_id{"01234567-89ab-4cde-8fab-0123456789ab"};
YoutubeAudioRequest request() {
    return {request_id,
            {{"aB0-_Cd1EF2", "歌 / Canção", "Artist", "https://www.letras.mus.br/naruto/silhouette/", true}}};
}
NativeHostConfiguration configuration() {
    const auto image = QCoreApplication::applicationFilePath();
    return {origin,
            QStringLiteral("rc-music-youtube-server-") + QUuid::createUuid().toString(QUuid::Id128),
            {image},
            {image}};
}
QByteArray payload() { return encodeYoutubeAudioRequest(request()); }
QByteArray wire(const QByteArray& body = payload(), const QString& caller = origin) {
    return native_youtube_audio_ipc::preamble(caller) + encodeNativeMessageFrame(body);
}
QByteArray prefix(const quint32 length) {
    QByteArray bytes(4, '\0');
    qToLittleEndian(length, bytes.data());
    return bytes;
}
class Submission final : public AsyncYoutubeAudioSubmission {
  public:
    int begins{}, polls{};
    YoutubeAudioRequest owned;
    DownloadAdmissionResult admission{.state = DownloadAdmissionState::Pending};
    std::optional<DownloadSubmissionResult> completion;
    std::function<void()> during_begin;
    DownloadAdmissionResult begin(const YoutubeAudioRequest& value) override {
        ++begins;
        owned = value;
        if (during_begin)
            during_begin();
        return admission;
    }
    std::optional<DownloadSubmissionResult> poll(const std::string_view id) override {
        ++polls;
        if (id != request_id)
            throw std::runtime_error("unexpected observer identity");
        return completion;
    }
};
struct PeerState final {
    NativeDownloadPeerState decision{NativeDownloadPeerState::Eligible};
    bool released{true}, destroyed_on_stack{};
    int begins{}, shutdowns{}, stopped_calls{}, destructions{}, active_calls{};
    std::function<void()> during_stopped;
};
struct PeerCall final {
    PeerState& state;
    explicit PeerCall(PeerState& value) : state(value) { ++state.active_calls; }
    ~PeerCall() { --state.active_calls; }
};
class Peer final : public NativeDownloadPeerCheck {
    std::shared_ptr<PeerState> state_;

  public:
    explicit Peer(std::shared_ptr<PeerState> state) : state_(std::move(state)) {}
    ~Peer() override {
        state_->destroyed_on_stack = state_->destroyed_on_stack || state_->active_calls != 0;
        ++state_->destructions;
    }
    bool begin(const qintptr pipe, NativeDownloadPeerSide side, const QStringList& images) override {
        const PeerCall call{*state_};
        ++state_->begins;
        return pipe != 0 && pipe != -1 && side == NativeDownloadPeerSide::Client && !images.isEmpty();
    }
    NativeDownloadPeerState poll() override {
        const PeerCall call{*state_};
        return state_->decision;
    }
    void shutdown() noexcept override {
        const PeerCall call{*state_};
        ++state_->shutdowns;
    }
    bool stopped() override {
        const PeerCall call{*state_};
        ++state_->stopped_calls;
        const auto callback = std::exchange(state_->during_stopped, {});
        if (callback)
            callback();
        return state_->released;
    }
};
QtNativeYoutubeAudioServerOptions options(const std::shared_ptr<PeerState>& state) {
    return {.peer_check_factory = [state] { return std::make_unique<Peer>(state); }};
}
void completed(Submission& port) {
    port.admission = {.state = DownloadAdmissionState::Completed,
                      .result = {.error = DownloadSubmissionError::None, .job_ids = {"youtube_job"}}};
}
void connectSocket(QLocalSocket& socket, const NativeHostConfiguration& config) {
    socket.connectToServer(config.endpoint_name);
}
} // namespace

class QtNativeYoutubeAudioServerTests final : public QObject {
    Q_OBJECT
  private slots:
    void emptyConfigurationRemainsDisabledAndNeverCallsTheOwner() {
        Submission port;
        const auto peers = std::make_shared<PeerState>();
        QtNativeYoutubeAudioServer server({}, port, options(peers));
        QVERIFY(!server.start());
        QCOMPARE(port.begins, 0);
        QCOMPARE(peers->begins, 0);
        server.beginShutdown();
        QVERIFY(server.stopped());
    }
    void pendingAdmissionRepliesOnlyAfterItsDurableCompletion() {
        Submission port;
        const auto config = configuration();
        const auto peers = std::make_shared<PeerState>();
        QtNativeYoutubeAudioServer server(config, port, options(peers));
        QCOMPARE(port.begins, 0);
        QCOMPARE(peers->begins, 0);
        QCOMPARE(server.activeConnectionCount(), qsizetype{0});
        QVERIFY(server.start());
        QVERIFY(!server.start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto bytes = wire();
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(port.begins, 1);
        QCOMPARE(port.owned, request());
        QTRY_VERIFY(port.polls > 0);
        QCOMPARE(socket.bytesAvailable(), qint64{0});
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"youtube_job"}};
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        const auto response = socket.readAll();
        QVERIFY(response.size() > 4);
        QCOMPARE(qFromLittleEndian<quint32>(response.constData()), static_cast<quint32>(response.size() - 4));
        const auto result = decodeYoutubeAudioResponse(response.sliced(4), request_id, 1);
        QVERIFY(result);
        QCOMPARE(result->error, DownloadSubmissionError::None);
        QCOMPARE(result->job_ids, std::vector<std::string>{"youtube_job"});
        QCOMPARE(port.begins, 1);
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
    }
    void fragmentedPreamblePrefixAndBodyDispatchOnce() {
        Submission port;
        completed(port);
        const auto config = configuration();
        QtNativeYoutubeAudioServer server(config, port, options(std::make_shared<PeerState>()));
        QVERIFY(server.start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto header = native_youtube_audio_ipc::preamble(origin);
        const auto frame = encodeNativeMessageFrame(payload());
        for (const auto& part : {header.first(5), header.sliced(5), frame.first(2), frame.sliced(2, 2),
                                 frame.sliced(4, frame.size() - 5)}) {
            QCOMPARE(socket.write(part), part.size());
            socket.flush();
            QTest::qWait(2);
            QCOMPARE(port.begins, 0);
        }
        QCOMPARE(socket.write(frame.last(1)), qint64{1});
        QTRY_COMPARE(port.begins, 1);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(decodeYoutubeAudioResponse(socket.readAll().sliced(4), request_id, 1));
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
    }
    void exact64KiBPayloadIsAdmittedWithoutRelaxingTheLimit() {
        Submission port;
        completed(port);
        const auto config = configuration();
        QtNativeYoutubeAudioServer server(config, port, options(std::make_shared<PeerState>()));
        QVERIFY(server.start());
        auto body = payload();
        QVERIFY(body.size() < static_cast<qsizetype>(youtube_audio_max_payload_bytes));
        body.append(QByteArray(static_cast<qsizetype>(youtube_audio_max_payload_bytes) - body.size(), ' '));
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto bytes = wire(body);
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(port.begins, 1);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(decodeYoutubeAudioResponse(socket.readAll().sliced(4), request_id, 1));
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
    }
    void invalidFramingNeverDispatches_data() {
        QTest::addColumn<QByteArray>("bytes");
        const auto preamble = native_youtube_audio_ipc::preamble(origin);
        const auto frame = encodeNativeMessageFrame(payload());
        QTest::newRow("zero prefix") << preamble + prefix(0);
        QTest::newRow("oversized prefix before body") << preamble + prefix(64 * 1024 + 1);
        QTest::newRow("excess byte") << preamble + frame + 'x';
        QTest::newRow("second coalesced frame") << preamble + frame + frame;
    }
    void invalidFramingNeverDispatches() {
        QFETCH(QByteArray, bytes);
        Submission port;
        const auto config = configuration();
        QtNativeYoutubeAudioServer server(config, port, options(std::make_shared<PeerState>()));
        QVERIFY(server.start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(port.begins, 0);
        QVERIFY(socket.readAll().isEmpty());
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
    }
    void mainMagicSchemaAndOtherOriginNeverSelectYoutube_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("main magic") << native_download_ipc::preamble(origin) + encodeNativeMessageFrame(payload());
        QTest::newRow("main schema") << wire(QByteArrayLiteral(
            "{\"schemaVersion\":1,\"type\":\"download.request\",\"requestId\":\"01234567-89ab-4cde-8fab-0123456789ab\","
            "\"items\":[{\"url\":\"https://fixture.example.test/music.mp3\",\"title\":\"Song\",\"kind\":\"audio\"}]}"));
        QTest::newRow("other origin") << wire(payload(),
                                              QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"));
    }
    void mainMagicSchemaAndOtherOriginNeverSelectYoutube() {
        QFETCH(QByteArray, bytes);
        Submission port;
        const auto config = configuration();
        QtNativeYoutubeAudioServer server(config, port, options(std::make_shared<PeerState>()));
        QVERIFY(server.start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(port.begins, 0);
        QVERIFY(socket.readAll().isEmpty());
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
    }
    void observerDisconnectAndShutdownDoNotCancelOrRepeatAdmission_data() {
        QTest::addColumn<bool>("shutdown");
        QTest::newRow("client disconnect") << false;
        QTest::newRow("server shutdown") << true;
    }
    void observerDisconnectAndShutdownDoNotCancelOrRepeatAdmission() {
        QFETCH(bool, shutdown);
        Submission port;
        const auto config = configuration();
        QtNativeYoutubeAudioServer server(config, port, options(std::make_shared<PeerState>()));
        QVERIFY(server.start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto bytes = wire();
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(port.begins, 1);
        if (shutdown)
            server.beginShutdown();
        else
            socket.abort();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QTRY_COMPARE(server.activeConnectionCount(), qsizetype{0});
        const auto polls = port.polls;
        port.completion = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"youtube_job"}};
        server.poll();
        QCOMPARE(port.begins, 1);
        QCOMPARE(port.polls, polls);
        QCOMPARE(port.owned, request());
        QVERIFY(socket.readAll().isEmpty());
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
        WindowsNativeDownloadLease contender;
        QVERIFY(!contender.acquire(config.endpoint_name));
    }
    void eightPendingPeerSlotsRemainBoundedThroughPhysicalRetirement() {
        Submission port;
        const auto config = configuration();
        const auto peers = std::make_shared<PeerState>();
        peers->decision = NativeDownloadPeerState::Pending;
        peers->released = false;
        QtNativeYoutubeAudioServer server(config, port, options(peers));
        QVERIFY(server.start());
        std::vector<std::unique_ptr<QLocalSocket>> sockets;
        for (int index = 0; index < 8; ++index) {
            auto socket = std::make_unique<QLocalSocket>();
            connectSocket(*socket, config);
            QTRY_COMPARE(socket->state(), QLocalSocket::ConnectedState);
            const auto bytes = wire().first(1);
            QCOMPARE(socket->write(bytes), bytes.size());
            sockets.push_back(std::move(socket));
        }
        QTRY_COMPARE(peers->begins, 8);
        QCOMPARE(server.activeConnectionCount(), qsizetype{8});
        QCOMPARE(port.begins, 0); // No reader/session before peer eligibility.
        QLocalSocket ninth;
        connectSocket(ninth, config);
        QTRY_COMPARE(ninth.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(peers->begins, 8);
        // Start reads only after eligibility, with one incomplete preamble byte
        // per peer. The transport can then witness disconnects without waiting
        // for the pending-peer deadline or admitting a request.
        peers->decision = NativeDownloadPeerState::Eligible;
        server.poll();
        for (auto& socket : sockets)
            socket->abort();
        QTRY_COMPARE(server.activeConnectionCount(), qsizetype{0});
        QCOMPARE(peers->destructions, 0);
        QLocalSocket before_ack;
        connectSocket(before_ack, config);
        QTRY_COMPARE(before_ack.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(peers->begins, 8);
        peers->released = true;
        QTRY_COMPARE(peers->destructions, 8);
        peers->decision = NativeDownloadPeerState::Eligible;
        completed(port);
        QLocalSocket after_ack;
        connectSocket(after_ack, config);
        QTRY_COMPARE(after_ack.state(), QLocalSocket::ConnectedState);
        const auto bytes = wire();
        QCOMPARE(after_ack.write(bytes), bytes.size());
        QTRY_COMPARE(port.begins, 1);
        QTRY_COMPARE(after_ack.state(), QLocalSocket::UnconnectedState);
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
        QVERIFY(!peers->destroyed_on_stack);
    }
    void reentrantShutdownDuringAdmissionCannotReplyOrProveDrainOnStack() {
        Submission port;
        const auto config = configuration();
        QtNativeYoutubeAudioServer server(config, port, options(std::make_shared<PeerState>()));
        bool drained_inside = true;
        port.during_begin = [&] {
            server.poll();
            server.beginShutdown();
            drained_inside = server.stopped();
        };
        QVERIFY(server.start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        const auto bytes = wire();
        QCOMPARE(socket.write(bytes), bytes.size());
        QTRY_COMPARE(port.begins, 1);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QVERIFY(!drained_inside);
        QCOMPARE(port.polls, 0);
        QVERIFY(socket.readAll().isEmpty());
        QTRY_VERIFY(server.stopped());
    }
    void retirementObservationCannotReenterOrDestroyTheActiveChecker() {
        Submission port;
        const auto config = configuration();
        const auto peers = std::make_shared<PeerState>();
        peers->decision = NativeDownloadPeerState::Pending;
        peers->released = false;
        auto server = std::make_unique<QtNativeYoutubeAudioServer>(config, port, options(peers));
        QVERIFY(server->start());
        QLocalSocket socket;
        connectSocket(socket, config);
        QTRY_COMPARE(peers->begins, 1);
        server->beginShutdown();
        bool nested_stopped = true;
        int nested_calls = -1;
        peers->during_stopped = [&] {
            const auto before = peers->stopped_calls;
            server->beginShutdown();
            nested_stopped = server->stopped();
            nested_calls = peers->stopped_calls - before;
        };
        QVERIFY(!server->stopped());
        QVERIFY(!nested_stopped);
        QCOMPARE(nested_calls, 0);
        QVERIFY(!peers->destroyed_on_stack);
        peers->released = true;
        QTRY_VERIFY(server->stopped());
        server.reset();
        QCOMPARE(peers->destructions, 1);
        QVERIFY(!peers->destroyed_on_stack);
    }
};
QTEST_GUILESS_MAIN(QtNativeYoutubeAudioServerTests)
#include "QtNativeYoutubeAudioServerTests.moc"
