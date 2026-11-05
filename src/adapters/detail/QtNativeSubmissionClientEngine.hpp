#pragma once

#include "adapters/NativeDownloadPeerCheck.hpp"
#include "adapters/NativeHostLaunch.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/WindowsNativeDownloadPeerCheck.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <qt_windows.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace music_player::m0::detail {
// Private implementation shared by the two typed clients. Protocol is a fixed
// C++ policy in each wrapper's translation unit, never a peer-selected value.
// The one-shot state/lifetime/uncertainty contract is deliberately identical.
template <typename Protocol, typename Options> struct QtNativeSubmissionClientEngine {
    using Request = typename Protocol::Request;
    enum class State { Idle, CheckingPeer, Active, Completed, Uncertain, Closed };
    static constexpr quint64 exchange_timeout_ms = 5'000;
    static constexpr qsizetype maximum_response_bytes = Protocol::response_max_payload_bytes + 4 + 1;

    QObject& owner;
    NativeHostConfiguration configuration;
    Options options;
    QLocalSocket socket;
    QElapsedTimer elapsed;
    QTimer peer_timer;
    std::unique_ptr<NativeDownloadPeerCheck> peer;
    bool checking_peer{false};
    bool beginning{false};
    bool polling{false};
    bool shutting_down{false};
    qsizetype callback_depth{0};
    State state{State::Idle};
    bool used{false};
    bool sent{false};
    qint64 started_ms{0};
    qint64 last_ms{0};
    QByteArray request_frame;
    QByteArray response;
    std::optional<quint32> expected_bytes;
    std::string request_id;
    std::size_t item_count{0};
    DownloadSubmissionResult result{};

    QtNativeSubmissionClientEngine(QObject& object, NativeHostConfiguration config, Options settings)
        : owner(object), configuration(std::move(config)), options(std::move(settings)) {
        elapsed.start();
        socket.setReadBufferSize(maximum_response_bytes);
        peer_timer.setInterval(1);
        QObject::connect(&peer_timer, &QTimer::timeout, &owner, [this] { checkPeer(); });
        QObject::connect(&socket, &QLocalSocket::readyRead, &owner, [this] { readAvailable(); });
        QObject::connect(&socket, &QLocalSocket::disconnected, &owner, [this] { finish(); });
        QObject::connect(&socket, &QLocalSocket::errorOccurred, &owner,
                         [this](const QLocalSocket::LocalSocketError error) {
                             if (error != QLocalSocket::PeerClosedError)
                                 fail();
                         });
    }
    std::optional<qint64> now() {
        ++callback_depth;
        const auto release = qScopeGuard([&] { --callback_depth; });
        try {
            const auto clock = options.now_ms;
            const auto current = clock ? clock() : elapsed.elapsed();
            if (current < 0 || current < last_ms)
                throw std::runtime_error{"native exchange clock invalid"};
            last_ms = current;
            return current;
        } catch (...) {
            fail();
            return std::nullopt;
        }
    }
    bool timing() {
        const auto current = now();
        if (!current || (state != State::Active && state != State::CheckingPeer))
            return false;
        if (*current < started_ms ||
            static_cast<quint64>(*current) - static_cast<quint64>(started_ms) >= exchange_timeout_ms) {
            fail();
            return false;
        }
        return true;
    }
    void fail() {
        if (state != State::Active && state != State::CheckingPeer)
            return;
        ++callback_depth;
        const auto release = qScopeGuard([&] { --callback_depth; });
        state = sent ? State::Uncertain : State::Completed;
        result = {};
        peer_timer.stop();
        if (peer)
            peer->shutdown();
        socket.abort();
        request_frame.clear();
        response.clear();
    }
    void checkPeer() {
        if (state != State::CheckingPeer || checking_peer || beginning)
            return;
        checking_peer = true;
        const auto release = qScopeGuard([&] { checking_peer = false; });
        if (!timing() || state != State::CheckingPeer)
            return;
        try {
            ++callback_depth;
            const auto observation = qScopeGuard([&] { --callback_depth; });
            const auto decision = peer->poll();
            if (state != State::CheckingPeer)
                return;
            if (decision == NativeDownloadPeerState::Eligible) {
                peer_timer.stop();
                state = State::Active;
                if (!socket.setSocketDescriptor(socket.socketDescriptor(), QLocalSocket::ConnectedState,
                                                QIODevice::ReadWrite))
                    fail();
                else
                    writeRequest();
            } else if (decision != NativeDownloadPeerState::Pending) {
                fail();
            }
        } catch (...) {
            fail();
        }
    }
    void writeRequest() {
        if (state != State::Active || !timing() || state != State::Active)
            return;
        // A partial/error write may already have transferred a request. Publish
        // uncertainty before entering the socket; never retransmit its bytes.
        sent = true;
        if (socket.write(request_frame) != request_frame.size())
            fail();
        request_frame.clear();
    }
    void readAvailable() {
        if (state != State::Active || !timing() || state != State::Active)
            return;
        const auto remaining = maximum_response_bytes - response.size();
        if (remaining <= 0) {
            fail();
            return;
        }
        if (socket.bytesAvailable() > 0)
            response += socket.read(remaining);
        if (!expected_bytes && response.size() >= 4) {
            expected_bytes = qFromLittleEndian<quint32>(response.constData());
            if (*expected_bytes == 0 || *expected_bytes > Protocol::response_max_payload_bytes) {
                fail();
                return;
            }
        }
        if (expected_bytes && response.size() > static_cast<qsizetype>(*expected_bytes) + 4)
            fail();
    }
    void finish() {
        if (state != State::Active)
            return;
        readAvailable();
        if (state != State::Active)
            return;
        if (!expected_bytes || response.size() != static_cast<qsizetype>(*expected_bytes) + 4) {
            fail();
            return;
        }
        const auto decoded = Protocol::decode(response.mid(4), request_id, item_count);
        if (!decoded) {
            fail();
            return;
        }
        result = *decoded;
        state = State::Completed;
        response.clear();
    }
    DownloadAdmissionResult begin(const Request& request) {
        if (QThread::currentThread() != owner.thread() || used || state != State::Idle)
            return {};
        beginning = true;
        const auto release = qScopeGuard([&] { beginning = false; });
        used = true;
        if (!isValidNativeHostConfiguration(configuration) ||
            !validNativePeerImages(configuration.expected_owner_images))
            return {};
        const auto payload = Protocol::encode(request);
        if (payload.isEmpty())
            return {.result = {.error = DownloadSubmissionError::InvalidRequest}};
        request_id = request.request_id;
        item_count = request.items.size();
        request_frame = Protocol::preamble(configuration.allowed_origin) + encodeNativeMessageFrame(payload);
        state = State::CheckingPeer;
        const auto current = now();
        if (!current || state != State::CheckingPeer)
            return {};
        started_ms = *current;
        // Qt's Windows connectToServer waits synchronously for up to five seconds
        // when every instance is busy. Make exactly one native open, without a
        // busy wait, retry or app launch.
        const auto name = (QStringLiteral("\\\\.\\pipe\\") + configuration.endpoint_name).toStdWString();
        const auto pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            fail();
        } else if (!socket.setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState,
                                               QIODevice::WriteOnly)) {
            CloseHandle(pipe);
            fail();
        } else {
            // WriteOnly adopts ownership without starting the Windows Qt reader.
            // Neither the preamble nor request is written before eligibility.
            try {
                const auto factory = options.peer_check_factory;
                peer = factory ? factory() : std::make_unique<WindowsNativeDownloadPeerCheck>();
                if (state != State::CheckingPeer || !peer ||
                    !peer->begin(socket.socketDescriptor(), NativeDownloadPeerSide::Server,
                                 configuration.expected_owner_images)) {
                    if (peer)
                        peer->shutdown();
                    fail();
                } else if (state == State::CheckingPeer) {
                    peer_timer.start();
                }
            } catch (...) {
                fail();
            }
        }
        if (state == State::Completed || state == State::Closed)
            return {.result = result};
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(const std::string_view id) {
        if (QThread::currentThread() != owner.thread() || id != request_id || beginning || polling ||
            callback_depth != 0)
            return std::nullopt;
        polling = true;
        const auto release = qScopeGuard([&] { polling = false; });
        if (state == State::CheckingPeer)
            checkPeer();
        if (state == State::Active)
            static_cast<void>(timing());
        if (state == State::Uncertain)
            throw std::runtime_error{"native admission acknowledgement was not confirmed"};
        if (state == State::Completed)
            return result;
        return std::nullopt;
    }
    void shutdown() {
        if (QThread::currentThread() != owner.thread())
            return;
        if (shutting_down)
            return;
        shutting_down = true;
        ++callback_depth;
        const auto release = qScopeGuard([&] {
            --callback_depth;
            shutting_down = false;
        });
        state = State::Closed;
        peer_timer.stop();
        if (peer)
            peer->shutdown();
        socket.abort();
        request_frame.clear();
        response.clear();
    }
    bool stopped() {
        if (state != State::Closed || checking_peer || beginning || polling || callback_depth != 0 ||
            socket.state() != QLocalSocket::UnconnectedState)
            return false;
        ++callback_depth;
        const auto release = qScopeGuard([&] { --callback_depth; });
        try {
            return !peer || peer->stopped();
        } catch (...) {
            return false; // Unknown retirement never proves physical shutdown.
        }
    }
};
} // namespace music_player::m0::detail
