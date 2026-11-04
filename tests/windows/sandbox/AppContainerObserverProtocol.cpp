#include "AppContainerObserverProtocol.hpp"
#include "adapters/DownloadCheckpointJson.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Codec = AppContainerObserverProtocol;
using Observer = AppContainerNetworkObserver;
constexpr std::array<std::string_view, 5> states{"idle", "observing", "stopped", "unavailable", "stop-unconfirmed"};
constexpr std::array<std::string_view, 19> reasons{"none",
                                                   "invalid-configuration",
                                                   "missing-call",
                                                   "app-id",
                                                   "engine",
                                                   "collection-query",
                                                   "collection-type",
                                                   "collection-disabled",
                                                   "keyword-query",
                                                   "keyword-type",
                                                   "keyword-disabled",
                                                   "subscribe",
                                                   "unsubscribe",
                                                   "close",
                                                   "filter-query",
                                                   "layer-query",
                                                   "isolation-rule",
                                                   "exception",
                                                   "window-clock"};
constexpr std::array<std::string_view, 2> evidence_kinds{"capability-drop", "classify-isolation"};
constexpr std::array<std::string_view, 3> providers{"unknown", "app-isolation", "windows-firewall"};
constexpr std::array<std::string_view, 3> sublayers{"unknown", "app-isolation", "windows-firewall"};
constexpr std::array<std::string_view, 3> layers{"unknown", "ale-auth-connect-v4", "ale-auth-connect-v6"};
constexpr std::array<std::string_view, 4> actions{"unknown", "block", "permit", "callout"};
constexpr std::array<std::string_view, 2> statuses{"admission-rejected", "observer-result"};

QString latin(const std::string_view value) {
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}
bool keys(const QJsonObject& object, const std::initializer_list<const char*> fields) {
    return object.size() == static_cast<qsizetype>(fields.size()) &&
           std::ranges::all_of(fields, [&](const char* field) { return object.contains(QLatin1StringView{field}); });
}
template <typename Enum, std::size_t Size>
QString enumText(const Enum value, const std::array<std::string_view, Size>& names) {
    const auto index = static_cast<std::size_t>(value);
    return index < Size ? latin(names[index]) : QString{};
}
template <typename Enum, std::size_t Size>
std::optional<Enum> enumValue(const QJsonValue& value, const std::array<std::string_view, Size>& names) {
    if (value.isString()) {
        const auto text = value.toString();
        for (std::size_t i = 0; i < Size; ++i)
            if (text == latin(names[i]))
                return static_cast<Enum>(i);
    }
    return std::nullopt;
}
bool lowerHex(const QString& value, const qsizetype length) {
    return value.size() == length && std::ranges::all_of(value, [](const QChar character) {
               const auto unit = character.unicode();
               return (unit >= u'0' && unit <= u'9') || (unit >= u'a' && unit <= u'f');
           });
}
std::optional<QByteArray> rawHex(const QJsonValue& value, const qsizetype byte_count) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (!lowerHex(text, byte_count * 2))
        return std::nullopt;
    return QByteArray::fromHex(text.toLatin1());
}
QString hex(const QByteArray& value) { return QString::fromLatin1(value.toHex()); }
QString hex(const Observer::GuidBytes& value) {
    return hex(QByteArray{reinterpret_cast<const char*>(value.data()), static_cast<qsizetype>(value.size())});
}
std::optional<Observer::GuidBytes> guid(const QJsonValue& value) {
    const auto bytes = rawHex(value, 16);
    if (!bytes)
        return std::nullopt;
    Observer::GuidBytes result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>((*bytes)[static_cast<qsizetype>(i)]);
    return result;
}
bool validSid(const QByteArray& bytes) {
    if (bytes.size() < 12 || bytes.size() > 68 || static_cast<unsigned char>(bytes[0]) != 1)
        return false;
    const auto count = static_cast<unsigned char>(bytes[1]);
    return count >= 1 && count <= 15 && bytes.size() == 8 + count * 4;
}
bool validPackageSid(const QByteArray& bytes) {
    if (!validSid(bytes) || bytes.size() != 40 || static_cast<unsigned char>(bytes[1]) != 8 || bytes[7] != 15 ||
        bytes[8] != 2 || bytes[9] != 0 || bytes[10] != 0 || bytes[11] != 0)
        return false;
    for (qsizetype i = 2; i < 7; ++i)
        if (bytes[i] != 0)
            return false;
    return true;
}
std::optional<QByteArray> sid(const QJsonValue& value, const bool package) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (text.size() < 24 || text.size() > 136 || text.size() % 2 != 0)
        return std::nullopt;
    const auto bytes = rawHex(value, text.size() / 2);
    if (!bytes || (package ? !validPackageSid(*bytes) : !validSid(*bytes)))
        return std::nullopt;
    return bytes;
}
bool absoluteHelperPath(const QString& value) {
    if (value.size() < 3 || value.contains(QChar{0}) || QString::fromUtf8(value.toUtf8()) != value)
        return false;
    const auto letter = value[0].unicode();
    return ((letter >= u'a' && letter <= u'z') || (letter >= u'A' && letter <= u'Z')) && value[1] == u':' &&
           (value[2] == u'/' || value[2] == u'\\');
}
std::optional<std::uint64_t> decimal(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text[0] == u'0'))
        return std::nullopt;
    std::uint64_t result = 0;
    for (const auto character : text) {
        const auto unit = character.unicode();
        if (unit < u'0' || unit > u'9')
            return std::nullopt;
        const auto digit = static_cast<std::uint64_t>(unit - u'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return std::nullopt;
        result = result * 10 + digit;
    }
    return result;
}
QString decimal(const std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
std::optional<std::uint32_t> number(const QJsonValue& value,
                                    const std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max()) {
    if (!value.isDouble())
        return std::nullopt;
    const auto numeric = value.toDouble();
    if (!std::isfinite(numeric) || numeric < 0 || numeric > maximum || std::trunc(numeric) != numeric)
        return std::nullopt;
    return static_cast<std::uint32_t>(numeric);
}
QJsonValue number(const std::uint32_t value) { return QJsonValue{static_cast<qint64>(value)}; }
std::optional<std::uint32_t> errorValue(const QJsonValue& value) {
    return value.isNull() ? std::optional{Observer::not_called} : number(value, Observer::not_called - 1);
}
QJsonValue errorValue(const std::uint32_t value) {
    return value == Observer::not_called ? QJsonValue{QJsonValue::Null} : number(value);
}
std::optional<bool> boolean(const QJsonValue& value) {
    return value.isBool() ? std::optional{value.toBool()} : std::nullopt;
}
bool window(const std::uint64_t begin, const std::uint64_t end) {
    return begin != 0 && end > begin && end - begin == Observer::maximum_window_ticks;
}
bool envelope(const QJsonObject& object, const char* kind) {
    const auto version = number(object.value("schema_version"));
    const auto nonce = object.value("nonce");
    return version && *version == 1 && object.value("owner") == QJsonValue{"rc-music-wfp-observer"} &&
           object.value("kind") == QJsonValue{kind} && nonce.isString() && lowerHex(nonce.toString(), 32);
}
QJsonObject envelope(const char* kind, const QString& nonce) {
    return {{"owner", "rc-music-wfp-observer"}, {"schema_version", 1}, {"kind", kind}, {"nonce", nonce}};
}
std::optional<QJsonObject> frame(const QByteArrayView bytes, const char* kind) {
    if (bytes.isEmpty() || bytes.size() > Codec::maximum_frame_size || !m0::strictDownloadJsonPreflight(bytes))
        return std::nullopt;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray{bytes.data(), bytes.size()}, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !envelope(document.object(), kind))
        return std::nullopt;
    return document.object();
}
QByteArray json(const QJsonObject& value) { return QJsonDocument{value}.toJson(QJsonDocument::Compact); }

std::optional<Codec::Ready> readReady(const QJsonObject& object) {
    if (!envelope(object, "ready") ||
        !keys(object, {"owner", "schema_version", "kind", "nonce", "request_sha256", "observer_pid",
                       "observer_creation_filetime", "window_begin_filetime", "window_end_filetime"}))
        return std::nullopt;
    const auto hash = rawHex(object.value("request_sha256"), 32);
    const auto pid = number(object.value("observer_pid"));
    const auto creation = decimal(object.value("observer_creation_filetime"));
    const auto begin = decimal(object.value("window_begin_filetime"));
    const auto end = decimal(object.value("window_end_filetime"));
    if (!hash || !pid || *pid == 0 || !creation || *creation == 0 || !begin || !end || !window(*begin, *end))
        return std::nullopt;
    return Codec::Ready{.nonce = object.value("nonce").toString(),
                        .request_sha256 = *hash,
                        .observer_pid = *pid,
                        .observer_creation_filetime = *creation,
                        .window_begin_filetime = *begin,
                        .window_end_filetime = *end};
}
QJsonObject writeReady(const Codec::Ready& value) {
    auto object = envelope("ready", value.nonce);
    object.insert("request_sha256", hex(value.request_sha256));
    object.insert("observer_pid", number(value.observer_pid));
    object.insert("observer_creation_filetime", decimal(value.observer_creation_filetime));
    object.insert("window_begin_filetime", decimal(value.window_begin_filetime));
    object.insert("window_end_filetime", decimal(value.window_end_filetime));
    return object;
}
std::optional<Observer::Match> readMatch(const QJsonObject& object) {
    if (!keys(object, {"timestamp", "filter_id", "capability_id", "layer_id", "ms_direction"}))
        return std::nullopt;
    const auto timestamp = decimal(object.value("timestamp"));
    const auto filter = decimal(object.value("filter_id"));
    const auto capability = object.value("capability_id").isNull() ? std::optional{Observer::not_called}
                                                                   : number(object.value("capability_id"), 2);
    const auto layer = number(object.value("layer_id"), 65535);
    const auto direction = number(object.value("ms_direction"));
    if (!timestamp || *timestamp == 0 || !filter || *filter == 0 || !capability || !layer || !direction)
        return std::nullopt;
    return Observer::Match{.timestamp = *timestamp,
                           .filter_id = *filter,
                           .capability_id = *capability,
                           .layer_id = static_cast<std::uint16_t>(*layer),
                           .ms_direction = *direction};
}
QJsonValue writeMatch(const std::optional<Observer::Match>& value) {
    if (!value)
        return QJsonValue::Null;
    return QJsonObject{{"timestamp", decimal(value->timestamp)},
                       {"filter_id", decimal(value->filter_id)},
                       {"capability_id", errorValue(value->capability_id)},
                       {"layer_id", number(value->layer_id)},
                       {"ms_direction", number(value->ms_direction)}};
}
std::optional<Observer::FilterResult> readFilter(const QJsonObject& object) {
    if (!keys(object, {"error", "present", "filter_id", "disabled", "action", "provider", "sublayer", "layer",
                       "provider_guid", "sublayer_guid", "layer_guid", "filter_key", "flags", "action_type",
                       "query_begin_filetime", "query_end_filetime"}))
        return std::nullopt;
    const auto error = errorValue(object.value("error"));
    const auto present = boolean(object.value("present"));
    const auto id = decimal(object.value("filter_id"));
    const auto disabled = boolean(object.value("disabled"));
    const auto action = enumValue<Observer::FilterAction>(object.value("action"), actions);
    const auto provider = enumValue<Observer::ProviderIdentity>(object.value("provider"), providers);
    const auto sublayer = enumValue<Observer::SublayerIdentity>(object.value("sublayer"), sublayers);
    const auto layer = enumValue<Observer::LayerIdentity>(object.value("layer"), layers);
    const auto provider_guid = guid(object.value("provider_guid"));
    const auto sublayer_guid = guid(object.value("sublayer_guid"));
    const auto layer_guid = guid(object.value("layer_guid"));
    const auto filter_key = guid(object.value("filter_key"));
    const auto flags = number(object.value("flags"));
    const auto action_type = number(object.value("action_type"));
    const auto begin = decimal(object.value("query_begin_filetime"));
    const auto end = decimal(object.value("query_end_filetime"));
    if (!error || !present || !id || !disabled || !action || !provider || !sublayer || !layer || !provider_guid ||
        !sublayer_guid || !layer_guid || !filter_key || !flags || !action_type || !begin || !end)
        return std::nullopt;
    return Observer::FilterResult{.error = *error,
                                  .present = *present,
                                  .filter_id = *id,
                                  .disabled = *disabled,
                                  .action = *action,
                                  .provider = *provider,
                                  .sublayer = *sublayer,
                                  .layer = *layer,
                                  .provider_guid = *provider_guid,
                                  .sublayer_guid = *sublayer_guid,
                                  .layer_guid = *layer_guid,
                                  .filter_key = *filter_key,
                                  .flags = *flags,
                                  .action_type = *action_type,
                                  .query_begin_filetime = *begin,
                                  .query_end_filetime = *end};
}
QJsonObject writeFilter(const Observer::FilterResult& value) {
    return {{"error", errorValue(value.error)},
            {"present", value.present},
            {"filter_id", decimal(value.filter_id)},
            {"disabled", value.disabled},
            {"action", enumText(value.action, actions)},
            {"provider", enumText(value.provider, providers)},
            {"sublayer", enumText(value.sublayer, sublayers)},
            {"layer", enumText(value.layer, layers)},
            {"provider_guid", hex(value.provider_guid)},
            {"sublayer_guid", hex(value.sublayer_guid)},
            {"layer_guid", hex(value.layer_guid)},
            {"filter_key", hex(value.filter_key)},
            {"flags", number(value.flags)},
            {"action_type", number(value.action_type)},
            {"query_begin_filetime", decimal(value.query_begin_filetime)},
            {"query_end_filetime", decimal(value.query_end_filetime)}};
}
std::optional<Observer::LayerResult> readLayer(const QJsonObject& object) {
    if (!keys(object,
              {"error", "present", "layer_id", "layer", "layer_guid", "query_begin_filetime", "query_end_filetime"}))
        return std::nullopt;
    const auto error = errorValue(object.value("error"));
    const auto present = boolean(object.value("present"));
    const auto id = number(object.value("layer_id"), 65535);
    const auto layer = enumValue<Observer::LayerIdentity>(object.value("layer"), layers);
    const auto layer_guid = guid(object.value("layer_guid"));
    const auto begin = decimal(object.value("query_begin_filetime"));
    const auto end = decimal(object.value("query_end_filetime"));
    if (!error || !present || !id || !layer || !layer_guid || !begin || !end)
        return std::nullopt;
    return Observer::LayerResult{.error = *error,
                                 .present = *present,
                                 .layer_id = static_cast<std::uint16_t>(*id),
                                 .layer = *layer,
                                 .layer_guid = *layer_guid,
                                 .query_begin_filetime = *begin,
                                 .query_end_filetime = *end};
}
QJsonObject writeLayer(const Observer::LayerResult& value) {
    return {{"error", errorValue(value.error)},
            {"present", value.present},
            {"layer_id", number(value.layer_id)},
            {"layer", enumText(value.layer, layers)},
            {"layer_guid", hex(value.layer_guid)},
            {"query_begin_filetime", decimal(value.query_begin_filetime)},
            {"query_end_filetime", decimal(value.query_end_filetime)}};
}
std::optional<Observer::Report> readReport(const QJsonObject& object) {
    if (!keys(object, {"state",
                       "reason",
                       "available",
                       "matched",
                       "context_retained",
                       "app_id_error",
                       "open_error",
                       "query_error",
                       "keyword_query_error",
                       "subscribe_error",
                       "unsubscribe_error",
                       "close_error",
                       "value_is_uint32",
                       "collection_value",
                       "keywords_is_uint32",
                       "keywords_value",
                       "first_match",
                       "evidence_kind",
                       "first_candidate",
                       "rule_verified",
                       "filter_result",
                       "layer_result",
                       "window_begin_filetime",
                       "window_end_filetime"}))
        return std::nullopt;
    const auto state = enumValue<Observer::State>(object.value("state"), states);
    const auto reason = enumValue<Observer::Reason>(object.value("reason"), reasons);
    const auto available = boolean(object.value("available"));
    const auto matched = boolean(object.value("matched"));
    const auto retained = boolean(object.value("context_retained"));
    const auto app_id_error = errorValue(object.value("app_id_error"));
    const auto open_error = errorValue(object.value("open_error"));
    const auto query_error = errorValue(object.value("query_error"));
    const auto keyword_query_error = errorValue(object.value("keyword_query_error"));
    const auto subscribe_error = errorValue(object.value("subscribe_error"));
    const auto unsubscribe_error = errorValue(object.value("unsubscribe_error"));
    const auto close_error = errorValue(object.value("close_error"));
    const auto value_is_uint32 = boolean(object.value("value_is_uint32"));
    const auto collection = number(object.value("collection_value"));
    const auto keywords_is_uint32 = boolean(object.value("keywords_is_uint32"));
    const auto keywords = number(object.value("keywords_value"));
    const auto evidence = enumValue<Observer::EvidenceKind>(object.value("evidence_kind"), evidence_kinds);
    const auto rule_verified = boolean(object.value("rule_verified"));
    const auto begin = decimal(object.value("window_begin_filetime"));
    const auto end = decimal(object.value("window_end_filetime"));
    if (!state || *state == Observer::State::Idle || *state == Observer::State::Observing || !reason || !available ||
        !matched || !retained || !app_id_error || !open_error || !query_error || !keyword_query_error ||
        !subscribe_error || !unsubscribe_error || !close_error || !value_is_uint32 || !collection ||
        !keywords_is_uint32 || !keywords || !evidence || !rule_verified || !begin || !end ||
        !((*begin == 0 && *end == 0) || window(*begin, *end)))
        return std::nullopt;
    std::optional<Observer::Match> first_match, first_candidate;
    for (const auto* name : {"first_match", "first_candidate"}) {
        const auto value = object.value(QLatin1StringView{name});
        if (value.isNull())
            continue;
        if (!value.isObject())
            return std::nullopt;
        auto match = readMatch(value.toObject());
        if (!match)
            return std::nullopt;
        if (std::string_view{name} == "first_match")
            first_match = std::move(match);
        else
            first_candidate = std::move(match);
    }
    if (!object.value("filter_result").isObject() || !object.value("layer_result").isObject())
        return std::nullopt;
    const auto filter = readFilter(object.value("filter_result").toObject());
    const auto layer = readLayer(object.value("layer_result").toObject());
    if (!filter || !layer)
        return std::nullopt;
    return Observer::Report{.state = *state,
                            .reason = *reason,
                            .available = *available,
                            .matched = *matched,
                            .context_retained = *retained,
                            .app_id_error = *app_id_error,
                            .open_error = *open_error,
                            .query_error = *query_error,
                            .keyword_query_error = *keyword_query_error,
                            .subscribe_error = *subscribe_error,
                            .unsubscribe_error = *unsubscribe_error,
                            .close_error = *close_error,
                            .value_is_uint32 = *value_is_uint32,
                            .collection_value = *collection,
                            .keywords_is_uint32 = *keywords_is_uint32,
                            .keywords_value = *keywords,
                            .first_match = std::move(first_match),
                            .evidence_kind = *evidence,
                            .first_candidate = std::move(first_candidate),
                            .rule_verified = *rule_verified,
                            .filter_result = *filter,
                            .layer_result = *layer,
                            .window_begin_filetime = *begin,
                            .window_end_filetime = *end};
}
QJsonObject writeReport(const Observer::Report& value) {
    return {{"state", enumText(value.state, states)},
            {"reason", enumText(value.reason, reasons)},
            {"available", value.available},
            {"matched", value.matched},
            {"context_retained", value.context_retained},
            {"app_id_error", errorValue(value.app_id_error)},
            {"open_error", errorValue(value.open_error)},
            {"query_error", errorValue(value.query_error)},
            {"keyword_query_error", errorValue(value.keyword_query_error)},
            {"subscribe_error", errorValue(value.subscribe_error)},
            {"unsubscribe_error", errorValue(value.unsubscribe_error)},
            {"close_error", errorValue(value.close_error)},
            {"value_is_uint32", value.value_is_uint32},
            {"collection_value", number(value.collection_value)},
            {"keywords_is_uint32", value.keywords_is_uint32},
            {"keywords_value", number(value.keywords_value)},
            {"first_match", writeMatch(value.first_match)},
            {"evidence_kind", enumText(value.evidence_kind, evidence_kinds)},
            {"first_candidate", writeMatch(value.first_candidate)},
            {"rule_verified", value.rule_verified},
            {"filter_result", writeFilter(value.filter_result)},
            {"layer_result", writeLayer(value.layer_result)},
            {"window_begin_filetime", decimal(value.window_begin_filetime)},
            {"window_end_filetime", decimal(value.window_end_filetime)}};
}
} // namespace

QByteArray AppContainerObserverProtocol::encodeRequest(const Request& value) {
    if (!absoluteHelperPath(value.helper_path))
        return {};
    auto object = envelope("request", value.nonce);
    object.insert("driver_user_sid_hex", hex(value.driver_user_sid));
    object.insert("driver_pid", number(value.driver_pid));
    object.insert("driver_creation_filetime", decimal(value.driver_creation_filetime));
    object.insert("probe_sha256", hex(value.probe_sha256));
    object.insert("helper_path", value.helper_path);
    object.insert("helper_sha256", hex(value.helper_sha256));
    object.insert("package_sid_hex", hex(value.package_sid));
    object.insert("loopback_tcp_port", number(value.loopback_tcp_port));
    const auto bytes = json(object);
    return decodeRequest(bytes) ? bytes : QByteArray{};
}
std::optional<AppContainerObserverProtocol::Request>
AppContainerObserverProtocol::decodeRequest(const QByteArrayView bytes) {
    const auto object = frame(bytes, "request");
    if (!object || !keys(*object, {"owner", "schema_version", "kind", "nonce", "driver_user_sid_hex", "driver_pid",
                                   "driver_creation_filetime", "probe_sha256", "helper_path", "helper_sha256",
                                   "package_sid_hex", "loopback_tcp_port"}))
        return std::nullopt;
    const auto user = sid(object->value("driver_user_sid_hex"), false);
    const auto pid = number(object->value("driver_pid"));
    const auto creation = decimal(object->value("driver_creation_filetime"));
    const auto probe_hash = rawHex(object->value("probe_sha256"), 32);
    const auto path = object->value("helper_path");
    const auto helper_hash = rawHex(object->value("helper_sha256"), 32);
    const auto package = sid(object->value("package_sid_hex"), true);
    const auto port = number(object->value("loopback_tcp_port"), 65535);
    if (!user || !pid || *pid == 0 || !creation || *creation == 0 || !probe_hash || !path.isString() ||
        !absoluteHelperPath(path.toString()) || !helper_hash || !package || !port || *port == 0)
        return std::nullopt;
    return Request{.nonce = object->value("nonce").toString(),
                   .driver_user_sid = *user,
                   .driver_pid = *pid,
                   .driver_creation_filetime = *creation,
                   .probe_sha256 = *probe_hash,
                   .helper_path = path.toString(),
                   .helper_sha256 = *helper_hash,
                   .package_sid = *package,
                   .loopback_tcp_port = static_cast<std::uint16_t>(*port)};
}
QByteArray AppContainerObserverProtocol::encodeReady(const Ready& value) {
    const auto bytes = json(writeReady(value));
    return decodeReady(bytes) ? bytes : QByteArray{};
}
std::optional<AppContainerObserverProtocol::Ready>
AppContainerObserverProtocol::decodeReady(const QByteArrayView bytes) {
    const auto object = frame(bytes, "ready");
    return object ? readReady(*object) : std::nullopt;
}
QByteArray AppContainerObserverProtocol::encodeResult(const Result& value) {
    auto object = envelope("result", value.nonce);
    object.insert("request_sha256", hex(value.request_sha256));
    object.insert("observer_pid", number(value.observer_pid));
    object.insert("observer_creation_filetime", decimal(value.observer_creation_filetime));
    object.insert("status", enumText(value.status, statuses));
    object.insert("ready", value.ready ? QJsonValue{writeReady(*value.ready)} : QJsonValue{QJsonValue::Null});
    object.insert("report", value.report ? QJsonValue{writeReport(*value.report)} : QJsonValue{QJsonValue::Null});
    const auto bytes = json(object);
    return decodeResult(bytes) ? bytes : QByteArray{};
}
std::optional<AppContainerObserverProtocol::Result>
AppContainerObserverProtocol::decodeResult(const QByteArrayView bytes) {
    const auto object = frame(bytes, "result");
    if (!object || !keys(*object, {"owner", "schema_version", "kind", "nonce", "request_sha256", "observer_pid",
                                   "observer_creation_filetime", "status", "ready", "report"}))
        return std::nullopt;
    const auto hash = rawHex(object->value("request_sha256"), 32);
    const auto pid = number(object->value("observer_pid"));
    const auto creation = decimal(object->value("observer_creation_filetime"));
    const auto status = enumValue<ResultStatus>(object->value("status"), statuses);
    if (!hash || !pid || *pid == 0 || !creation || *creation == 0 || !status)
        return std::nullopt;
    Result result{.nonce = object->value("nonce").toString(),
                  .request_sha256 = *hash,
                  .observer_pid = *pid,
                  .observer_creation_filetime = *creation,
                  .status = *status};
    const auto ready = object->value("ready");
    const auto report = object->value("report");
    if (*status == ResultStatus::AdmissionRejected)
        return ready.isNull() && report.isNull() ? std::optional{result} : std::nullopt;
    if (!report.isObject())
        return std::nullopt;
    result.report = readReport(report.toObject());
    if (!result.report)
        return std::nullopt;
    if (!ready.isNull()) {
        if (!ready.isObject())
            return std::nullopt;
        result.ready = readReady(ready.toObject());
        if (!result.ready || result.ready->nonce != result.nonce ||
            result.ready->request_sha256 != result.request_sha256 ||
            result.ready->observer_pid != result.observer_pid ||
            result.ready->observer_creation_filetime != result.observer_creation_filetime ||
            result.ready->window_begin_filetime != result.report->window_begin_filetime ||
            result.ready->window_end_filetime != result.report->window_end_filetime)
            return std::nullopt;
    }
    return result;
}
QByteArray AppContainerObserverProtocol::requestSha256(const QByteArrayView bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool AppContainerObserverProtocol::matchesRequest(const Request& request, const QByteArrayView request_sha256,
                                                  const Ready& ready) {
    return request_sha256.size() == 32 && !encodeRequest(request).isEmpty() && !encodeReady(ready).isEmpty() &&
           ready.nonce == request.nonce && QByteArrayView{ready.request_sha256} == request_sha256;
}
bool AppContainerObserverProtocol::matchesRequest(const Request& request, const QByteArrayView request_sha256,
                                                  const Result& result) {
    return request_sha256.size() == 32 && !encodeRequest(request).isEmpty() && !encodeResult(result).isEmpty() &&
           result.nonce == request.nonce && QByteArrayView{result.request_sha256} == request_sha256;
}
} // namespace music_player::sandbox_probe
