#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/QtNativeDownloadClient.hpp"
#include "adapters/QtNativeDownloadServer.hpp"
#include "adapters/WindowsNativeDownloadLease.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QtEndian>
#include <qt_windows.h>

#include <array>
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
    return {origin,
            QStringLiteral("rc-music-native-transport-") + QUuid::createUuid().toString(QUuid::Id128),
            {QCoreApplication::applicationFilePath()},
            {QCoreApplication::applicationFilePath()}};
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
struct HeldPeerState final {
    NativeDownloadPeerState state{NativeDownloadPeerState::Pending};
    int begins{0};
    int shutdowns{0};
    bool stopped{false};
    qintptr borrowed_pipe{0};
    int polls{0};
};
class HeldPeerCheck final : public NativeDownloadPeerCheck {
  public:
    explicit HeldPeerCheck(std::shared_ptr<HeldPeerState> state) : state_(std::move(state)) {}
    bool begin(qintptr pipe, NativeDownloadPeerSide, const QStringList&) override {
        ++state_->begins;
        state_->borrowed_pipe = pipe;
        return GetFileType(reinterpret_cast<HANDLE>(pipe)) == FILE_TYPE_PIPE;
    }
    NativeDownloadPeerState poll() override {
        ++state_->polls;
        return state_->state;
    }
    void shutdown() noexcept override { ++state_->shutdowns; }
    bool stopped() override { return state_->stopped; }

  private:
    std::shared_ptr<HeldPeerState> state_;
};
void pump() { QCoreApplication::processEvents(); }
} // namespace

class QtNativeDownloadTransportTests final : public QObject {
    Q_OBJECT
  private slots:
    void pendingPeerChecksLeaveRequestAndReplyBytesInTheirNativePipes() {
        const auto config = configuration();
        const auto server_state = std::make_shared<HeldPeerState>();
        const auto client_state = std::make_shared<HeldPeerState>();
        Submission owner;
        QtNativeDownloadServer server{
            config,
            owner,
            {.now_ms = [] { return qint64{0}; },
             .peer_check_factory = [server_state] { return std::make_unique<HeldPeerCheck>(server_state); }}};
        QVERIFY(server.start());
        QLocalSocket sender;
        sender.connectToServer(config.endpoint_name);
        QTRY_COMPARE(sender.state(), QLocalSocket::ConnectedState);
        const auto input = framedRequest();
        QCOMPARE(sender.write(input), input.size());
        QTRY_COMPARE(server_state->begins, 1);
        QTRY_VERIFY(server_state->polls > 1);
        DWORD queued = 0;
        // With a zero-quota pipe, overlapped writes may remain pending until a
        // read. Observe queued native bytes without waiting for write completion.
        QTRY_VERIFY(PeekNamedPipe(reinterpret_cast<HANDLE>(server_state->borrowed_pipe), nullptr, 0, nullptr, &queued,
                                  nullptr) &&
                    queued == static_cast<DWORD>(input.size()));
        QCOMPARE(owner.begins, 0);
        server_state->state = NativeDownloadPeerState::Eligible;
        QTRY_COMPARE(owner.begins, 1);
        server.beginShutdown();
        server_state->stopped = true;
        QTRY_VERIFY(server.stopped());

        auto client_config = configuration();
        QLocalServer receiver;
        QVERIFY(receiver.listen(client_config.endpoint_name));
        QtNativeDownloadClient client{
            client_config,
            {.now_ms = [] { return qint64{0}; },
             .peer_check_factory = [client_state] { return std::make_unique<HeldPeerCheck>(client_state); }}};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(receiver.hasPendingConnections());
        std::unique_ptr<QLocalSocket> reply{receiver.nextPendingConnection()};
        const auto output = encodeNativeMessageFrame(
            encodeNativeDownloadResponse(request_id, {.error = DownloadSubmissionError::None, .job_ids = {"job-1"}}));
        QCOMPARE(reply->write(output), output.size());
        QTRY_VERIFY(client_state->polls > 1);
        QTRY_VERIFY(PeekNamedPipe(reinterpret_cast<HANDLE>(client_state->borrowed_pipe), nullptr, 0, nullptr, &queued,
                                  nullptr) &&
                    queued == static_cast<DWORD>(output.size()));
        QCOMPARE(reply->bytesAvailable(), qint64{0});
        client_state->state = NativeDownloadPeerState::Eligible;
        QTRY_COMPARE(reply->bytesAvailable(),
                     native_download_ipc::preamble_bytes + 4 + encodeNativeDownloadRequest(request()).size());
        const auto delivered = reply->readAll();
        QCOMPARE(delivered.first(native_download_ipc::preamble_bytes), native_download_ipc::preamble(origin));
        const auto frame = delivered.mid(native_download_ipc::preamble_bytes);
        QVERIFY(frame.size() > 4);
        QCOMPARE(qFromLittleEndian<quint32>(frame.constData()), static_cast<quint32>(frame.size() - 4));
        const auto decoded = decodeNativeDownloadRequest(frame.mid(4));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QVERIFY(decoded.request == request());
        reply->disconnectFromServer();
        std::optional<DownloadSubmissionResult> result;
        QTRY_VERIFY((result = client.poll(request_id)).has_value());
        QCOMPARE(result->job_ids, std::vector<std::string>{"job-1"});
        client.shutdown();
        client_state->stopped = true;
        QTRY_VERIFY(client.stopped());
    }
    void pendingClientImageCheckWritesNothingAndLateEligibilityCannotEscapeShutdown() {
        const auto config = configuration();
        QLocalServer peer;
        QVERIFY(peer.listen(config.endpoint_name));
        const auto state = std::make_shared<HeldPeerState>();
        QtNativeDownloadClient client{
            config, {.peer_check_factory = [state] { return std::make_unique<HeldPeerCheck>(state); }}};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(peer.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket{peer.nextPendingConnection()};
        QTRY_COMPARE(state->begins, 1);
        QVERIFY(!client.poll(request_id));
        QCOMPARE(socket->bytesAvailable(), qint64{0});
        client.shutdown();
        QVERIFY(!client.stopped());
        QVERIFY(state->shutdowns > 0);
        state->state = NativeDownloadPeerState::Eligible;
        pump();
        QCOMPARE(socket->bytesAvailable(), qint64{0});
        QVERIFY(!client.stopped());
        state->stopped = true;
        QTRY_VERIFY(client.stopped());
        QTRY_COMPARE(socket->state(), QLocalSocket::UnconnectedState);
        QVERIFY(socket->readAll().isEmpty());
    }
    void pendingServerImageCheckUsesOriginalDeadlineAndDrainsAfterLateEligibility() {
        const auto config = configuration();
        const auto state = std::make_shared<HeldPeerState>();
        qint64 now = 0;
        Submission owner;
        QtNativeDownloadServer server{
            config,
            owner,
            {.now_ms = [&] { return now; },
             .automatic_polling = false,
             .peer_check_factory = [state] { return std::make_unique<HeldPeerCheck>(state); }}};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(state->begins, 1);
        QCOMPARE(server.activeConnectionCount(), qsizetype{1});
        now = 4'999;
        server.poll();
        QCOMPARE(owner.begins, 0);
        now = 5'000;
        server.poll();
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(owner.begins, 0);
        QVERIFY(state->shutdowns > 0);
        state->state = NativeDownloadPeerState::Eligible;
        server.beginShutdown();
        QVERIFY(!server.stopped());
        pump();
        QCOMPARE(owner.begins, 0);
        state->stopped = true;
        QTRY_VERIFY(server.stopped());
        QVERIFY(socket.readAll().isEmpty());
    }
    void pendingImageChecksShareAllEightSlotsAndMustDrainBeforeStopped() {
        const auto config = configuration();
        Submission owner;
        std::vector<std::shared_ptr<HeldPeerState>> states;
        QtNativeDownloadServer server{config, owner, {.peer_check_factory = [&] {
                                          auto state = std::make_shared<HeldPeerState>();
                                          states.push_back(state);
                                          return std::make_unique<HeldPeerCheck>(state);
                                      }}};
        QVERIFY(server.start());
        std::array<QLocalSocket, 8> sockets;
        for (auto& socket : sockets) {
            socket.connectToServer(config.endpoint_name);
            QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
            QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        }
        QTRY_COMPARE(states.size(), std::size_t{8});
        QCOMPARE(server.activeConnectionCount(), qsizetype{8});
        QLocalSocket ninth;
        QSignalSpy disconnected{&ninth, &QLocalSocket::disconnected};
        QSignalSpy errors{&ninth, &QLocalSocket::errorOccurred};
        ninth.connectToServer(config.endpoint_name);
        QTRY_VERIFY(disconnected.count() + errors.count() > 0);
        QCOMPARE(states.size(), std::size_t{8});
        QCOMPARE(owner.begins, 0);
        server.beginShutdown();
        QVERIFY(!server.stopped());
        for (const auto& state : states) {
            QVERIFY(state->shutdowns > 0);
            state->state = NativeDownloadPeerState::Eligible;
            state->stopped = true;
        }
        QTRY_VERIFY(server.stopped());
        QCOMPARE(owner.begins, 0);
        for (auto& socket : sockets) {
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
            QVERIFY(socket.readAll().isEmpty());
        }
    }
    void missingPeerImageProfilesRefuseBeforeOpeningIngressOrSendingBytes() {
        Submission owner;
        auto config = configuration();
        config.expected_host_images.clear();
        QtNativeDownloadServer server{config, owner};
        QVERIFY(!server.start());
        QCOMPARE(owner.begins, 0);
        config = configuration();
        config.expected_owner_images.clear();
        QLocalServer peer;
        QVERIFY(peer.listen(config.endpoint_name));
        QtNativeDownloadClient client{config};
        const auto result = client.begin(request());
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::NotAvailable);
        pump();
        QVERIFY(!peer.hasPendingConnections());
    }
    void unlistedServerImageReceivesNoPreambleOrRequestBytes() {
        auto config = configuration();
        config.expected_owner_images = {QCoreApplication::applicationFilePath() + QStringLiteral(".unlisted.exe")};
        QLocalServer peer;
        QVERIFY(peer.listen(config.endpoint_name));
        QByteArray received;
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY(peer.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket{peer.nextPendingConnection()};
        connect(socket.get(), &QLocalSocket::readyRead, socket.get(), [&] {
            received += socket->readAll();
            socket->disconnectFromServer();
        });
        if (socket->bytesAvailable()) {
            received += socket->readAll();
            socket->disconnectFromServer();
        }
        QTRY_COMPARE(socket->state(), QLocalSocket::UnconnectedState);
        QVERIFY(received.isEmpty());
        const auto result = client.poll(request_id);
        QVERIFY(result);
        QCOMPARE(result->error, DownloadSubmissionError::NotAvailable);
        client.shutdown();
        QTRY_VERIFY(client.stopped());
    }
    void unlistedClientImageCannotDispatchEvenWithValidPreambleAndFrame() {
        auto config = configuration();
        config.expected_host_images = {QCoreApplication::applicationFilePath() + QStringLiteral(".unlisted.exe")};
        Submission owner;
        owner.result = {.state = DownloadAdmissionState::Completed,
                        .result = {.error = DownloadSubmissionError::NotAvailable}};
        QtNativeDownloadServer server{config, owner};
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(owner.begins, 0);
        QVERIFY(socket.readAll().isEmpty());
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
    }
    void secondOwnerOfSameEndpointIsRefusedWithoutDispatch() {
        Submission first_owner;
        Submission second_owner;
        const auto config = configuration();
        QtNativeDownloadServer first{config, first_owner};
        QtNativeDownloadServer second{config, second_owner};
        QVERIFY(first.start());
        QVERIFY(!second.start());
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE(first_owner.begins, 1);
        QCOMPARE(second_owner.begins, 0);
        client.shutdown();
        first.beginShutdown();
        second.beginShutdown();
        QVERIFY(first.stopped());
        QVERIFY(second.stopped());
    }
    void endpointCaseAndOriginCannotBypassExclusiveOwner() {
        Submission first_owner;
        Submission second_owner;
        const auto config = configuration();
        auto alias = config;
        alias.endpoint_name = alias.endpoint_name.toUpper();
        alias.allowed_origin = QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/");
        QtNativeDownloadServer first{config, first_owner};
        QtNativeDownloadServer second{alias, second_owner};
        QVERIFY(first.start());
        QVERIFY(!second.start());
        QCOMPARE(second_owner.begins, 0);
    }
    void ownerLeaseOutlivesIngressShutdownUntilOwnerIsDestroyed() {
        Submission owner;
        const auto config = configuration();
        {
            QtNativeDownloadServer first{config, owner};
            QVERIFY(first.start());
            first.beginShutdown();
            QVERIFY(first.stopped());
            QtNativeDownloadServer during_owner_drain{config, owner};
            QVERIFY(!during_owner_drain.start());
        }
        QtNativeDownloadServer successor{config, owner};
        QVERIFY(successor.start());
        QtNativeDownloadClient client{config};
        QCOMPARE(client.begin(request()).state, DownloadAdmissionState::Pending);
        QTRY_COMPARE(owner.begins, 1);
        client.shutdown();
        successor.beginShutdown();
        QVERIFY(successor.stopped());
    }
    void failedListenReleasesItsReservationWhileFailedServerStillExists() {
        Submission owner;
        const auto config = configuration();
        const auto name = QStringLiteral("\\\\.\\pipe\\") + config.endpoint_name;
        const auto blocker = CreateNamedPipeW(
            name.toStdWString().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4'096, 4'096, 0, nullptr);
        QVERIFY(blocker != INVALID_HANDLE_VALUE);
        const auto close_blocker = qScopeGuard([&] { CloseHandle(blocker); });
        QtNativeDownloadServer failed{config, owner};
        QVERIFY(!failed.start());
        WindowsNativeDownloadLease successor;
        QVERIFY(successor.acquire(config.endpoint_name));
        QCOMPARE(owner.begins, 0);
    }
    void preexistingUnlimitedPipeWithoutLeaseRefusesNamespaceWithoutAdmission() {
        Submission owner;
        const auto config = configuration();
        const auto name = QStringLiteral("\\\\.\\pipe\\") + config.endpoint_name;
        auto blocker = CreateNamedPipeW(name.toStdWString().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                        PIPE_UNLIMITED_INSTANCES, 4'096, 4'096, 3'000, nullptr);
        QVERIFY(blocker != INVALID_HANDLE_VALUE);
        const auto close_blocker = qScopeGuard([&] {
            if (blocker != INVALID_HANDLE_VALUE)
                CloseHandle(blocker);
        });
        QtNativeDownloadServer failed{config, owner};
        const auto started = failed.start();
        QCOMPARE(owner.begins, 0);
        QVERIFY(!started);
        {
            WindowsNativeDownloadLease successor;
            QVERIFY(successor.acquire(config.endpoint_name));
        }
        QVERIFY(CloseHandle(blocker));
        blocker = INVALID_HANDLE_VALUE;
        QtNativeDownloadServer successor{config, owner};
        QVERIFY(successor.start());
        QCOMPARE(owner.begins, 0);
    }
    void rawFirstPipeNamespaceOutlivesIngressShutdownUntilOwnerIsDestroyed() {
        Submission owner;
        const auto config = configuration();
        const auto name = QStringLiteral("\\\\.\\pipe\\") + config.endpoint_name;
        const auto create_first_instance = [&] {
            return CreateNamedPipeW(name.toStdWString().c_str(),
                                    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                    PIPE_UNLIMITED_INSTANCES, 4'096, 4'096, 0, nullptr);
        };
        HANDLE probe = INVALID_HANDLE_VALUE;
        const auto close_probe = qScopeGuard([&] {
            if (probe != INVALID_HANDLE_VALUE)
                CloseHandle(probe);
        });
        auto server = std::make_unique<QtNativeDownloadServer>(config, owner);
        QVERIFY(server->start());
        probe = create_first_instance();
        const auto running_error = GetLastError();
        QVERIFY(probe == INVALID_HANDLE_VALUE);
        QCOMPARE(running_error, DWORD{ERROR_ACCESS_DENIED});
        server->beginShutdown();
        QVERIFY(server->stopped());
        QCOMPARE(server->activeConnectionCount(), qsizetype{0});
        probe = create_first_instance();
        const auto stopped_error = GetLastError();
        QVERIFY(probe == INVALID_HANDLE_VALUE);
        QCOMPARE(stopped_error, DWORD{ERROR_ACCESS_DENIED});
        QCOMPARE(owner.begins, 0);
        server.reset();
        probe = create_first_instance();
        QVERIFY(probe != INVALID_HANDLE_VALUE);
        QCOMPARE(owner.begins, 0);
    }
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
    void eightPendingConnectionsRejectNinthAndReuseReleasedCapacityUntilShutdown() {
        Submission owner;
        qint64 now = 0;
        const auto config = configuration();
        QtNativeDownloadServer server{config, owner, {.now_ms = [&] { return now; }, .automatic_polling = false}};
        QVERIFY(server.start());
        std::array<QLocalSocket, 8> sockets;
        int admitted = 0;
        for (auto& socket : sockets) {
            socket.connectToServer(config.endpoint_name);
            QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
            QCOMPARE(socket.write(framedRequest()), framedRequest().size());
            ++admitted;
            QTRY_COMPARE(owner.begins, admitted);
        }
        QCOMPARE(server.activeConnectionCount(), qsizetype{8});
        QLocalSocket ninth;
        QSignalSpy ninth_errors{&ninth, &QLocalSocket::errorOccurred};
        QSignalSpy ninth_disconnected{&ninth, &QLocalSocket::disconnected};
        connect(&ninth, &QLocalSocket::connected, &ninth, [&] { static_cast<void>(ninth.write(framedRequest())); });
        ninth.connectToServer(config.endpoint_name);
        QTRY_VERIFY(ninth_errors.count() + ninth_disconnected.count() > 0);
        QCOMPARE(ninth.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(server.activeConnectionCount(), qsizetype{8});
        QCOMPARE(owner.begins, 8);
        sockets.front().abort();
        QTRY_COMPARE(server.activeConnectionCount(), qsizetype{7});
        server.poll();
        QLocalSocket replacement;
        replacement.connectToServer(config.endpoint_name);
        QTRY_COMPARE(replacement.state(), QLocalSocket::ConnectedState);
        QCOMPARE(replacement.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 9);
        QCOMPARE(server.activeConnectionCount(), qsizetype{8});
        server.beginShutdown();
        QTRY_VERIFY(server.stopped());
        for (auto& socket : sockets)
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QTRY_COMPARE(replacement.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(server.activeConnectionCount(), qsizetype{0});
        QCOMPARE(owner.begins, 9);
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
    void busyRawPipeRejectsClientPromptlyWithoutRetry() {
        const auto config = configuration();
        const auto name = QStringLiteral("\\\\.\\pipe\\") + config.endpoint_name;
        auto blocker = CreateNamedPipeW(
            name.toStdWString().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4'096, 4'096, 0, nullptr);
        QVERIFY(blocker != INVALID_HANDLE_VALUE);
        const auto close_blocker = qScopeGuard([&] {
            if (blocker != INVALID_HANDLE_VALUE)
                CloseHandle(blocker);
        });
        auto private_client = CreateFileW(name.toStdWString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        QVERIFY(private_client != INVALID_HANDLE_VALUE);
        const auto close_private_client = qScopeGuard([&] {
            if (private_client != INVALID_HANDLE_VALUE)
                CloseHandle(private_client);
        });
        QtNativeDownloadClient client{config};
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = client.begin(request());
        const auto elapsed_ms = elapsed.elapsed();
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(result.result.job_ids.empty());
        QVERIFY(CloseHandle(private_client));
        private_client = INVALID_HANDLE_VALUE;
        QVERIFY(CloseHandle(blocker));
        blocker = INVALID_HANDLE_VALUE;
        QLocalServer late_server;
        QVERIFY(late_server.listen(config.endpoint_name));
        QSignalSpy arrivals{&late_server, &QLocalServer::newConnection};
        QCOMPARE(client.begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        pump();
        QCOMPARE(arrivals.size(), 0);
        client.shutdown();
        QVERIFY(client.stopped());
        // This is the prompt submission-port contract, not a throughput budget.
        QVERIFY2(elapsed_ms < 1'000,
                 qPrintable(QStringLiteral("begin blocked for %1 ms on a busy local pipe").arg(elapsed_ms)));
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
        bool admitted_another_owner_in_callback = true;
        owner.during_begin = [&] {
            server.beginShutdown();
            observed_stopped_in_callback = server.stopped();
            WindowsNativeDownloadLease contender;
            admitted_another_owner_in_callback = contender.acquire(config.endpoint_name);
        };
        QVERIFY(server.start());
        QLocalSocket socket;
        socket.connectToServer(config.endpoint_name);
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.write(framedRequest()), framedRequest().size());
        QTRY_COMPARE(owner.begins, 1);
        QVERIFY(!observed_stopped_in_callback);
        QVERIFY(!admitted_another_owner_in_callback);
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
