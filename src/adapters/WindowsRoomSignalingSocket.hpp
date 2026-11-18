#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

class QObject;

namespace music_player::m0 {

// Where the S5 relay lives. Parsed from an origin "scheme://host[:port]" with nothing after it:
// https is the product (wss, TLS checked by Schannel against the system roots); plain http is
// accepted only for a loopback host (127.0.0.1, ::1 or localhost), which the tests use.
struct RoomSignalingTarget final {
    QString host;
    std::uint16_t port{};
    bool secure{};
};
[[nodiscard]] std::optional<RoomSignalingTarget> parseRoomSignalingOrigin(const QString& origin);

// One WebSocket to the S5 relay, on WinHTTP (ADR-0022, S6.3).
//
// Why not rtc::WebSocket: libdatachannel 0.24.6 cannot add request headers (the host needs
// X-Room-Host-Token) nor report the HTTP status of a refused upgrade (409 means the room belongs to
// another host). WinHTTP does both, and its TLS is Schannel, so the listening room never opens an
// Mbed TLS WebSocket: Mbed TLS (no MBEDTLS_THREADING_C) only runs DTLS, one context per
// PeerConnection. That is how the "at most one secure signaling WebSocket per process, from one
// thread" rule of ADR-0022 holds: the number of Mbed TLS WebSockets is zero by construction.
//
// Threads: a worker thread connects and receives; every handler runs on the thread of `context`
// through a queued call. sendText and close must be called on that thread. No handler runs after
// close() or the destructor returns, and close() is safe inside a handler. `context` must outlive
// this object. Nothing is logged; the host token is sent only as a header and never stored after
// the request is built.
class WindowsRoomSignalingSocket final {
  public:
    // http_status: 101 when the WebSocket is open; the HTTP status of a refused upgrade (400, 409,
    // 426, 429...); 0 when the server could not be reached. Called once.
    using OpenResult = std::function<void(int http_status)>;
    using Text = std::function<void(QByteArray text)>;
    // After a successful open: the peer's WebSocket close status, or 0 when the connection broke.
    using Closed = std::function<void(int close_status)>;

    // An empty host_token sends no X-Room-Host-Token header (guests).
    WindowsRoomSignalingSocket(QObject* context, const RoomSignalingTarget& target, const QString& path,
                               const QByteArray& host_token, OpenResult on_open, Text on_text, Closed on_closed);
    ~WindowsRoomSignalingSocket();
    WindowsRoomSignalingSocket(const WindowsRoomSignalingSocket&) = delete;
    WindowsRoomSignalingSocket& operator=(const WindowsRoomSignalingSocket&) = delete;

    // One UTF-8 text message. false when the socket is not open or the send fails.
    bool sendText(const QByteArray& text);
    // Sends a normal close frame when open, aborts any pending request, waits for the worker.
    void close();

    // Text messages above this size (the relay's limit) end the connection.
    static constexpr qsizetype maximum_message_bytes = 16 * 1024;

    struct Shared; // implementation detail, shared with the worker thread

  private:
    std::shared_ptr<Shared> shared_;
    std::thread worker_;
};

} // namespace music_player::m0
