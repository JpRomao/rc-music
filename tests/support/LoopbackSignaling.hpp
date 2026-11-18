#pragma once

// LoopbackSignaling: the S5 relay protocol (services/room-signaling/src/index.ts + RoomRelay.ts,
// docs/room-signaling.md) on 127.0.0.1, for the transport tests. Header-only test support.
//
// Deviation from the plan text ("using rtc::WebSocketServer"): libdatachannel's WebSocketServer
// accepts every upgrade and exposes neither the request headers nor a way to answer 400/409/429, so
// it cannot check X-Room-Host-Token or refuse a second host. This server is a small RFC 6455
// implementation on QTcpServer instead: it needs no rtc headers, so the libdatachannel boundary
// (tests/cmake/ListeningRoomBuildBoundaryTests.cmake) is unchanged.
//
// What it reproduces: the routes and their validation (room_id 22 canonical base64url, host token 43
// canonical base64url -> else 400, Upgrade: websocket -> else 426), the first host token owning the
// room (SHA-256 kept; another token -> 409; the same token replaces the old host, closed with 4001),
// 10 guests per room (the 11th -> 429), peers numbered from 0, the {to,data} -> {from,data} relay
// with its drops (guest to anything but "host", host to an unknown peer, bad JSON, extra/missing
// keys, empty or non-base64 data, binary frames), "peer-left" to the host, "host-offline" to guests
// (also to a guest joining without a host), 16 KiB per message (close 1009) and 20 messages per
// second per socket. Not reproduced: the 10-minute alarm (rooms live as long as the server).
//
// For the tests it records every text message a client sent (`received`) and every host request's
// X-Room-Host-Token header (`host_tokens`, with "<absent>" when the header is missing). It runs on
// the thread that creates it (the test's event loop).

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace music_player::m0::testing {

class LoopbackSignaling final : public QObject {
  public:
    LoopbackSignaling() {
        QObject::connect(&server_, &QTcpServer::newConnection, this, [this] { accept(); });
        server_.listen(QHostAddress::LocalHost, 0);
        port_ = server_.serverPort();
    }
    // stop() marks every connection gone, so the sockets' disconnected signals during ~server_
    // (declared last, destroyed first) find nothing to do.
    ~LoopbackSignaling() override { stop(); }

    [[nodiscard]] bool isListening() const { return server_.isListening(); }
    [[nodiscard]] quint16 port() const { return port_; } // kept after stop(): nobody listens there then
    [[nodiscard]] QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(port()); }

    // Stops listening and drops every connection without a close frame (a crashed relay).
    void stop() {
        server_.close();
        for (const auto& connection : connections_)
            drop(connection.get());
    }

    QList<QByteArray> received;    // text messages from clients, as sent
    QList<QByteArray> host_tokens; // X-Room-Host-Token of each host request
    int guest_requests{};

  private:
    struct Room {
        QByteArray token_sha256;
        QPointer<QTcpSocket> host;
        std::map<std::uint32_t, QPointer<QTcpSocket>> guests;
        std::uint32_t next_peer{};
    };
    enum class Role { Pending, Host, Guest };
    struct Connection {
        QTcpSocket* socket{};
        QByteArray buffer;
        Role role{Role::Pending};
        QString room;
        std::uint32_t peer{};
        QByteArray fragments;
        bool in_message{};
        bool text_message{};
        qint64 window_start{};
        int window_count{};
        bool gone{};
    };

    static constexpr qsizetype max_message = 16 * 1024;
    static constexpr int max_guests = 10;
    static constexpr int max_per_second = 20;

    void accept() {
        while (auto* socket = server_.nextPendingConnection()) {
            auto* connection = connections_.emplace_back(std::make_unique<Connection>(Connection{socket})).get();
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, connection] { read(connection); });
            QObject::connect(socket, &QTcpSocket::disconnected, this, [this, connection] { leave(connection); });
        }
    }

    static QByteArray status(int code, const char* text) {
        return "HTTP/1.1 " + QByteArray::number(code) + ' ' + text +
               "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }
    void refuse(Connection* connection, int code, const char* text) {
        connection->socket->write(status(code, text));
        connection->socket->disconnectFromHost();
    }

    void read(Connection* connection) {
        if (connection->gone)
            return;
        connection->buffer += connection->socket->readAll();
        if (connection->role == Role::Pending) {
            const auto end = connection->buffer.indexOf("\r\n\r\n");
            if (end < 0) {
                if (connection->buffer.size() > 8192)
                    refuse(connection, 400, "Bad Request");
                return;
            }
            const auto head = connection->buffer.left(end);
            connection->buffer.remove(0, end + 4);
            if (!handshake(connection, head))
                return;
        }
        frames(connection);
    }

    bool handshake(Connection* connection, const QByteArray& head) {
        const auto lines = head.split('\n');
        const auto request = lines.value(0).trimmed().split(' ');
        std::map<QByteArray, QByteArray> headers;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const auto line = lines[i].trimmed();
            const auto colon = line.indexOf(':');
            if (colon > 0)
                headers[line.left(colon).trimmed().toLower()] = line.mid(colon + 1).trimmed();
        }
        static const QRegularExpression route(QStringLiteral("^/rooms/([^/]+)/(host|guest)$"));
        static const QRegularExpression room_id(QStringLiteral("^[A-Za-z0-9_-]{21}[AQgw]$"));
        static const QRegularExpression token_shape(QStringLiteral("^[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$"));
        const auto match = route.match(QString::fromLatin1(request.value(1)));
        if (request.size() != 3 || request[0] != "GET" || !match.hasMatch() ||
            !room_id.match(match.captured(1)).hasMatch()) {
            refuse(connection, 404, "Not Found");
            return false;
        }
        if (headers["upgrade"].toLower() != "websocket" || !headers.contains("sec-websocket-key")) {
            refuse(connection, 426, "Upgrade Required");
            return false;
        }
        const auto id = match.captured(1);
        auto& room = rooms_[id];
        const auto as_host = match.captured(2) == QStringLiteral("host");
        if (as_host) {
            const auto found = headers.find("x-room-host-token");
            host_tokens.push_back(found == headers.end() ? QByteArray("<absent>") : found->second);
            if (found == headers.end() || !token_shape.match(QString::fromLatin1(found->second)).hasMatch()) {
                refuse(connection, 400, "Bad Request");
                return false;
            }
            const auto digest = QCryptographicHash::hash(found->second, QCryptographicHash::Sha256);
            if (room.token_sha256.isEmpty())
                room.token_sha256 = digest;
            else if (room.token_sha256 != digest) {
                refuse(connection, 409, "Conflict");
                return false;
            }
        } else {
            ++guest_requests;
            int open = 0;
            for (const auto& [peer, guest] : room.guests)
                open += guest ? 1 : 0;
            if (open >= max_guests) {
                refuse(connection, 429, "Too Many Requests");
                return false;
            }
        }

        const auto accept =
            QCryptographicHash::hash(headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                     QCryptographicHash::Sha1)
                .toBase64();
        connection->socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                  "Sec-WebSocket-Accept: " +
                                  accept + "\r\n\r\n");
        connection->room = id;
        if (as_host) {
            const QPointer<QTcpSocket> previous = room.host;
            connection->role = Role::Host;
            room.host = connection->socket;
            if (previous)
                if (auto* old = find(previous))
                    closeWith(old, 4001);
        } else {
            connection->role = Role::Guest;
            connection->peer = room.next_peer++;
            room.guests[connection->peer] = connection->socket;
            if (!room.host)
                sendText(connection->socket, R"({"event":"host-offline"})");
        }
        return true;
    }

    Connection* find(QTcpSocket* socket) {
        for (const auto& connection : connections_)
            if (connection->socket == socket && !connection->gone)
                return connection.get();
        return nullptr;
    }

    static void sendFrame(QTcpSocket* socket, std::uint8_t opcode, const QByteArray& payload) {
        QByteArray frame;
        frame.append(static_cast<char>(0x80 | opcode));
        const auto size = static_cast<std::uint64_t>(payload.size());
        if (size < 126) {
            frame.append(static_cast<char>(size));
        } else if (size <= 0xFFFF) {
            frame.append(static_cast<char>(126));
            frame.append(static_cast<char>(size >> 8));
            frame.append(static_cast<char>(size & 0xFF));
        } else {
            frame.append(static_cast<char>(127));
            for (int shift = 56; shift >= 0; shift -= 8)
                frame.append(static_cast<char>((size >> shift) & 0xFF));
        }
        socket->write(frame + payload);
    }
    static void sendText(QTcpSocket* socket, const QByteArray& text) {
        if (socket && socket->state() == QAbstractSocket::ConnectedState)
            sendFrame(socket, 0x1, text);
    }
    void closeWith(Connection* connection, int code) {
        QByteArray payload;
        payload.append(static_cast<char>((code >> 8) & 0xFF));
        payload.append(static_cast<char>(code & 0xFF));
        sendFrame(connection->socket, 0x8, payload);
        connection->socket->flush();
        connection->socket->disconnectFromHost();
    }

    void frames(Connection* connection) {
        auto& buffer = connection->buffer;
        while (!connection->gone && connection->socket->state() == QAbstractSocket::ConnectedState) {
            if (buffer.size() < 2)
                return;
            const auto first = static_cast<std::uint8_t>(buffer[0]);
            const auto second = static_cast<std::uint8_t>(buffer[1]);
            const bool fin = (first & 0x80) != 0;
            const std::uint8_t opcode = first & 0x0F;
            if ((second & 0x80) == 0 || (first & 0x70) != 0) { // clients must mask; no extensions
                closeWith(connection, 1002);
                return;
            }
            std::uint64_t size = second & 0x7F;
            qsizetype offset = 2;
            if (size == 126) {
                if (buffer.size() < 4)
                    return;
                size = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer[2])) << 8) |
                       static_cast<std::uint8_t>(buffer[3]);
                offset = 4;
            } else if (size == 127) {
                if (buffer.size() < 10)
                    return;
                size = 0;
                for (int i = 0; i < 8; ++i)
                    size = (size << 8) | static_cast<std::uint8_t>(buffer[2 + i]);
                offset = 10;
            }
            if (size > static_cast<std::uint64_t>(max_message)) {
                closeWith(connection, 1009);
                return;
            }
            if (buffer.size() < offset + 4 + static_cast<qsizetype>(size))
                return;
            const auto mask = buffer.mid(offset, 4);
            auto payload = buffer.mid(offset + 4, static_cast<qsizetype>(size));
            for (qsizetype i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
            buffer.remove(0, offset + 4 + static_cast<qsizetype>(size));

            if (opcode == 0x8) {
                closeWith(connection, 1000);
                return;
            }
            if (opcode == 0x9) {
                sendFrame(connection->socket, 0xA, payload);
                continue;
            }
            if (opcode == 0xA)
                continue;
            if (opcode == 0x1 || opcode == 0x2) {
                connection->fragments = payload;
                connection->in_message = !fin;
                connection->text_message = opcode == 0x1;
                if (fin)
                    message(connection, connection->text_message, connection->fragments);
                continue;
            }
            if (opcode == 0x0 && connection->in_message) {
                connection->fragments += payload;
                if (connection->fragments.size() > max_message) {
                    closeWith(connection, 1009);
                    return;
                }
                if (fin) {
                    connection->in_message = false;
                    message(connection, connection->text_message, connection->fragments);
                }
                continue;
            }
            closeWith(connection, 1002);
            return;
        }
    }

    bool withinRate(Connection* connection) {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (now - connection->window_start >= 1000) {
            connection->window_start = now;
            connection->window_count = 1;
            return true;
        }
        return ++connection->window_count <= max_per_second;
    }

    void message(Connection* connection, bool text, const QByteArray& payload) {
        if (!withinRate(connection) || !text)
            return;
        received.push_back(payload);
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return;
        const auto object = document.object();
        static const QRegularExpression ciphertext(QStringLiteral("^[A-Za-z0-9+/_-]+={0,2}$"));
        if (object.size() != 2 || !object.contains(QStringLiteral("to")) ||
            !object.value(QStringLiteral("data")).isString() ||
            !ciphertext.match(object.value(QStringLiteral("data")).toString()).hasMatch())
            return;
        const auto data = object.value(QStringLiteral("data")).toString();
        auto& room = rooms_[connection->room];
        const auto to = object.value(QStringLiteral("to"));
        if (connection->role == Role::Guest) {
            if (to != QJsonValue(QStringLiteral("host")))
                return;
            sendText(room.host,
                     QJsonDocument(QJsonObject{{QStringLiteral("from"), static_cast<qint64>(connection->peer)},
                                               {QStringLiteral("data"), data}})
                         .toJson(QJsonDocument::Compact));
            return;
        }
        const auto peer = to.toDouble(-1);
        if (!to.isDouble() || peer < 0 || peer > 4294967295.0 ||
            peer != static_cast<double>(static_cast<std::uint64_t>(peer)))
            return;
        const auto found = room.guests.find(static_cast<std::uint32_t>(peer));
        if (found == room.guests.end())
            return;
        sendText(found->second, QJsonDocument(QJsonObject{{QStringLiteral("from"), QStringLiteral("host")},
                                                          {QStringLiteral("data"), data}})
                                    .toJson(QJsonDocument::Compact));
    }

    void leave(Connection* connection) {
        if (connection->gone)
            return;
        connection->gone = true;
        if (connection->role != Role::Pending) {
            auto& room = rooms_[connection->room];
            if (connection->role == Role::Guest) {
                room.guests.erase(connection->peer);
                sendText(room.host,
                         QJsonDocument(QJsonObject{{QStringLiteral("event"), QStringLiteral("peer-left")},
                                                   {QStringLiteral("peer"), static_cast<qint64>(connection->peer)}})
                             .toJson(QJsonDocument::Compact));
            } else if (room.host == connection->socket) {
                room.host = nullptr;
                for (const auto& [peer, guest] : room.guests)
                    sendText(guest, R"({"event":"host-offline"})");
            }
        }
        connection->socket->deleteLater();
    }

    void drop(Connection* connection) {
        if (!connection->gone)
            connection->socket->abort();
        leave(connection);
    }

    quint16 port_{};
    std::map<QString, Room> rooms_;
    std::vector<std::unique_ptr<Connection>> connections_; // sockets are children of server_
    QTcpServer server_;

  public:
    LoopbackSignaling(const LoopbackSignaling&) = delete;
    LoopbackSignaling& operator=(const LoopbackSignaling&) = delete;
};

} // namespace music_player::m0::testing
