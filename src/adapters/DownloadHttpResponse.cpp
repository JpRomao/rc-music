#include "DownloadHttpResponse.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0 {

namespace {

[[nodiscard]] bool isToken(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || QByteArrayView("!#$%&'*+-.^_`|~").contains(character);
}

[[nodiscard]] bool isFieldValue(QByteArrayView text) {
    return std::all_of(text.begin(), text.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte == '\t' || (byte >= 32 && byte != 127);
    });
}

[[nodiscard]] std::optional<quint64> decimal(QByteArrayView text) {
    if (text.empty())
        return {};
    quint64 value = 0;
    for (char character : text) {
        if (character < '0' || character > '9')
            return {};
        const auto digit = static_cast<quint64>(character - '0');
        if (value > (std::numeric_limits<quint64>::max() - digit) / 10)
            return {};
        value = value * 10 + digit;
    }
    return value;
}

[[nodiscard]] int hexDigit(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

void skipWhitespace(QByteArrayView text, qsizetype& offset) {
    while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t'))
        ++offset;
}

[[nodiscard]] bool chunkExtensions(QByteArrayView text, qsizetype offset) {
    while (offset < text.size()) {
        skipWhitespace(text, offset);
        if (offset == text.size() || text[offset++] != ';')
            return false;
        skipWhitespace(text, offset);
        const auto nameStart = offset;
        while (offset < text.size() && isToken(text[offset]))
            ++offset;
        if (offset == nameStart)
            return false;
        skipWhitespace(text, offset);
        if (offset == text.size())
            return true;
        if (text[offset] != '=')
            continue;
        ++offset;
        skipWhitespace(text, offset);
        if (offset == text.size())
            return false;
        if (text[offset] != '"') {
            const auto valueStart = offset;
            while (offset < text.size() && isToken(text[offset]))
                ++offset;
            if (offset == valueStart)
                return false;
            continue;
        }
        ++offset;
        bool closed = false;
        while (offset < text.size()) {
            const char character = text[offset++];
            if (character == '"') {
                closed = true;
                break;
            }
            if (character == '\\') {
                if (offset == text.size() || !isFieldValue(text.sliced(offset, 1)))
                    return false;
                ++offset;
            } else if (!isFieldValue(QByteArrayView(&character, 1))) {
                return false;
            }
        }
        if (!closed)
            return false;
    }
    return true;
}

[[nodiscard]] std::optional<quint64> chunkSize(QByteArrayView text) {
    quint64 value = 0;
    qsizetype offset = 0;
    while (offset < text.size()) {
        const int digit = hexDigit(text[offset]);
        if (digit < 0)
            break;
        if (value > (std::numeric_limits<quint64>::max() - static_cast<quint64>(digit)) / 16)
            return {};
        value = value * 16 + static_cast<quint64>(digit);
        ++offset;
    }
    if (offset == 0 || !chunkExtensions(text, offset))
        return {};
    return value;
}

[[nodiscard]] bool protectedHeader(QByteArrayView name) {
    return name == "content-length" || name == "transfer-encoding" || name == "content-encoding" ||
           name == "content-type" || name == "content-range" || name == "location" || name == "connection" ||
           name == "upgrade" || name == "trailer" || name == "host" || name == "authorization" ||
           name == "proxy-authorization" || name == "www-authenticate" || name == "proxy-authenticate" ||
           name == "set-cookie";
}

} // namespace

DownloadHttpResponse::DownloadHttpResponse(quint64 maxBodyBytes) : max_body_bytes_(maxBodyBytes) {}

DownloadHttpUpdate DownloadHttpResponse::feed(QByteArrayView bytes) {
    QByteArray body;
    while (!bytes.empty() && state_ != State::Failed) {
        if (state_ == State::Done) {
            if (status_code_ == 200)
                reject();
            break;
        }
        if (state_ == State::FixedBody || state_ == State::ChunkBody || state_ == State::CloseBody) {
            const auto count = state_ == State::CloseBody ? static_cast<quint64>(bytes.size())
                                                          : std::min(remaining_, static_cast<quint64>(bytes.size()));
            if (count > max_body_bytes_ - body_bytes_) {
                reject(DownloadHttpError::LimitExceeded);
                break;
            }
            const auto size = static_cast<qsizetype>(count);
            body.append(bytes.data(), size);
            bytes = bytes.sliced(size);
            body_bytes_ += count;
            if (state_ == State::CloseBody)
                continue;
            remaining_ -= count;
            if (remaining_ == 0)
                state_ = state_ == State::FixedBody ? State::Done : State::ChunkCrlf;
            continue;
        }
        const char character = bytes.front();
        bytes = bytes.sliced(1);
        if (state_ == State::ChunkCrlf || state_ == State::ChunkSize) {
            if (++chunk_framing_bytes_ > download_http_max_chunk_framing_bytes) {
                reject(DownloadHttpError::LimitExceeded);
                break;
            }
        } else if (++header_bytes_ > download_http_max_header_bytes) {
            reject(DownloadHttpError::LimitExceeded);
            break;
        }
        if (state_ == State::ChunkCrlf) {
            if (character != (chunk_crlf_bytes_ == 0 ? '\r' : '\n')) {
                reject();
                break;
            }
            if (++chunk_crlf_bytes_ == 2) {
                chunk_crlf_bytes_ = 0;
                state_ = State::ChunkSize;
            }
            continue;
        }
        if ((!line_.isEmpty() && line_.back() == '\r' && character != '\n') ||
            (character == '\n' && (line_.isEmpty() || line_.back() != '\r'))) {
            reject();
            break;
        }
        line_.append(character);
        if (state_ == State::ChunkSize && line_.size() > download_http_max_chunk_line_bytes) {
            reject(DownloadHttpError::LimitExceeded);
            break;
        }
        if (character == '\n') {
            line_.chop(2);
            const QByteArray complete = std::move(line_);
            line_.clear();
            consumeLine(complete);
        }
    }
    return update(std::move(body));
}

DownloadHttpUpdate DownloadHttpResponse::finishEof() {
    if (state_ == State::CloseBody)
        state_ = State::Done;
    else if (state_ != State::Done)
        reject();
    return update();
}

int DownloadHttpResponse::statusCode() const { return metadata_available_ ? status_code_ : 0; }

QByteArray DownloadHttpResponse::header(QByteArrayView name) const {
    return metadata_available_ ? headers_.value(QByteArray(name.data(), name.size()).toLower()) : QByteArray{};
}

std::optional<quint64> DownloadHttpResponse::contentLength() const {
    return metadata_available_ ? content_length_ : std::optional<quint64>{};
}

bool DownloadHttpResponse::failed() const { return state_ == State::Failed; }

bool DownloadHttpResponse::finished() const { return state_ == State::Done; }

DownloadHttpError DownloadHttpResponse::error() const { return error_; }

DownloadHttpUpdate DownloadHttpResponse::update(QByteArray body) const {
    if (failed())
        body.clear();
    return {std::move(body), metadata_available_, finished(), failed()};
}

void DownloadHttpResponse::reject(DownloadHttpError error) {
    if (state_ == State::Failed)
        return;
    state_ = State::Failed;
    error_ = error;
    line_.clear();
}

void DownloadHttpResponse::consumeLine(const QByteArray& line) {
    if (state_ == State::Status) {
        if (line.size() < 13 || (!line.startsWith("HTTP/1.1 ") && !line.startsWith("HTTP/1.0 ")) || line[12] != ' ' ||
            !isFieldValue(QByteArrayView(line).sliced(13))) {
            reject();
            return;
        }
        const auto status = decimal(QByteArrayView(line).sliced(9, 3));
        if (!status || *status < 100 || *status > 599 || *status == 101) {
            reject();
            return;
        }
        status_code_ = static_cast<int>(*status);
        http_1_0_ = line.startsWith("HTTP/1.0 ");
        headers_.clear();
        content_length_.reset();
        state_ = State::Headers;
    } else if (state_ == State::Headers) {
        if (line.isEmpty())
            completeHeaders();
        else
            consumeHeader(line, false);
    } else if (state_ == State::ChunkSize) {
        const auto size = chunkSize(line);
        if (!size) {
            reject();
            return;
        }
        if (*size > max_body_bytes_ - body_bytes_) {
            reject(DownloadHttpError::LimitExceeded);
            return;
        }
        remaining_ = *size;
        state_ = remaining_ == 0 ? State::Trailers : State::ChunkBody;
    } else if (state_ == State::Trailers) {
        if (line.isEmpty())
            state_ = State::Done;
        else
            consumeHeader(line, true);
    }
}

void DownloadHttpResponse::consumeHeader(const QByteArray& line, bool trailer) {
    const auto separator = line.indexOf(':');
    if (++header_fields_ > download_http_max_header_fields) {
        reject(DownloadHttpError::LimitExceeded);
        return;
    }
    if (separator <= 0 || !std::all_of(line.begin(), line.begin() + separator, isToken) ||
        !isFieldValue(QByteArrayView(line).sliced(separator + 1))) {
        reject();
        return;
    }
    const auto name = line.first(separator).toLower();
    const auto value = line.sliced(separator + 1).trimmed();
    if (trailer) {
        if (protectedHeader(name))
            reject();
        return;
    }
    if (headers_.contains(name)) {
        if (name == "content-length" || name == "transfer-encoding" || name == "content-encoding" ||
            name == "content-type" || name == "content-range" || name == "location") {
            reject();
            return;
        }
        headers_[name] += ", " + value;
    } else {
        headers_.insert(name, value);
    }
}

void DownloadHttpResponse::completeHeaders() {
    const bool hasLength = headers_.contains("content-length");
    const bool hasTransfer = headers_.contains("transfer-encoding");
    if (headers_.contains("upgrade") || (hasTransfer && hasLength) || (http_1_0_ && hasTransfer) ||
        (hasTransfer && headers_.value("transfer-encoding").toLower() != "chunked")) {
        reject();
        return;
    }
    for (const auto& option : headers_.value("connection").split(',')) {
        if (option.trimmed().toLower() == "upgrade") {
            reject();
            return;
        }
    }
    if (hasLength) {
        content_length_ = decimal(headers_.value("content-length"));
        if (!content_length_) {
            reject();
            return;
        }
    }
    if (status_code_ < 200) {
        if (hasLength || hasTransfer) {
            reject();
            return;
        }
        if (++informational_count_ > 4) {
            reject(DownloadHttpError::LimitExceeded);
            return;
        }
        headers_.clear();
        status_code_ = 0;
        state_ = State::Status;
        return;
    }
    if (status_code_ != 200) {
        metadata_available_ = true;
        state_ = State::Done;
        return;
    }
    if (headers_.contains("content-encoding") && headers_.value("content-encoding").toLower() != "identity") {
        reject();
        return;
    }
    if (content_length_ && *content_length_ > max_body_bytes_) {
        reject(DownloadHttpError::LimitExceeded);
        return;
    }
    metadata_available_ = true;
    if (hasTransfer) {
        state_ = State::ChunkSize;
    } else if (content_length_) {
        remaining_ = *content_length_;
        state_ = remaining_ == 0 ? State::Done : State::FixedBody;
    } else {
        state_ = State::CloseBody;
    }
}

} // namespace music_player::m0
