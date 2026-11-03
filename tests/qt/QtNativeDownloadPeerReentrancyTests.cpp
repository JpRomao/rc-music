#include "adapters/QtNativeDownloadClient.hpp"
#include "adapters/QtNativeDownloadServer.hpp"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTest>
#include <QUuid>

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace music_player::m0;

namespace {
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";

NativeHostConfiguration configuration() {
    const auto image = QCoreApplication::applicationFilePath();
    return {QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"),
            QStringLiteral("rc-music-native-reentrancy-") + QUuid::createUuid().toString(QUuid::Id128),
            {image},
            {image}};
}

DownloadRequest request() {
    return {.request_id = request_id,
            .items = {{"https://fixture.example.test/audio.mp3", "Synthetic", DownloadMediaKind::Audio}}};
}

class Submission final : public AsyncDownloadSubmission {
  public:
    int begins{0};
    DownloadAdmissionResult begin(const DownloadRequest&) override {
        ++begins;
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override { return std::nullopt; }
};

struct PeerState final {
    NativeDownloadPeerState decision{NativeDownloadPeerState::Pending};
    bool released{false};
    bool destroyed_inside_call{false};
    bool hook_threw{false};
    int active_calls{0};
    int begins{0};
    int shutdowns{0};
    int stopped_calls{0};
    int destructions{0};
    std::function<void()> during_shutdown;
    std::function<void()> during_stopped;
};

class PeerCall final {
  public:
    explicit PeerCall(PeerState& state) : state_(state) { ++state_.active_calls; }
    ~PeerCall() { --state_.active_calls; }

  private:
    PeerState& state_;
};

void oneShot(std::function<void()>& hook) {
    const auto callback = std::exchange(hook, {});
    if (callback)
        callback();
}

class PeerCheck final : public NativeDownloadPeerCheck {
  public:
    explicit PeerCheck(std::shared_ptr<PeerState> state) : state_(std::move(state)) {}
    ~PeerCheck() override {
        state_->destroyed_inside_call = state_->destroyed_inside_call || state_->active_calls != 0;
        ++state_->destructions;
    }
    bool begin(const qintptr pipe, NativeDownloadPeerSide, const QStringList&) override {
        const PeerCall call{*state_};
        ++state_->begins;
        return pipe != 0 && pipe != -1;
    }
    NativeDownloadPeerState poll() override {
        const PeerCall call{*state_};
        return state_->decision;
    }
    void shutdown() noexcept override {
        const PeerCall call{*state_};
        ++state_->shutdowns;
        try {
            oneShot(state_->during_shutdown);
        } catch (...) {
            state_->hook_threw = true;
        }
    }
    bool stopped() override {
        const PeerCall call{*state_};
        ++state_->stopped_calls;
        oneShot(state_->during_stopped);
        // Keeping this false through each hook makes the Red deterministic:
        // recursive cleanup cannot delete a checker whose method is on stack.
        return state_->released;
    }

  private:
    std::shared_ptr<PeerState> state_;
};

NativeDownloadPeerCheckFactory factory(const std::shared_ptr<PeerState>& state) {
    return [state] { return std::make_unique<PeerCheck>(state); };
}
} // namespace

class QtNativeDownloadPeerReentrancyTests final : public QObject {
    Q_OBJECT

  private slots:
    void unknownClientRetirementDoesNotEscapeOrProveShutdown() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QtNativeDownloadClient client{config, {.peer_check_factory = factory(state)}};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE(state->begins, 1);
        client.shutdown();
        state->during_stopped = [] { throw std::runtime_error{"unknown peer retirement"}; };
        bool threw = false;
        bool stopped = true;
        try {
            stopped = client.stopped();
        } catch (...) {
            threw = true;
        }
        QVERIFY(!threw);
        QVERIFY(!stopped);
        state->released = true;
        QTRY_VERIFY(client.stopped());
    }
    void serverStoppedObservationDoesNotReenterTheChecker() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        Submission owner;
        auto server = std::make_unique<QtNativeDownloadServer>(
            config, owner, QtNativeDownloadServerOptions{.peer_check_factory = factory(state)});
        QVERIFY(server->start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(state->begins, 1);
        server->beginShutdown();

        bool observed_stopped = true;
        int nested_calls = -1;
        state->during_stopped = [&] {
            const auto before = state->stopped_calls;
            observed_stopped = server->stopped();
            nested_calls = state->stopped_calls - before;
        };
        QVERIFY(!server->stopped());
        QVERIFY(!observed_stopped);
        QCOMPARE(nested_calls, 0);
        QVERIFY(!state->destroyed_inside_call);
        state->released = true;
        QTRY_VERIFY(server->stopped());
        server.reset();
        QCOMPARE(state->destructions, 1);
        QVERIFY(!state->destroyed_inside_call);
    }

    void serverPruneObservationDoesNotReenterCleanupThroughShutdown() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        Submission owner;
        auto server = std::make_unique<QtNativeDownloadServer>(
            config, owner, QtNativeDownloadServerOptions{.peer_check_factory = factory(state)});
        QVERIFY(server->start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(state->begins, 1);
        server->beginShutdown();

        bool hook_called = false;
        bool observed_stopped = true;
        int nested_calls = -1;
        state->during_stopped = [&] {
            hook_called = true;
            const auto before = state->stopped_calls;
            server->beginShutdown();
            observed_stopped = server->stopped();
            nested_calls = state->stopped_calls - before;
        };
        // The queued prune and peer timer exercise cleanup, not the public
        // stopped() observation used by the preceding regression.
        QTRY_VERIFY(hook_called);
        QVERIFY(!observed_stopped);
        QCOMPARE(nested_calls, 0);
        QVERIFY(!state->destroyed_inside_call);
        state->released = true;
        QTRY_VERIFY(server->stopped());
        server.reset();
        QCOMPARE(state->destructions, 1);
        QVERIFY(!state->destroyed_inside_call);
    }

    void serverShutdownHookCannotReenterDrainObservation() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        Submission owner;
        auto server = std::make_unique<QtNativeDownloadServer>(
            config, owner, QtNativeDownloadServerOptions{.peer_check_factory = factory(state)});
        QVERIFY(server->start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(state->begins, 1);

        bool observed_stopped = true;
        int nested_calls = -1;
        state->during_shutdown = [&] {
            const auto before = state->stopped_calls;
            server->beginShutdown();
            observed_stopped = server->stopped();
            nested_calls = state->stopped_calls - before;
        };
        server->beginShutdown();
        QVERIFY(!observed_stopped);
        QCOMPARE(nested_calls, 0);
        QVERIFY(!state->hook_threw);
        QVERIFY(!state->destroyed_inside_call);
        state->released = true;
        QTRY_VERIFY(server->stopped());
        server.reset();
        QCOMPARE(state->destructions, 1);
        QVERIFY(!state->destroyed_inside_call);
    }

    void activeClientClockCannotObserveTheExchangeRecursively() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        state->decision = NativeDownloadPeerState::Eligible;
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QtNativeDownloadClient* observer = nullptr;
        bool nest_once = false;
        int clock_reads = 0;
        int nested_clock_reads = -1;
        std::optional<DownloadSubmissionResult> nested_result;
        QtNativeDownloadClient client{config,
                                      {.now_ms =
                                           [&] {
                                               ++clock_reads;
                                               if (nest_once) {
                                                   nest_once = false;
                                                   const auto before = clock_reads;
                                                   nested_result = observer->poll(request_id);
                                                   nested_clock_reads = clock_reads - before;
                                               }
                                               return qint64{0};
                                           },
                                       .peer_check_factory = factory(state)}};
        observer = &client;
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket{server.nextPendingConnection()};
        QVERIFY(socket);
        // Actual request bytes prove that the client has passed CheckingPeer
        // and entered its Active exchange before the clock hook is armed.
        QTRY_VERIFY(socket->bytesAvailable() > 0);
        clock_reads = 0;
        nest_once = true;
        QVERIFY(!client.poll(request_id));
        QCOMPARE(nested_clock_reads, 0);
        QCOMPARE(clock_reads, 1);
        QVERIFY(!nested_result);
        client.shutdown();
        state->released = true;
        QTRY_VERIFY(client.stopped());
        QVERIFY(!state->destroyed_inside_call);
    }

    void clientShutdownHookCannotReenterDrainObservation() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QtNativeDownloadClient client{config, {.peer_check_factory = factory(state)}};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE(state->begins, 1);

        bool observed_stopped = true;
        int nested_calls = -1;
        state->during_shutdown = [&] {
            // Reentrant shutdown is one-shot. On the unfixed client it closes
            // the socket before the observation below, exposing nested checks.
            client.shutdown();
            const auto before = state->stopped_calls;
            observed_stopped = client.stopped();
            nested_calls = state->stopped_calls - before;
        };
        client.shutdown();
        QVERIFY(!observed_stopped);
        QCOMPARE(nested_calls, 0);
        QVERIFY(!state->hook_threw);
        QVERIFY(!state->destroyed_inside_call);
        state->released = true;
        QTRY_VERIFY(client.stopped());
    }

    void clientStoppedObservationDoesNotReenterTheChecker() {
        const auto config = configuration();
        const auto state = std::make_shared<PeerState>();
        QLocalServer server;
        QVERIFY(server.listen(config.endpoint_name));
        QtNativeDownloadClient client{config, {.peer_check_factory = factory(state)}};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE(state->begins, 1);
        client.shutdown();

        bool observed_stopped = true;
        int nested_calls = -1;
        state->during_stopped = [&] {
            const auto before = state->stopped_calls;
            observed_stopped = client.stopped();
            nested_calls = state->stopped_calls - before;
        };
        QVERIFY(!client.stopped());
        QVERIFY(!observed_stopped);
        QCOMPARE(nested_calls, 0);
        QVERIFY(!state->destroyed_inside_call);
        state->released = true;
        QTRY_VERIFY(client.stopped());
    }
};

QTEST_GUILESS_MAIN(QtNativeDownloadPeerReentrancyTests)
#include "QtNativeDownloadPeerReentrancyTests.moc"
