#include "DownloadWorkerProtocol.hpp"
#include "DownloadUserAgent.hpp"

#include <QtEndian>

#include <algorithm>

namespace music_player::m0 {
namespace {
bool pin(const QByteArray& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
                                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool path(const QString& value) {
    return value.size() >= 4 && value.size() <= 30000 && value.at(0).isLetter() && value.at(0).unicode() < 128 &&
           value.at(1) == ':' && (value.at(2) == '/' || value.at(2) == '\\') && !value.contains(QChar{u'\0'}) &&
           QString::fromUtf8(value.toUtf8()) == value;
}
bool host(const QString& value) {
    if (value.isEmpty() || value.size() > 254)
        return false;
    return std::all_of(value.begin(), value.end(), [](QChar c) {
        const auto n = c.unicode();
        return (n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || (n >= '0' && n <= '9') || n == '-' || n == '.' ||
               n == ':';
    });
}
bool target(const QByteArray& value) {
    if (value.isEmpty() || value.size() > 8192 || value.front() != '/')
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c > 0x20 && c < 0x7f && c != '\\' && c != '#' && c != '"' && c != '<' && c != '>';
    });
}
bool address(const QHostAddress& value) {
    return (value.protocol() == QAbstractSocket::IPv4Protocol || value.protocol() == QAbstractSocket::IPv6Protocol) &&
           value.scopeId().isEmpty();
}
bool valid(const DownloadWorkerRequest& value) {
    const auto& runtime = value.runtime;
    if (!path(runtime.core_path) || !pin(runtime.core_sha256) || !path(runtime.network_path) ||
        !pin(runtime.network_sha256) || !path(runtime.plugin_path) || !pin(runtime.plugin_sha256) ||
        !validDownloadUserAgent(value.endpoint.user_agent))
        return false;
    if (value.mode == DownloadWorkerMode::Resolve)
        return host(value.host) && value.endpoint.user_agent.isEmpty();
    return value.mode == DownloadWorkerMode::Stream && address(value.endpoint.address) && host(value.endpoint.host) &&
           target(value.endpoint.request_target) && value.endpoint.port != 0;
}
void put(QByteArray& result, const QByteArray& value) {
    char length[4];
    qToLittleEndian<quint32>(static_cast<quint32>(value.size()), length);
    result.append(length, 4);
    result += value;
}
class Reader final {
  public:
    explicit Reader(QByteArrayView bytes) : bytes_(bytes) {}
    QByteArray field(qsizetype maximum) {
        if (bytes_.size() < 4) {
            failed_ = true;
            return {};
        }
        const auto length = qFromLittleEndian<quint32>(bytes_.data());
        bytes_ = bytes_.sliced(4);
        if (length > maximum || length > bytes_.size()) {
            failed_ = true;
            return {};
        }
        const auto result = bytes_.first(length).toByteArray();
        bytes_ = bytes_.sliced(length);
        return result;
    }
    QString text(qsizetype maximum) {
        const auto bytes = field(maximum);
        const auto result = QString::fromUtf8(bytes);
        if (result.toUtf8() != bytes)
            failed_ = true;
        return result;
    }
    bool done() const { return !failed_ && bytes_.isEmpty(); }
    QByteArrayView remaining() const { return bytes_; }
    void consume(qsizetype size) { bytes_ = bytes_.sliced(size); }

  private:
    QByteArrayView bytes_;
    bool failed_{false};
};
} // namespace

QByteArray encodeDownloadWorkerRequest(const DownloadWorkerRequest& request) {
    if (!valid(request))
        return {};
    QByteArray bytes("RCMD", 4);
    bytes += request.endpoint.user_agent.isEmpty() ? char{1} : char{2};
    bytes += static_cast<char>(request.mode);
    for (const auto& field :
         {request.runtime.core_path.toUtf8(), request.runtime.core_sha256, request.runtime.network_path.toUtf8(),
          request.runtime.network_sha256, request.runtime.plugin_path.toUtf8(), request.runtime.plugin_sha256})
        put(bytes, field);
    if (request.mode == DownloadWorkerMode::Resolve)
        put(bytes, request.host.toLatin1());
    else {
        put(bytes, request.endpoint.address.toString().toLatin1());
        put(bytes, request.endpoint.host.toLatin1());
        put(bytes, request.endpoint.request_target);
        if (!request.endpoint.user_agent.isEmpty())
            put(bytes, request.endpoint.user_agent);
        char port[2];
        qToLittleEndian<quint16>(request.endpoint.port, port);
        bytes.append(port, 2);
        bytes += request.endpoint.tls ? char{1} : char{0};
    }
    return bytes.size() <= download_worker_max_request_bytes ? bytes : QByteArray{};
}
std::optional<DownloadWorkerRequest> decodeDownloadWorkerRequest(const QByteArray& bytes) {
    if (bytes.size() > download_worker_max_request_bytes || bytes.size() < 6 ||
        bytes.first(4) != QByteArray("RCMD", 4) || (bytes.at(4) != char{1} && bytes.at(4) != char{2}))
        return std::nullopt;
    DownloadWorkerRequest value;
    const bool with_user_agent = bytes.at(4) == char{2};
    value.mode = static_cast<DownloadWorkerMode>(static_cast<quint8>(bytes.at(5)));
    if (with_user_agent && value.mode != DownloadWorkerMode::Stream)
        return std::nullopt;
    Reader reader(QByteArrayView{bytes}.sliced(6));
    value.runtime.core_path = reader.text(120000);
    value.runtime.core_sha256 = reader.field(64);
    value.runtime.network_path = reader.text(120000);
    value.runtime.network_sha256 = reader.field(64);
    value.runtime.plugin_path = reader.text(120000);
    value.runtime.plugin_sha256 = reader.field(64);
    if (value.mode == DownloadWorkerMode::Resolve)
        value.host = reader.text(254);
    else if (value.mode == DownloadWorkerMode::Stream) {
        value.endpoint.address = QHostAddress(reader.text(45));
        value.endpoint.host = reader.text(254);
        value.endpoint.request_target = reader.field(8192);
        if (with_user_agent) {
            value.endpoint.user_agent = reader.field(download_max_user_agent_bytes);
            if (value.endpoint.user_agent.isEmpty())
                return std::nullopt;
        }
        const auto tail = reader.remaining();
        if (tail.size() != 3 || (tail.at(2) != char{0} && tail.at(2) != char{1}))
            return std::nullopt;
        value.endpoint.port = qFromLittleEndian<quint16>(tail.data());
        value.endpoint.tls = tail.at(2) == char{1};
        reader.consume(3);
    } else
        return std::nullopt;
    return reader.done() && valid(value) ? std::optional{value} : std::nullopt;
}
QByteArray encodeDownloadWorkerFrame(const DownloadWorkerFrame& frame) {
    QByteArray body(1, static_cast<char>(frame.kind));
    if (frame.kind == DownloadWorkerFrameKind::Addresses) {
        if (frame.addresses.isEmpty() || frame.addresses.size() > 32 || !frame.data.isEmpty() ||
            frame.error != DownloadWorkerError::None || frame.authenticated_eof)
            return {};
        body += static_cast<char>(frame.addresses.size());
        for (const auto& value : frame.addresses) {
            if (!address(value))
                return {};
            const auto text = value.toString().toLatin1();
            body += static_cast<char>(text.size());
            body += text;
        }
    } else if (frame.kind == DownloadWorkerFrameKind::Data) {
        if (frame.data.isEmpty() || frame.data.size() > download_worker_max_data_bytes || !frame.addresses.isEmpty() ||
            frame.error != DownloadWorkerError::None || frame.authenticated_eof)
            return {};
        body += frame.data;
    } else if (frame.kind == DownloadWorkerFrameKind::Terminal) {
        if (static_cast<quint8>(frame.error) > static_cast<quint8>(DownloadWorkerError::Tls) || !frame.data.isEmpty() ||
            !frame.addresses.isEmpty() || (frame.error != DownloadWorkerError::None && frame.authenticated_eof))
            return {};
        body += static_cast<char>(frame.error);
        body += frame.authenticated_eof ? char{1} : char{0};
    } else
        return {};
    QByteArray packet(4, '\0');
    qToLittleEndian<quint32>(static_cast<quint32>(body.size()), packet.data());
    return packet + body;
}
std::optional<DownloadWorkerFrame> decodeDownloadWorkerFrame(const QByteArray& body) {
    if (body.isEmpty() || body.size() > download_worker_max_frame_bytes)
        return std::nullopt;
    DownloadWorkerFrame value;
    value.kind = static_cast<DownloadWorkerFrameKind>(static_cast<quint8>(body.front()));
    if (value.kind == DownloadWorkerFrameKind::Addresses) {
        if (body.size() < 2)
            return std::nullopt;
        const auto count = static_cast<quint8>(body.at(1));
        if (count == 0 || count > 32)
            return std::nullopt;
        qsizetype position = 2;
        for (quint8 index = 0; index < count; ++index) {
            if (position >= body.size())
                return std::nullopt;
            const auto length = static_cast<quint8>(body.at(position++));
            if (length == 0 || length > 45 || length > body.size() - position)
                return std::nullopt;
            const auto text = body.mid(position, length);
            const QHostAddress parsed(QString::fromLatin1(text));
            if (!address(parsed) || text.contains('\0'))
                return std::nullopt;
            value.addresses.push_back(parsed);
            position += length;
        }
        if (position != body.size())
            return std::nullopt;
    } else if (value.kind == DownloadWorkerFrameKind::Data) {
        if (body.size() == 1)
            return std::nullopt;
        value.data = body.mid(1);
    } else if (value.kind == DownloadWorkerFrameKind::Terminal) {
        if (body.size() != 3 || static_cast<quint8>(body.at(1)) > static_cast<quint8>(DownloadWorkerError::Tls) ||
            (body.at(2) != char{0} && body.at(2) != char{1}))
            return std::nullopt;
        value.error = static_cast<DownloadWorkerError>(static_cast<quint8>(body.at(1)));
        value.authenticated_eof = body.at(2) == char{1};
        if (value.error != DownloadWorkerError::None && value.authenticated_eof)
            return std::nullopt;
    } else
        return std::nullopt;
    return value;
}
qsizetype DownloadWorkerFrameDecoder::append(QByteArrayView bytes) {
    if (failed_ || ready())
        return 0;
    qsizetype consumed = 0;
    if (length_.size() < 4) {
        const auto count = std::min<qsizetype>(4 - length_.size(), bytes.size());
        length_.append(bytes.data(), count);
        bytes = bytes.sliced(count);
        consumed += count;
        if (length_.size() < 4)
            return consumed;
        expected_ = qFromLittleEndian<quint32>(length_.constData());
        if (expected_ == 0 || expected_ > download_worker_max_frame_bytes) {
            failed_ = true;
            return consumed;
        }
    }
    const auto count = std::min<qsizetype>(expected_ - body_.size(), bytes.size());
    body_.append(bytes.data(), count);
    return consumed + count;
}
bool DownloadWorkerFrameDecoder::ready() const noexcept {
    return !failed_ && expected_ != 0 && body_.size() == expected_;
}
bool DownloadWorkerFrameDecoder::failed() const noexcept { return failed_; }
std::optional<DownloadWorkerFrame> DownloadWorkerFrameDecoder::take() {
    if (!ready())
        return std::nullopt;
    auto result = decodeDownloadWorkerFrame(body_);
    if (!result) {
        failed_ = true;
        return std::nullopt;
    }
    length_.clear();
    body_.clear();
    expected_ = 0;
    return result;
}
bool DownloadWorkerFrameDecoder::incomplete() const noexcept {
    return !failed_ && !ready() && (!length_.isEmpty() || !body_.isEmpty());
}
} // namespace music_player::m0
