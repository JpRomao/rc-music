#include "adapters/WindowsRoomSignalingSocket.hpp"

#include <QMetaObject>
#include <QObject>
#include <QUrl>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace music_player::m0 {
namespace {

constexpr int resolve_timeout_ms = 10000;
constexpr int connect_timeout_ms = 10000;
constexpr int send_timeout_ms = 10000;
constexpr int handshake_timeout_ms = 10000;

bool isLoopbackHost(const QString& host) {
    return host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1") ||
           host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0;
}

std::wstring wide(const QString& text) { return text.toStdWString(); }

} // namespace

std::optional<RoomSignalingTarget> parseRoomSignalingOrigin(const QString& origin) {
    const QUrl url(origin, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        !(url.path().isEmpty() || url.path() == QStringLiteral("/")))
        return std::nullopt;
    RoomSignalingTarget target;
    target.host = url.host();
    if (url.scheme() == QStringLiteral("https")) {
        target.secure = true;
        target.port = static_cast<std::uint16_t>(url.port(INTERNET_DEFAULT_HTTPS_PORT));
    } else if (url.scheme() == QStringLiteral("http") && isLoopbackHost(target.host)) {
        target.port = static_cast<std::uint16_t>(url.port(INTERNET_DEFAULT_HTTP_PORT));
    } else {
        return std::nullopt;
    }
    if (target.port == 0)
        return std::nullopt;
    return target;
}

// Shared between the owner thread and the worker. `handles` are published under `mutex`; whoever
// closes one sets it to null, so each handle is closed exactly once. Closing a handle from the
// owner thread is how WinHTTP cancels a blocking call on the worker (documented for sync handles).
struct WindowsRoomSignalingSocket::Shared {
    std::mutex mutex;
    bool aborted{};
    HINTERNET session{};
    HINTERNET connection{};
    HINTERNET request{};
    HINTERNET websocket{};

    std::mutex send_mutex; // one outstanding send at a time (WinHTTP rule)
    // Read on the owner thread only: queued handlers run only while this is true.
    std::shared_ptr<bool> live = std::make_shared<bool>(true);

    void closeAllLocked() {
        for (auto* handle : {&websocket, &request, &connection, &session}) {
            if (*handle != nullptr) {
                WinHttpCloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }
};

namespace {

struct WorkerInput {
    QObject* context;
    RoomSignalingTarget target;
    QString path;
    QByteArray host_token;
    WindowsRoomSignalingSocket::OpenResult on_open;
    WindowsRoomSignalingSocket::Text on_text;
    WindowsRoomSignalingSocket::Closed on_closed;
};

template <typename Function> void post(QObject* context, const std::shared_ptr<bool>& live, Function function) {
    QMetaObject::invokeMethod(
        context,
        [live, function]() mutable {
            if (*live)
                function();
        },
        Qt::QueuedConnection);
}

} // namespace

static void runSocket(const std::shared_ptr<WindowsRoomSignalingSocket::Shared>& shared, WorkerInput input) {
    const auto live = shared->live;
    auto open_failed = [&](int status) {
        std::lock_guard lock(shared->mutex);
        if (!shared->aborted)
            post(input.context, live, [on_open = input.on_open, status] { on_open(status); });
        shared->closeAllLocked();
    };

    const auto secure = input.target.secure;
    HINTERNET session =
        WinHttpOpen(L"RC Music", secure ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    {
        std::lock_guard lock(shared->mutex);
        shared->session = session;
        if (shared->aborted || session == nullptr) {
            if (!shared->aborted)
                post(input.context, live, [on_open = input.on_open] { on_open(0); });
            shared->closeAllLocked();
            return;
        }
        WinHttpSetTimeouts(session, resolve_timeout_ms, connect_timeout_ms, send_timeout_ms, handshake_timeout_ms);
        shared->connection = WinHttpConnect(session, wide(input.target.host).c_str(), input.target.port, 0);
        if (shared->connection != nullptr) {
            const auto path = wide(input.path);
            shared->request = WinHttpOpenRequest(shared->connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
        }
    }
    HINTERNET request{};
    {
        std::lock_guard lock(shared->mutex);
        request = shared->request;
    }
    if (request == nullptr || !WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        open_failed(0);
        return;
    }
    // No credentials, no redirects: the relay never redirects, and following one could carry the
    // host token elsewhere.
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (!input.host_token.isEmpty()) {
        const auto header = L"X-Room-Host-Token: " + wide(QString::fromLatin1(input.host_token));
        if (!WinHttpAddRequestHeaders(request, header.c_str(), static_cast<DWORD>(header.size()),
                                      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            open_failed(0);
            return;
        }
    }
    input.host_token.fill('\0');
    input.host_token.clear();

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        open_failed(0);
        return;
    }
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        open_failed(0);
        return;
    }
    if (status != HTTP_STATUS_SWITCH_PROTOCOLS) {
        open_failed(static_cast<int>(status));
        return;
    }
    HINTERNET websocket = WinHttpWebSocketCompleteUpgrade(request, 0);
    {
        std::lock_guard lock(shared->mutex);
        if (shared->aborted) {
            if (websocket != nullptr)
                WinHttpCloseHandle(websocket);
            shared->closeAllLocked();
            return;
        }
        if (websocket == nullptr) {
            post(input.context, live, [on_open = input.on_open] { on_open(0); });
            shared->closeAllLocked();
            return;
        }
        shared->websocket = websocket;
        WinHttpCloseHandle(shared->request);
        shared->request = nullptr;
        post(input.context, live, [on_open = input.on_open] { on_open(101); });
    }

    std::vector<char> buffer(4096);
    QByteArray message;
    int close_status = 0;
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        const auto error =
            WinHttpWebSocketReceive(websocket, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
        if (error == ERROR_WINHTTP_TIMEOUT) // defensive: an idle relay socket is not an error
            continue;
        if (error != NO_ERROR)
            break;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            USHORT code = 0;
            DWORD reason_size = 0;
            std::lock_guard lock(shared->mutex);
            if (!shared->aborted &&
                WinHttpWebSocketQueryCloseStatus(websocket, &code, nullptr, 0, &reason_size) == NO_ERROR)
                close_status = code;
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            message.append(buffer.data(), static_cast<qsizetype>(read));
            if (message.size() > WindowsRoomSignalingSocket::maximum_message_bytes)
                break; // the relay never sends this much: treat it as a broken connection
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                std::lock_guard lock(shared->mutex);
                if (shared->aborted)
                    return;
                post(input.context, live, [on_text = input.on_text, message] { on_text(message); });
            }
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
                message.clear(); // binary messages are not part of the protocol: dropped
        }
    }
    std::lock_guard lock(shared->mutex);
    if (!shared->aborted)
        post(input.context, live, [on_closed = input.on_closed, close_status] { on_closed(close_status); });
    shared->closeAllLocked();
}

WindowsRoomSignalingSocket::WindowsRoomSignalingSocket(QObject* context, const RoomSignalingTarget& target,
                                                       const QString& path, const QByteArray& host_token,
                                                       OpenResult on_open, Text on_text, Closed on_closed)
    : shared_(std::make_shared<Shared>()) {
    worker_ = std::thread(
        runSocket, shared_,
        WorkerInput{context, target, path, host_token, std::move(on_open), std::move(on_text), std::move(on_closed)});
}

WindowsRoomSignalingSocket::~WindowsRoomSignalingSocket() { close(); }

bool WindowsRoomSignalingSocket::sendText(const QByteArray& text) {
    if (text.size() > maximum_message_bytes)
        return false;
    std::lock_guard send_lock(shared_->send_mutex);
    HINTERNET websocket{};
    {
        std::lock_guard lock(shared_->mutex);
        if (shared_->aborted || shared_->websocket == nullptr)
            return false;
        websocket = shared_->websocket;
    }
    // The handle stays valid here: only close() (this thread) or the worker's exit path closes it,
    // and a close from the worker during the send just makes the send fail (WinHTTP cancels it).
    return WinHttpWebSocketSend(websocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                const_cast<char*>(text.constData()), static_cast<DWORD>(text.size())) == NO_ERROR;
}

void WindowsRoomSignalingSocket::close() {
    *shared_->live = false;
    {
        std::lock_guard lock(shared_->mutex);
        if (!shared_->aborted) {
            shared_->aborted = true;
            if (shared_->websocket != nullptr)
                WinHttpWebSocketShutdown(shared_->websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            shared_->closeAllLocked();
        }
    }
    if (worker_.joinable())
        worker_.join();
}

} // namespace music_player::m0
