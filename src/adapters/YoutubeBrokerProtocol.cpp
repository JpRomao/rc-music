#include "YoutubeBrokerProtocol.hpp"
#include "StrictJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {
using Failure = DownloadFailure;
bool keys(const QJsonObject& object, std::initializer_list<const char*> expected) {
    return object.size() == static_cast<qsizetype>(expected.size()) &&
           std::all_of(expected.begin(), expected.end(),
                       [&object](const char* name) { return object.contains(QLatin1String(name)); });
}
Failure text(const QString& value, const qsizetype maximum, const bool nonblank = true) {
    if (value.size() > maximum)
        return Failure::LimitExceeded;
    const auto bytes = value.toUtf8();
    if (QString::fromUtf8(bytes) != value || (nonblank && value.trimmed().isEmpty()))
        return Failure::InvalidResult;
    return bytes.size() > maximum ? Failure::LimitExceeded : Failure::None;
}
Failure validate(const YoutubeBrokerChallenge& challenge) {
    auto status = text(challenge.player, youtube_broker_max_player_bytes);
    if (status != Failure::None)
        return status;
    if (challenge.requests.empty())
        return Failure::InvalidResult;
    if (challenge.requests.size() > youtube_broker_max_requests)
        return Failure::LimitExceeded;
    qsizetype total = 0;
    for (const auto& request : challenge.requests) {
        if (request.type != QLatin1String("n") && request.type != QLatin1String("sig"))
            return Failure::InvalidResult;
        if (request.challenges.isEmpty())
            return Failure::InvalidResult;
        if (request.challenges.size() > youtube_broker_max_challenges)
            return Failure::LimitExceeded;
        for (const auto& value : request.challenges) {
            status = text(value, youtube_broker_max_value_bytes);
            if (status != Failure::None)
                return status;
            const auto bytes = value.toUtf8().size();
            if (bytes > youtube_broker_max_response_bytes - total)
                return Failure::LimitExceeded;
            total += bytes;
        }
    }
    return Failure::None;
}
QJsonArray requests(const YoutubeBrokerChallenge& challenge) {
    QJsonArray result;
    for (const auto& request : challenge.requests)
        result.push_back(
            QJsonObject{{"type", request.type}, {"challenges", QJsonArray::fromStringList(request.challenges)}});
    return result;
}
std::optional<QJsonObject> object(const QByteArray& bytes, const qsizetype maximum) {
    if (bytes.isEmpty() || bytes.size() > maximum || !detail::strictJsonPreflight(bytes, 8))
        return {};
    const auto document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? std::optional{document.object()} : std::nullopt;
}
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QByteArray frame(const QByteArray& payload) {
    QByteArray result(4, '\0');
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), reinterpret_cast<uchar*>(result.data()));
    result.append(payload);
    return result;
}
bool script(const QByteArray& bytes) {
    if (bytes.isEmpty() || bytes.size() > youtube_broker_max_script_bytes)
        return false;
    const auto decoded = QString::fromUtf8(bytes);
    return !decoded.trimmed().isEmpty() && decoded.toUtf8() == bytes;
}
} // namespace

YoutubeBrokerDecodeResult decodeYoutubeBrokerMessage(QByteArray payload, const quint32 expected_sequence) {
    const auto fail = [](const Failure error = Failure::InvalidResult) {
        return YoutubeBrokerDecodeResult{.error = error};
    };
    if (payload.size() > youtube_broker_max_frame_bytes)
        return fail(Failure::LimitExceeded);
    if (expected_sequence == 0 || expected_sequence > youtube_broker_max_groups + 1)
        return fail();
    const auto root = object(payload, youtube_broker_max_frame_bytes);
    if (!root || !root->value("v").isDouble() || root->value("v").toDouble() != 1 || !root->value("seq").isDouble() ||
        root->value("seq").toDouble() != expected_sequence || !root->value("kind").isString())
        return fail();
    YoutubeBrokerMessage message;
    message.sequence = expected_sequence;
    const auto kind = root->value("kind").toString();
    if (kind == QLatin1String("challenge")) {
        if (!keys(*root, {"v", "kind", "seq", "player", "requests"}) || !root->value("player").isString() ||
            !root->value("requests").isArray())
            return fail();
        if (expected_sequence > youtube_broker_max_groups)
            return fail(Failure::LimitExceeded);
        const auto entries = root->value("requests").toArray();
        if (entries.size() > static_cast<qsizetype>(youtube_broker_max_requests))
            return fail(Failure::LimitExceeded);
        YoutubeBrokerChallenge challenge;
        challenge.player = root->value("player").toString();
        for (const auto& entry : entries) {
            if (!entry.isObject())
                return fail();
            const auto item = entry.toObject();
            if (!keys(item, {"type", "challenges"}) || !item.value("type").isString() ||
                !item.value("challenges").isArray())
                return fail();
            const auto values = item.value("challenges").toArray();
            if (values.size() > youtube_broker_max_challenges)
                return fail(Failure::LimitExceeded);
            YoutubeBrokerRequest request;
            request.type = item.value("type").toString();
            for (const auto& value : values) {
                if (!value.isString())
                    return fail();
                request.challenges.push_back(value.toString());
            }
            challenge.requests.push_back(std::move(request));
        }
        const auto status = validate(challenge);
        if (status != Failure::None)
            return fail(status);
        message.kind = YoutubeBrokerMessageKind::Challenge;
        message.challenge = std::move(challenge);
    } else if (kind == QLatin1String("result")) {
        if (!keys(*root, {"v", "kind", "seq", "data"}) || !root->value("data").isObject())
            return fail();
        message.result_json = json(root->value("data").toObject());
        if (message.result_json.size() > youtube_broker_max_response_bytes)
            return fail(Failure::LimitExceeded);
        message.kind = YoutubeBrokerMessageKind::Result;
    } else if (kind == QLatin1String("failure")) {
        if (!keys(*root, {"v", "kind", "seq", "code"}) || !root->value("code").isString())
            return fail();
        const auto code = root->value("code").toString();
        if (code == QLatin1String("unavailable"))
            message.failure = Failure::Unavailable;
        else if (code == QLatin1String("restricted"))
            message.failure = Failure::Restricted;
        else if (code == QLatin1String("unsupported"))
            message.failure = Failure::UnsupportedMedia;
        else
            return fail();
        message.kind = YoutubeBrokerMessageKind::Failure;
    } else {
        return fail();
    }
    return {.error = Failure::None, .message = std::move(message)};
}

std::optional<QByteArray> encodeYoutubeBrokerResponse(const quint32 sequence, const YoutubeBrokerChallenge& challenge,
                                                      QByteArray node_output) {
    if (sequence == 0 || sequence > youtube_broker_max_groups || validate(challenge) != Failure::None)
        return {};
    const auto root = object(node_output, youtube_broker_max_response_bytes);
    if (!root || !keys(*root, {"type", "responses"}) || root->value("type") != QJsonValue("result") ||
        !root->value("responses").isArray())
        return {};
    const auto responses = root->value("responses").toArray();
    if (responses.size() != static_cast<qsizetype>(challenge.requests.size()))
        return {};
    QJsonArray normalized;
    for (qsizetype index = 0; index < responses.size(); ++index) {
        if (!responses[index].isObject())
            return {};
        const auto response = responses[index].toObject();
        if (response.value("type") == QJsonValue("error")) {
            if (!keys(response, {"type", "error"}) || !response.value("error").isString() ||
                text(response.value("error").toString(), youtube_broker_max_value_bytes, false) != Failure::None)
                return {};
            normalized.push_back(QJsonObject{{"type", "error"}, {"code", "unavailable"}});
            continue;
        }
        if (!keys(response, {"type", "data"}) || response.value("type") != QJsonValue("result") ||
            !response.value("data").isObject())
            return {};
        const auto& inputs = challenge.requests[static_cast<std::size_t>(index)].challenges;
        const QSet<QString> expected(inputs.begin(), inputs.end());
        const auto data = response.value("data").toObject();
        if (data.size() != expected.size())
            return {};
        for (auto value = data.begin(); value != data.end(); ++value)
            if (!expected.contains(value.key()) || !value.value().isString() ||
                text(value.value().toString(), youtube_broker_max_value_bytes) != Failure::None)
                return {};
        normalized.push_back(QJsonObject{{"type", "result"}, {"data", data}});
    }
    const auto payload =
        json({{"v", 1}, {"kind", "response"}, {"seq", static_cast<qint64>(sequence)}, {"responses", normalized}});
    return payload.size() <= youtube_broker_max_response_bytes ? std::optional{frame(payload)} : std::nullopt;
}

std::optional<QByteArray> youtubeBrokerNodeInput(const YoutubeBrokerChallenge& challenge, QByteArray ejs_core,
                                                 QByteArray ejs_library) {
    if (validate(challenge) != Failure::None || !script(ejs_core) || !script(ejs_library))
        return {};
    const auto input = json({{"type", "player"},
                             {"player", challenge.player},
                             {"requests", requests(challenge)},
                             {"output_preprocessed", false}});
    const QByteArray assignment{"\nObject.assign(globalThis, lib);\n"};
    const QByteArray call{"\nprocess.stdout.write(JSON.stringify(jsc("}, ending{")));\n"};
    const auto total =
        ejs_library.size() + assignment.size() + ejs_core.size() + call.size() + input.size() + ending.size();
    if (total > youtube_broker_max_frame_bytes)
        return {};
    QByteArray result;
    result.reserve(total);
    result.append(ejs_library).append(assignment).append(ejs_core).append(call).append(input).append(ending);
    return result;
}

bool YoutubeBrokerFrameReader::append(QByteArrayView bytes) {
    const auto fail = [this] {
        failed_ = true;
        payload_ = {};
        return false;
    };
    if (failed_ || sealed_)
        return fail();
    if (header_size_ < 4) {
        const auto needed = std::min<qsizetype>(4 - header_size_, bytes.size());
        std::copy_n(bytes.data(), needed, header_.data() + header_size_);
        header_size_ += needed;
        bytes = bytes.sliced(needed);
        if (header_size_ != 4)
            return true;
        expected_bytes_ = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header_.data()));
        if (expected_bytes_ == 0 || expected_bytes_ > static_cast<quint32>(youtube_broker_max_frame_bytes))
            return fail();
        payload_.reserve(static_cast<qsizetype>(expected_bytes_));
    }
    if (bytes.size() > static_cast<qsizetype>(expected_bytes_) - payload_.size())
        return fail();
    payload_.append(bytes.data(), bytes.size());
    return true;
}
std::optional<QByteArray> YoutubeBrokerFrameReader::take() {
    if (failed_ || header_size_ != 4 || payload_.size() != static_cast<qsizetype>(expected_bytes_))
        return {};
    auto result = std::move(payload_);
    payload_ = {};
    header_size_ = 0;
    expected_bytes_ = 0;
    return result;
}
bool YoutubeBrokerFrameReader::finish() {
    sealed_ = true;
    if (failed_ ||
        (header_size_ != 0 && (header_size_ != 4 || payload_.size() != static_cast<qsizetype>(expected_bytes_)))) {
        failed_ = true;
        payload_ = {};
        return false;
    }
    return true;
}
bool YoutubeBrokerFrameReader::failed() const noexcept { return failed_; }
} // namespace music_player::m0
