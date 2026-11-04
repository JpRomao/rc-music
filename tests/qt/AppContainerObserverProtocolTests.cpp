#include "../windows/sandbox/AppContainerObserverProtocol.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;
using namespace music_player::sandbox_probe;
namespace {
using Codec = AppContainerObserverProtocol;
using Observer = AppContainerNetworkObserver;
constexpr auto package_hex = "010800000000000f0200000001000000020000000300000004000000050000000600000007000000";
constexpr auto user_hex = "010500000000000515000000010000000200000003000000e8030000";
constexpr auto nonce = "0123456789abcdef0123456789abcdef";
constexpr auto hash_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr auto guid_hex = "00112233445566778899aabbccddeeff";
constexpr std::uint64_t begin = 134'067'564'000'000'000;
constexpr std::uint64_t end = begin + Observer::maximum_window_ticks;
QByteArray json(const QJsonObject& value) { return QJsonDocument{value}.toJson(QJsonDocument::Compact); }
QJsonObject envelope(const char* kind) {
    return {{"owner", "rc-music-wfp-observer"}, {"schema_version", 1}, {"kind", kind}, {"nonce", nonce}};
}
QJsonObject requestObject() {
    auto object = envelope("request");
    object.insert("driver_user_sid_hex", user_hex);
    object.insert("driver_pid", 123);
    object.insert("driver_creation_filetime", QString::number(begin - 10));
    object.insert("probe_sha256", hash_hex);
    object.insert("helper_path", "C:\\private\\runtime\\helper.exe");
    object.insert("helper_sha256", hash_hex);
    object.insert("package_sid_hex", package_hex);
    object.insert("loopback_tcp_port", 54321);
    return object;
}
QJsonObject readyObject() {
    auto object = envelope("ready");
    object.insert("request_sha256", hash_hex);
    object.insert("observer_pid", 456);
    object.insert("observer_creation_filetime", QString::number(begin - 5));
    object.insert("window_begin_filetime", QString::number(begin));
    object.insert("window_end_filetime", QString::number(end));
    return object;
}
QJsonObject matchObject() {
    return {{"timestamp", QString::number(begin + 1)},
            {"filter_id", "18446744073709551615"},
            {"capability_id", QJsonValue::Null},
            {"layer_id", 48},
            {"ms_direction", 0x3901}};
}
QJsonObject reportObject() {
    const QJsonObject filter{{"error", 0},
                             {"present", true},
                             {"filter_id", "18446744073709551615"},
                             {"disabled", false},
                             {"action", "block"},
                             {"provider", "app-isolation"},
                             {"sublayer", "app-isolation"},
                             {"layer", "ale-auth-connect-v4"},
                             {"provider_guid", guid_hex},
                             {"sublayer_guid", guid_hex},
                             {"layer_guid", guid_hex},
                             {"filter_key", guid_hex},
                             {"flags", 4294967295.0},
                             {"action_type", 4097},
                             {"query_begin_filetime", QString::number(end + 1)},
                             {"query_end_filetime", QString::number(end + 2)}};
    const QJsonObject layer{{"error", 0},
                            {"present", true},
                            {"layer_id", 48},
                            {"layer", "ale-auth-connect-v4"},
                            {"layer_guid", guid_hex},
                            {"query_begin_filetime", QString::number(end + 3)},
                            {"query_end_filetime", QString::number(end + 4)}};
    return {{"state", "stopped"},
            {"reason", "none"},
            {"available", true},
            {"matched", true},
            {"context_retained", false},
            {"app_id_error", 0},
            {"open_error", 0},
            {"query_error", 0},
            {"keyword_query_error", 0},
            {"subscribe_error", 0},
            {"unsubscribe_error", 0},
            {"close_error", 0},
            {"value_is_uint32", true},
            {"collection_value", 1},
            {"keywords_is_uint32", true},
            {"keywords_value", 4},
            {"first_match", matchObject()},
            {"evidence_kind", "classify-isolation"},
            {"first_candidate", matchObject()},
            {"rule_verified", true},
            {"filter_result", filter},
            {"layer_result", layer},
            {"window_begin_filetime", QString::number(begin)},
            {"window_end_filetime", QString::number(end)}};
}
QJsonObject resultObject() {
    auto object = envelope("result");
    object.insert("request_sha256", hash_hex);
    object.insert("observer_pid", 456);
    object.insert("observer_creation_filetime", QString::number(begin - 5));
    object.insert("status", "observer-result");
    object.insert("ready", readyObject());
    object.insert("report", reportObject());
    return object;
}
Codec::Request request() {
    return {.nonce = QString::fromLatin1(nonce),
            .driver_user_sid = QByteArray::fromHex(user_hex),
            .driver_pid = 123,
            .driver_creation_filetime = begin - 10,
            .probe_sha256 = QByteArray::fromHex(hash_hex),
            .helper_path = u"C:\\private\\runtime\\helper.exe"_s,
            .helper_sha256 = QByteArray::fromHex(hash_hex),
            .package_sid = QByteArray::fromHex(package_hex),
            .loopback_tcp_port = 54321};
}
Codec::Ready ready() {
    return {.nonce = QString::fromLatin1(nonce),
            .request_sha256 = QByteArray::fromHex(hash_hex),
            .observer_pid = 456,
            .observer_creation_filetime = begin - 5,
            .window_begin_filetime = begin,
            .window_end_filetime = end};
}
void replace(QJsonObject& object, const QStringList& path, const QJsonValue& value) {
    if (path.size() == 1) {
        object.insert(path.front(), value);
        return;
    }
    auto child = object.value(path.front()).toObject();
    replace(child, path.mid(1), value);
    object.insert(path.front(), child);
}
void mutation(const char* name, const char* path, const QJsonValue& value) {
    QTest::newRow(name) << QString::fromLatin1(path) << value;
}
void mutationColumns() {
    QTest::addColumn<QString>("path");
    QTest::addColumn<QJsonValue>("value");
}
} // namespace

class AppContainerObserverProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void independentRequestRoundTripsClosedFields() {
        const auto decoded = Codec::decodeRequest(json(requestObject()));
        QVERIFY(decoded);
        QCOMPARE(decoded->nonce, request().nonce);
        QCOMPARE(decoded->driver_user_sid, request().driver_user_sid);
        QCOMPARE(decoded->driver_pid, request().driver_pid);
        QCOMPARE(decoded->driver_creation_filetime, request().driver_creation_filetime);
        QCOMPARE(decoded->probe_sha256, request().probe_sha256);
        QCOMPARE(decoded->helper_path, request().helper_path);
        QCOMPARE(decoded->helper_sha256, request().helper_sha256);
        QCOMPARE(decoded->package_sid, request().package_sid);
        QCOMPARE(decoded->loopback_tcp_port, request().loopback_tcp_port);
        QCOMPARE(QJsonDocument::fromJson(Codec::encodeRequest(*decoded)).object(), requestObject());
    }
    void independentReadyRoundTripsExactWindow() {
        const auto decoded = Codec::decodeReady(json(readyObject()));
        QVERIFY(decoded);
        QCOMPARE(decoded->observer_pid, ready().observer_pid);
        QCOMPARE(decoded->observer_creation_filetime, ready().observer_creation_filetime);
        QCOMPARE(decoded->window_begin_filetime, begin);
        QCOMPARE(decoded->window_end_filetime, end);
        QCOMPARE(QJsonDocument::fromJson(Codec::encodeReady(*decoded)).object(), readyObject());
    }
    void fullReportPreservesEveryFieldWithoutPromotingEvidence() {
        const auto decoded = Codec::decodeResult(json(resultObject()));
        QVERIFY(decoded && decoded->ready && decoded->report);
        QCOMPARE(decoded->status, Codec::ResultStatus::ObserverResult);
        const auto& report = *decoded->report;
        QCOMPARE(report.state, Observer::State::Stopped);
        QCOMPARE(report.reason, Observer::Reason::None);
        QVERIFY(report.available && report.matched && report.rule_verified && !report.context_retained);
        QVERIFY(report.first_candidate && report.first_match);
        QCOMPARE(report.first_match->filter_id, std::numeric_limits<std::uint64_t>::max());
        QCOMPARE(report.first_match->capability_id, Observer::not_called);
        QCOMPARE(report.filter_result.flags, std::numeric_limits<std::uint32_t>::max());
        QCOMPARE(report.filter_result.filter_key[1], std::uint8_t{0x11});
        QCOMPARE(report.filter_result.filter_key[15], std::uint8_t{0xff});
        QCOMPARE(report.layer_result.query_end_filetime, end + 4);
        QCOMPARE(QJsonDocument::fromJson(Codec::encodeResult(*decoded)).object(), resultObject());
        auto failed = *decoded;
        failed.report->matched = false;
        failed.report->rule_verified = false;
        failed.report->first_match.reset();
        failed.report->reason = Observer::Reason::IsolationRule;
        const auto loaded = Codec::decodeResult(Codec::encodeResult(failed));
        QVERIFY(loaded && loaded->report);
        QVERIFY(!loaded->report->matched && !loaded->report->rule_verified && !loaded->report->first_match);
        QVERIFY(loaded->report->first_candidate);
        QCOMPARE(loaded->report->reason, Observer::Reason::IsolationRule);
    }
    void failedStartupNeedsNoReadyAndPreservesNullErrors() {
        auto object = resultObject();
        object.insert("ready", QJsonValue::Null);
        auto report = reportObject();
        report.insert("state", "unavailable");
        report.insert("reason", "window-clock");
        for (const auto* field : {"available", "matched", "rule_verified", "value_is_uint32", "keywords_is_uint32"})
            report.insert(QLatin1StringView{field}, false);
        for (const auto* field : {"app_id_error", "open_error", "query_error", "keyword_query_error", "subscribe_error",
                                  "unsubscribe_error", "close_error"})
            report.insert(QLatin1StringView{field}, QJsonValue::Null);
        report.insert("first_match", QJsonValue::Null);
        report.insert("first_candidate", QJsonValue::Null);
        report.insert("window_begin_filetime", "0");
        report.insert("window_end_filetime", "0");
        object.insert("report", report);
        const auto decoded = Codec::decodeResult(json(object));
        QVERIFY(decoded && !decoded->ready && decoded->report);
        QCOMPARE(decoded->report->reason, Observer::Reason::WindowClock);
        QCOMPARE(decoded->report->open_error, Observer::not_called);
        QCOMPARE(decoded->report->window_begin_filetime, std::uint64_t{0});
        QCOMPARE(QJsonDocument::fromJson(Codec::encodeResult(*decoded)).object(), object);
    }
    void admissionRejectionHasIdentityButNoFabricatedReport() {
        auto object = resultObject();
        object.insert("status", "admission-rejected");
        object.insert("ready", QJsonValue::Null);
        object.insert("report", QJsonValue::Null);
        const auto decoded = Codec::decodeResult(json(object));
        QVERIFY(decoded && !decoded->ready && !decoded->report);
        QCOMPARE(decoded->observer_pid, std::uint32_t{456});
        QCOMPARE(QJsonDocument::fromJson(Codec::encodeResult(*decoded)).object(), object);
    }
    void allTerminalStatesAndReasonsRoundTrip() {
        const QStringList reasons{"none",
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
        for (const auto& state : {"stopped", "unavailable", "stop-unconfirmed"}) {
            for (qsizetype i = 0; i < reasons.size(); ++i) {
                auto object = resultObject();
                auto report = reportObject();
                report.insert("state", state);
                report.insert("reason", reasons[i]);
                object.insert("report", report);
                const auto decoded = Codec::decodeResult(json(object));
                QVERIFY(decoded && decoded->report);
                QCOMPARE(decoded->report->reason, static_cast<Observer::Reason>(i));
                QCOMPARE(QJsonDocument::fromJson(Codec::encodeResult(*decoded)).object(), object);
            }
        }
    }
    void requestMutations_data() {
        mutationColumns();
        mutation("extra", "extra", true);
        mutation("version-future", "schema_version", 2);
        mutation("version-fraction", "schema_version", 1.5);
        mutation("version-bool", "schema_version", true);
        mutation("owner", "owner", "other");
        mutation("kind", "kind", "ready");
        mutation("nonce-short", "nonce", "0123456789abcdef");
        mutation("nonce-uppercase", "nonce", "0123456789ABCDEF0123456789abcdef");
        mutation("user-sid-uppercase", "driver_user_sid_hex", "0101000000000005FF000000");
        mutation("user-sid-length", "driver_user_sid_hex", "010500000000000515000000");
        mutation("user-sid-revision", "driver_user_sid_hex", "020100000000000515000000");
        mutation("user-sid-subcount", "driver_user_sid_hex", "011000000000000515000000");
        mutation("package-user-sid", "package_sid_hex", user_hex);
        mutation("package-sid-base", "package_sid_hex", QString::fromLatin1(package_hex).replace(16, 2, "03"));
        mutation("pid-zero", "driver_pid", 0);
        mutation("pid-negative", "driver_pid", -1);
        mutation("pid-fraction", "driver_pid", 1.5);
        mutation("pid-overflow", "driver_pid", 4294967296.0);
        mutation("pid-string", "driver_pid", "123");
        mutation("time-number", "driver_creation_filetime", 123);
        mutation("time-zero", "driver_creation_filetime", "0");
        mutation("time-leading-zero", "driver_creation_filetime", "0123");
        mutation("time-plus", "driver_creation_filetime", "+123");
        mutation("time-overflow", "driver_creation_filetime", "18446744073709551616");
        mutation("time-fraction", "driver_creation_filetime", "1.5");
        mutation("probe-hash-short", "probe_sha256", "abcd");
        mutation("helper-hash-uppercase", "helper_sha256", QString::fromLatin1(hash_hex).toUpper());
        mutation("helper-relative", "helper_path", "runtime/helper.exe");
        mutation("helper-nul", "helper_path", u"C:/"_s + QChar{0} + u"x"_s);
        mutation("helper-type", "helper_path", true);
        mutation("port-zero", "loopback_tcp_port", 0);
        mutation("port-overflow", "loopback_tcp_port", 65536);
        mutation("port-fraction", "loopback_tcp_port", 1.5);
        mutation("port-null", "loopback_tcp_port", QJsonValue::Null);
    }
    void requestMutations() {
        QFETCH(QString, path);
        QFETCH(QJsonValue, value);
        auto object = requestObject();
        replace(object, path.split('.'), value);
        QVERIFY(!Codec::decodeRequest(json(object)));
    }
    void readyMutations_data() {
        mutationColumns();
        mutation("extra", "extra", true);
        mutation("kind", "kind", "result");
        mutation("nonce", "nonce", "");
        mutation("hash", "request_sha256", QString::fromLatin1(hash_hex).toUpper());
        mutation("pid-zero", "observer_pid", 0);
        mutation("creation-zero", "observer_creation_filetime", "0");
        mutation("begin-zero", "window_begin_filetime", "0");
        mutation("end-before", "window_end_filetime", QString::number(begin - 1));
        mutation("window-short", "window_end_filetime", QString::number(end - 1));
        mutation("window-long", "window_end_filetime", QString::number(end + 1));
        mutation("time-numeric", "window_begin_filetime", 123);
        mutation("time-leading-zero", "window_end_filetime", "0134067564150000000");
        mutation("time-overflow", "window_end_filetime", "18446744073709551616");
    }
    void readyMutations() {
        QFETCH(QString, path);
        QFETCH(QJsonValue, value);
        auto object = readyObject();
        replace(object, path.split('.'), value);
        QVERIFY(!Codec::decodeReady(json(object)));
    }
    void resultMutations_data() {
        mutationColumns();
        mutation("extra", "extra", true);
        mutation("kind", "kind", "request");
        mutation("pid-zero", "observer_pid", 0);
        mutation("creation-zero", "observer_creation_filetime", "0");
        mutation("status-unknown", "status", "passed");
        mutation("rejected-with-evidence", "status", "admission-rejected");
        mutation("missing-report", "report", QJsonValue::Null);
        mutation("ready-other-nonce", "ready.nonce", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        mutation("ready-other-hash", "ready.request_sha256", QString(64, 'a'));
        mutation("ready-other-pid", "ready.observer_pid", 789);
        mutation("ready-other-creation", "ready.observer_creation_filetime", QString::number(begin - 4));
        mutation("report-extra", "report.extra", false);
        mutation("report-idle", "report.state", "idle");
        mutation("report-observing", "report.state", "observing");
        mutation("reason-unknown", "report.reason", "oops");
        mutation("evidence-unknown", "report.evidence_kind", "tcp-timeout");
        mutation("available-type", "report.available", 1);
        mutation("matched-type", "report.matched", "true");
        mutation("rule-type", "report.rule_verified", QJsonValue::Null);
        mutation("retained-type", "report.context_retained", 0);
        mutation("error-string", "report.open_error", "5");
        mutation("error-negative", "report.close_error", -1);
        mutation("error-sentinel-must-null", "report.subscribe_error", 4294967295.0);
        mutation("collection-overflow", "report.collection_value", 4294967296.0);
        mutation("keywords-fraction", "report.keywords_value", 0.5);
        mutation("window-different", "report.window_begin_filetime", QString::number(begin + 1));
        mutation("match-extra", "report.first_match.extra", true);
        mutation("filter-id-numeric", "report.first_match.filter_id", 1);
        mutation("filter-id-overflow", "report.first_candidate.filter_id", "18446744073709551616");
        mutation("capability-invalid", "report.first_match.capability_id", 3);
        mutation("layer-id-overflow", "report.first_candidate.layer_id", 65536);
        mutation("direction-negative", "report.first_candidate.ms_direction", -1);
        mutation("filter-extra", "report.filter_result.extra", true);
        mutation("provider-unknown", "report.filter_result.provider", "other");
        mutation("sublayer-unknown", "report.filter_result.sublayer", "other");
        mutation("layer-unknown", "report.filter_result.layer", "other");
        mutation("action-unknown", "report.filter_result.action", "other");
        mutation("guid-length", "report.filter_result.provider_guid", "0011");
        mutation("guid-uppercase", "report.filter_result.filter_key", QString::fromLatin1(guid_hex).toUpper());
        mutation("guid-nothex", "report.filter_result.layer_guid", QString(32, 'g'));
        mutation("flags-fraction", "report.filter_result.flags", 1.5);
        mutation("action-type-overflow", "report.filter_result.action_type", 4294967296.0);
        mutation("query-time-leadingzero", "report.filter_result.query_begin_filetime", "00");
        mutation("layer-extra", "report.layer_result.extra", true);
        mutation("layer-type", "report.layer_result.layer", true);
        mutation("layer-error-sentinel", "report.layer_result.error", 4294967295.0);
    }
    void resultMutations() {
        QFETCH(QString, path);
        QFETCH(QJsonValue, value);
        auto object = resultObject();
        replace(object, path.split('.'), value);
        QVERIFY(!Codec::decodeResult(json(object)));
    }
    void missingFieldsAreRejectedAtEveryLevel() {
        for (const auto kind : {0, 1, 2}) {
            const auto initial = kind == 0 ? requestObject() : kind == 1 ? readyObject() : resultObject();
            for (const auto& key : initial.keys()) {
                auto object = initial;
                object.remove(key);
                const auto bytes = json(object);
                QVERIFY(kind == 0   ? !Codec::decodeRequest(bytes)
                        : kind == 1 ? !Codec::decodeReady(bytes)
                                    : !Codec::decodeResult(bytes));
            }
        }
        const auto original = resultObject();
        for (const auto path : {"report", "report.filter_result", "report.layer_result", "report.first_match"}) {
            auto nested = original;
            for (const auto& part : QString::fromLatin1(path).split('.'))
                nested = nested.value(part).toObject();
            for (const auto& key : nested.keys()) {
                auto child = nested;
                child.remove(key);
                auto object = original;
                replace(object, QString::fromLatin1(path).split('.'), child);
                QVERIFY(!Codec::decodeResult(json(object)));
            }
        }
    }
    void lexicalGuardRejectsDuplicatesUnicodeAndTrailingDocuments_data() {
        QTest::addColumn<QByteArray>("bytes");
        const auto bytes = json(requestObject());
        auto duplicate = bytes;
        duplicate.insert(1, "\"nonce\":\"0123456789abcdef0123456789abcdef\",");
        QTest::newRow("duplicate") << duplicate;
        auto escaped = bytes;
        escaped.insert(1, "\"no\\u006ece\":\"0123456789abcdef0123456789abcdef\",");
        QTest::newRow("escaped-duplicate") << escaped;
        auto invalid_utf8 = bytes;
        invalid_utf8.replace("helper.exe", QByteArray{"helper"} + QByteArray::fromHex("c0af") + ".exe");
        QTest::newRow("utf8-overlong") << invalid_utf8;
        auto surrogate = bytes;
        surrogate.replace("helper.exe", "\\ud800.exe");
        QTest::newRow("unpaired-surrogate") << surrogate;
        QTest::newRow("trailing-document") << bytes + "{}";
        QTest::newRow("bom") << QByteArray::fromHex("efbbbf") + bytes;
        QTest::newRow("non-object") << QByteArray{"[]"};
        QTest::newRow("empty") << QByteArray{};
    }
    void lexicalGuardRejectsDuplicatesUnicodeAndTrailingDocuments() {
        QFETCH(QByteArray, bytes);
        QVERIFY(!Codec::decodeRequest(bytes));
    }
    void inclusiveFrameBoundaryAppliesToAllThreeDocuments() {
        for (const auto kind : {0, 1, 2}) {
            auto bytes = json(kind == 0 ? requestObject() : kind == 1 ? readyObject() : resultObject());
            QVERIFY(bytes.size() < Codec::maximum_frame_size);
            bytes.append(Codec::maximum_frame_size - bytes.size(), ' ');
            QVERIFY(kind == 0   ? Codec::decodeRequest(bytes).has_value()
                    : kind == 1 ? Codec::decodeReady(bytes).has_value()
                                : Codec::decodeResult(bytes).has_value());
            bytes.append(' ');
            QVERIFY(kind == 0   ? !Codec::decodeRequest(bytes)
                    : kind == 1 ? !Codec::decodeReady(bytes)
                                : !Codec::decodeResult(bytes));
        }
    }
    void exactByteHashAndBindingRejectStaleFrames() {
        const auto raw = json(requestObject());
        const auto expected = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
        QCOMPARE(Codec::requestSha256(raw), expected);
        QVERIFY(Codec::requestSha256(raw + ' ') != expected);
        auto ready_frame = ready();
        ready_frame.request_sha256 = expected;
        QVERIFY(Codec::matchesRequest(request(), expected, ready_frame));
        ready_frame.nonce[0] = 'a';
        QVERIFY(!Codec::matchesRequest(request(), expected, ready_frame));
        ready_frame = ready();
        QVERIFY(!Codec::matchesRequest(request(), expected, ready_frame));
        auto result = Codec::decodeResult(json(resultObject()));
        QVERIFY(result);
        result->request_sha256 = expected;
        result->ready->request_sha256 = expected;
        QVERIFY(Codec::matchesRequest(request(), expected, *result));
        result->ready->observer_pid = 1;
        QVERIFY(!Codec::matchesRequest(request(), expected, *result));
    }
    void invalidTypedInputsCannotBeEncoded() {
        auto req = request();
        req.package_sid.clear();
        QVERIFY(Codec::encodeRequest(req).isEmpty());
        auto started = ready();
        started.window_end_filetime = std::numeric_limits<std::uint64_t>::max();
        QVERIFY(Codec::encodeReady(started).isEmpty());
        Codec::Result result;
        result.nonce = QString::fromLatin1(nonce);
        result.request_sha256 = QByteArray::fromHex(hash_hex);
        result.observer_pid = 456;
        result.observer_creation_filetime = begin - 5;
        result.status = static_cast<Codec::ResultStatus>(99);
        QVERIFY(Codec::encodeResult(result).isEmpty());
    }
};
QTEST_GUILESS_MAIN(AppContainerObserverProtocolTests)
#include "AppContainerObserverProtocolTests.moc"
