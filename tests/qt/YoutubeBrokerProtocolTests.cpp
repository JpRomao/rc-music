#include "adapters/YoutubeBrokerProtocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QtEndian>

#include <limits>

using namespace music_player::m0;
namespace {
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
YoutubeBrokerChallenge challenge() {
    return {QStringLiteral("function syntheticPlayer() { return '日本'; }"),
            {{QStringLiteral("n"), {QStringLiteral("alpha"), QStringLiteral("日本"), QStringLiteral("alpha")}},
             {QStringLiteral("sig"), {QStringLiteral("signature")}}}};
}
QJsonArray requests(const YoutubeBrokerChallenge& value) {
    QJsonArray result;
    for (const auto& request : value.requests)
        result.push_back(
            QJsonObject{{"type", request.type}, {"challenges", QJsonArray::fromStringList(request.challenges)}});
    return result;
}
QJsonObject challengeMessage(const YoutubeBrokerChallenge& value = challenge(), const quint32 sequence = 1) {
    return {{"v", 1},
            {"kind", "challenge"},
            {"seq", static_cast<qint64>(sequence)},
            {"player", value.player},
            {"requests", requests(value)}};
}
QJsonObject resultMessage(const QJsonObject& data = {{"id", "synthetic"}}, const quint32 sequence = 1) {
    return {{"v", 1}, {"kind", "result"}, {"seq", static_cast<qint64>(sequence)}, {"data", data}};
}
QJsonArray responses(const YoutubeBrokerChallenge& value) {
    QJsonArray result;
    for (const auto& request : value.requests) {
        QJsonObject solved;
        for (const auto& input : request.challenges)
            solved.insert(input, QStringLiteral("solved:") + input.left(64));
        result.push_back(QJsonObject{{"type", "result"}, {"data", solved}});
    }
    return result;
}
QByteArray nodeOutput(const QJsonArray& values) { return json({{"type", "result"}, {"responses", values}}); }
QByteArray nodeOutput(const YoutubeBrokerChallenge& value = challenge()) { return nodeOutput(responses(value)); }
QJsonObject responseMessage(const quint32 sequence, const QJsonArray& values) {
    return {{"v", 1}, {"kind", "response"}, {"seq", static_cast<qint64>(sequence)}, {"responses", values}};
}
QByteArray prefix(const quint32 size) {
    QByteArray result(4, '\0');
    qToLittleEndian(size, reinterpret_cast<uchar*>(result.data()));
    return result;
}
QByteArray frame(const QByteArray& payload) { return prefix(static_cast<quint32>(payload.size())) + payload; }
void invalid(const QByteArray& payload, const DownloadFailure expected = DownloadFailure::InvalidResult,
             const quint32 sequence = 1) {
    const auto result = decodeYoutubeBrokerMessage(payload, sequence);
    QCOMPARE(result.error, expected);
    QVERIFY(!result.message);
}
QByteArray script(const QByteArray& body, const qsizetype bytes) {
    return body + "\n//" + QByteArray(bytes - body.size() - 4, 'x') + '\n';
}
const QByteArray core{"function jsc(data) { return {type: 'result', responses: []}; }"};
const QByteArray library{"var lib = {syntheticHelper: function() { return 1; }};"};
} // namespace

class YoutubeBrokerProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void challengePreservesUnicodeDuplicateInputsAndBothRequestTypes() {
        const auto original = challenge();
        const auto result = decodeYoutubeBrokerMessage(json(challengeMessage(original, 8)), 8);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message);
        QCOMPARE(result.message->kind, YoutubeBrokerMessageKind::Challenge);
        QCOMPARE(result.message->sequence, quint32{8});
        QVERIFY(result.message->challenge);
        QVERIFY(*result.message->challenge == original);
        QVERIFY(result.message->result_json.isEmpty());
        QCOMPARE(result.message->failure, DownloadFailure::None);
    }
    void finalResultPreservesOnlyCompactObjectDataAndAllowsSequenceNine() {
        const QJsonObject data{{"id", "synthetic"}, {"nested", QJsonObject{{"literal", "日本 <b>plain</b>"}}}};
        const auto result = decodeYoutubeBrokerMessage(json(resultMessage(data, 9)), 9);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message);
        QCOMPARE(result.message->kind, YoutubeBrokerMessageKind::Result);
        QCOMPARE(result.message->sequence, quint32{9});
        QCOMPARE(result.message->result_json, json(data));
        QVERIFY(!result.message->challenge);
        QCOMPARE(result.message->failure, DownloadFailure::None);
    }
    void typedFailureUsesOnlyTheThreeSupportedCodes_data() {
        QTest::addColumn<QString>("code");
        QTest::addColumn<int>("failure");
        QTest::newRow("unavailable") << QStringLiteral("unavailable") << static_cast<int>(DownloadFailure::Unavailable);
        QTest::newRow("restricted") << QStringLiteral("restricted") << static_cast<int>(DownloadFailure::Restricted);
        QTest::newRow("unsupported") << QStringLiteral("unsupported")
                                     << static_cast<int>(DownloadFailure::UnsupportedMedia);
    }
    void typedFailureUsesOnlyTheThreeSupportedCodes() {
        QFETCH(QString, code);
        QFETCH(int, failure);
        const auto result =
            decodeYoutubeBrokerMessage(json({{"v", 1}, {"kind", "failure"}, {"seq", 9}, {"code", code}}), 9);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message);
        QCOMPARE(result.message->kind, YoutubeBrokerMessageKind::Failure);
        QCOMPARE(result.message->failure, static_cast<DownloadFailure>(failure));
        QCOMPARE(result.message->sequence, quint32{9});
        QVERIFY(!result.message->challenge);
        QVERIFY(result.message->result_json.isEmpty());
    }
    void exactRootAndRequestSchemasAndCorrelatedSequencesAreRequired_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode < 18; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void exactRootAndRequestSchemasAndCorrelatedSequencesAreRequired() {
        QFETCH(int, mode);
        auto object = challengeMessage();
        switch (mode) {
        case 0:
            object.remove("v");
            break;
        case 1:
            object.insert("v", 2);
            break;
        case 2:
            object.insert("v", "1");
            break;
        case 3:
            object.insert("kind", "response");
            break;
        case 4:
            object.insert("extra", true);
            break;
        case 5:
            object.insert("seq", 0);
            break;
        case 6:
            object.insert("seq", 9);
            break;
        case 7:
            object.insert("seq", 1.5);
            break;
        case 8:
            object.insert("seq", "1");
            break;
        case 9:
            object.insert("seq", 2);
            break;
        case 10:
            object.insert("player", true);
            break;
        case 11:
            object.insert("requests", QJsonObject{});
            break;
        case 12:
            object.insert("requests", QJsonArray{true});
            break;
        case 13:
            object.insert("requests", QJsonArray{QJsonObject{{"type", "n"}}});
            break;
        case 14:
            object.insert("requests",
                          QJsonArray{QJsonObject{{"type", "n"}, {"challenges", QJsonArray{"a"}}, {"extra", 1}}});
            break;
        case 15:
            object.insert("requests", QJsonArray{QJsonObject{{"type", "N"}, {"challenges", QJsonArray{"a"}}}});
            break;
        case 16:
            object.insert("requests", QJsonArray{QJsonObject{{"type", "sig"}, {"challenges", QJsonArray{1}}}});
            break;
        case 17:
            object.insert("requests", QJsonArray{QJsonObject{{"type", "n"}, {"challenges", "a"}}});
            break;
        }
        invalid(json(object));
    }
    void resultAndFailureShapesCannotSmuggleExtraFieldsOrWrongTypes() {
        auto object = resultMessage();
        object.insert("data", QJsonArray{});
        invalid(json(object));
        object = resultMessage();
        object.insert("extra", "private diagnostic");
        invalid(json(object));
        object.remove("extra");
        object.insert("seq", 0);
        invalid(json(object), DownloadFailure::InvalidResult, 0);
        object.insert("seq", 10);
        invalid(json(object), DownloadFailure::InvalidResult, 10);
        object = {{"v", 1}, {"kind", "failure"}, {"seq", 1}, {"code", "arbitrary raw error"}};
        invalid(json(object));
        object.insert("code", "unavailable");
        object.insert("error", "private path must never become a code");
        invalid(json(object));
    }
    void lexicalJsonMustBeStrictUtf8WithoutDuplicatesOrTrailingDocuments_data() {
        QTest::addColumn<QByteArray>("payload");
        const QByteArray good{"{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{}}"};
        QTest::newRow("duplicate root key")
            << QByteArray{"{\"v\":1,\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{}}"};
        QTest::newRow("escaped duplicate key")
            << QByteArray{"{\"v\":1,\"\\u0076\":1,\"kind\":\"result\",\"seq\":1,\"data\":{}}"};
        QTest::newRow("nested duplicate")
            << QByteArray{"{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{\"x\":1,\"x\":2}}"};
        QTest::newRow("invalid UTF8") << good.left(good.size() - 2) + QByteArray("\xc0\xaf", 2) + "}}";
        QTest::newRow("unpaired surrogate")
            << QByteArray{"{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{\"x\":\"\\ud800\"}}"};
        QTest::newRow("unknown escape") << QByteArray{
            "{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{\"x\":\"\\q\"}}"};
        QTest::newRow("raw control") << QByteArray{"{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{\"x\":\"a\nb\"}}"};
        QTest::newRow("trailing document") << good + good;
        QTest::newRow("truncated") << good.first(good.size() - 1);
        QTest::newRow("BOM") << QByteArray("\xef\xbb\xbf", 3) + good;
        QTest::newRow("root array") << QByteArray{"[]"};
        QTest::newRow("trailing comma") << QByteArray{"{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{},}"};
    }
    void lexicalJsonMustBeStrictUtf8WithoutDuplicatesOrTrailingDocuments() {
        QFETCH(QByteArray, payload);
        invalid(payload);
    }
    void jsonDepthEightIsAcceptedAndNineIsRefused() {
        QByteArray value{"\"leaf\""};
        for (int index = 0; index < 6; ++index)
            value = '[' + value + ']';
        const auto nested = [&](const QByteArray& data) {
            return QByteArray{"{\"v\":1,\"kind\":\"result\",\"seq\":1,\"data\":{\"nested\":"} + data + "}}";
        };
        const auto result = decodeYoutubeBrokerMessage(nested(value), 1);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message);
        invalid(nested('[' + value + ']'));
    }
    void compactFinalDataHasAnExactOneMiBBoundary() {
        QJsonObject data{{"value", ""}};
        const auto overhead = json(data).size();
        data.insert("value", QString(youtube_broker_max_response_bytes - overhead, QLatin1Char('a')));
        QCOMPARE(json(data).size(), youtube_broker_max_response_bytes);
        const auto result = decodeYoutubeBrokerMessage(json(resultMessage(data)), 1);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message);
        QCOMPARE(result.message->result_json.size(), youtube_broker_max_response_bytes);
        data.insert("value", data.value("value").toString() + 'x');
        invalid(json(resultMessage(data)), DownloadFailure::LimitExceeded);
        invalid(QByteArray(youtube_broker_max_frame_bytes + 1, 'x'), DownloadFailure::LimitExceeded);
    }
    void validChallengeBoundsPermitDuplicateValuesAndCountUtf8Bytes() {
        auto value = challenge();
        value.player = QString(youtube_broker_max_player_bytes, QLatin1Char('p'));
        value.requests = {{QStringLiteral("n"), {QString(youtube_broker_max_value_bytes / 2, QChar{u'é'})}}};
        auto result = decodeYoutubeBrokerMessage(json(challengeMessage(value)), 1);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message && result.message->challenge);
        QVERIFY(*result.message->challenge == value);
        value = challenge();
        value.requests = {{QStringLiteral("n"), QStringList(youtube_broker_max_challenges, QStringLiteral("same"))}};
        result = decodeYoutubeBrokerMessage(json(challengeMessage(value)), 1);
        QCOMPARE(result.error, DownloadFailure::None);
        value.requests.assign(youtube_broker_max_requests, {QStringLiteral("sig"), {QStringLiteral("same")}});
        result = decodeYoutubeBrokerMessage(json(challengeMessage(value)), 1);
        QCOMPARE(result.error, DownloadFailure::None);
        value.requests = {
            {QStringLiteral("n"), QStringList(64, QString(youtube_broker_max_value_bytes, QLatin1Char('a')))}};
        result = decodeYoutubeBrokerMessage(json(challengeMessage(value)), 1);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.message);
    }
    void typedChallengeValidationAppliesToDecodeResponseAndNodeInput_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("limit");
        for (int mode = 0; mode < 14; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode << (mode >= 8);
    }
    void typedChallengeValidationAppliesToDecodeResponseAndNodeInput() {
        QFETCH(int, mode);
        QFETCH(bool, limit);
        auto value = challenge();
        switch (mode) {
        case 0:
            value.player.clear();
            break;
        case 1:
            value.player = QStringLiteral(" \t\r\n");
            break;
        case 2:
            value.player += QChar{0xd800};
            break;
        case 3:
            value.requests.clear();
            break;
        case 4:
            value.requests.front().type = QStringLiteral(" sig ");
            break;
        case 5:
            value.requests.front().challenges.clear();
            break;
        case 6:
            value.requests.front().challenges = {QStringLiteral(" \n\t")};
            break;
        case 7:
            value.requests.front().challenges = {QString(QChar{0xd800})};
            break;
        case 8:
            value.player = QString(youtube_broker_max_player_bytes + 1, QLatin1Char('p'));
            break;
        case 9:
            value.requests.assign(youtube_broker_max_requests + 1, {QStringLiteral("n"), {QStringLiteral("a")}});
            break;
        case 10:
            value.requests.front().challenges = QStringList(youtube_broker_max_challenges + 1, QStringLiteral("a"));
            break;
        case 11:
            value.requests.front().challenges = {QString(youtube_broker_max_value_bytes + 1, QLatin1Char('a'))};
            break;
        case 12:
            value.requests.front().challenges = {QString(youtube_broker_max_value_bytes / 2 + 1, QChar{u'é'})};
            break;
        case 13:
            value.requests = {
                {QStringLiteral("n"), QStringList(65, QString(youtube_broker_max_value_bytes, QLatin1Char('a')))}};
            break;
        }
        if (mode == 2)
            invalid(QByteArray{"{\"v\":1,\"kind\":\"challenge\",\"seq\":1,\"player\":\"\\ud800\",\"requests\":[{"
                               "\"type\":\"n\",\"challenges\":[\"a\"]}]}"});
        else if (mode == 7)
            invalid(QByteArray{"{\"v\":1,\"kind\":\"challenge\",\"seq\":1,\"player\":\"player\",\"requests\":[{"
                               "\"type\":\"n\",\"challenges\":[\"\\ud800\"]}]}"});
        else
            invalid(json(challengeMessage(value)),
                    limit ? DownloadFailure::LimitExceeded : DownloadFailure::InvalidResult);
        QVERIFY(!encodeYoutubeBrokerResponse(1, value, nodeOutput(value)));
        QVERIFY(!youtubeBrokerNodeInput(value, core, library));
    }
    void responseCorrelatesEveryRequestAndUsesUniqueChallengeKeys() {
        const auto original = challenge();
        const auto values = responses(original);
        const auto encoded = encodeYoutubeBrokerResponse(8, original, nodeOutput(values));
        QVERIFY(encoded);
        QVERIFY(encoded->size() >= 4);
        QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(encoded->constData())),
                 static_cast<quint32>(encoded->size() - 4));
        QCOMPARE(QJsonDocument::fromJson(encoded->mid(4)).object(), responseMessage(8, values));
        QCOMPARE(values.first().toObject().value("data").toObject().size(), 2);
        QVERIFY(!encodeYoutubeBrokerResponse(0, original, nodeOutput(values)));
        QVERIFY(!encodeYoutubeBrokerResponse(9, original, nodeOutput(values)));
    }
    void perRequestErrorsAreNormalizedWithoutLeakingNodeErrorText() {
        auto values = responses(challenge());
        const QString secret = QStringLiteral("PRIVATE C:/Users/test/key?secret=1\nraw diagnostic");
        values[1] = QJsonObject{{"type", "error"}, {"error", secret}};
        const auto encoded = encodeYoutubeBrokerResponse(1, challenge(), nodeOutput(values));
        QVERIFY(encoded);
        values[1] = QJsonObject{{"type", "error"}, {"code", "unavailable"}};
        QCOMPARE(QJsonDocument::fromJson(encoded->mid(4)).object(), responseMessage(1, values));
        QVERIFY(!encoded->contains("PRIVATE"));
        QVERIFY(!encoded->contains("secret"));
        QVERIFY(!encodeYoutubeBrokerResponse(1, challenge(), json({{"type", "error"}, {"error", secret}})));
    }
    void nodeResultRequiresExactShapesCountsKeysAndBoundedValues_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode < 17; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void nodeResultRequiresExactShapesCountsKeysAndBoundedValues() {
        QFETCH(int, mode);
        auto values = responses(challenge());
        auto first = values.first().toObject();
        auto data = first.value("data").toObject();
        QJsonObject root{{"type", "result"}, {"responses", values}};
        switch (mode) {
        case 0:
            root.insert("extra", true);
            break;
        case 1:
            root.insert("type", "other");
            break;
        case 2:
            root.remove("responses");
            break;
        case 3:
            values.removeLast();
            break;
        case 4:
            values.push_back(values.last());
            break;
        case 5:
            values[0] = true;
            break;
        case 6:
            first.insert("extra", 1);
            values[0] = first;
            break;
        case 7:
            data.remove("alpha");
            first.insert("data", data);
            values[0] = first;
            break;
        case 8:
            data.insert("unexpected", "extra");
            first.insert("data", data);
            values[0] = first;
            break;
        case 9:
            data.insert("alpha", 3);
            first.insert("data", data);
            values[0] = first;
            break;
        case 10:
            data.insert("alpha", " \n\t");
            first.insert("data", data);
            values[0] = first;
            break;
        case 11:
            data.insert("alpha", QString(youtube_broker_max_value_bytes + 1, QLatin1Char('a')));
            first.insert("data", data);
            values[0] = first;
            break;
        case 12:
            values[0] = QJsonObject{{"type", "error"}, {"error", "private"}, {"extra", true}};
            break;
        case 13:
            values[0] = QJsonObject{{"type", "error"}, {"error", 1}};
            break;
        case 14:
            values[0] = QJsonObject{{"type", "error"},
                                    {"error", QString(youtube_broker_max_value_bytes + 1, QLatin1Char('x'))}};
            break;
        case 15:
            values[0] = QJsonObject{{"type", "result"}, {"data", QJsonArray{}}};
            break;
        case 16: {
            const QJsonValue temporary = values[0];
            values[0] = values[1];
            values[1] = temporary;
            break;
        }
        }
        if (mode >= 3)
            root.insert("responses", values);
        QVERIFY(!encodeYoutubeBrokerResponse(1, challenge(), json(root)));
    }
    void rawNodeOutputUsesTheSameStrictJsonBoundary() {
        QVERIFY(!encodeYoutubeBrokerResponse(1, challenge(), QByteArray("\xc0\xaf", 2)));
        QVERIFY(!encodeYoutubeBrokerResponse(1, challenge(),
                                             QByteArray{"{\"type\":\"result\",\"type\":\"result\",\"responses\":[]}"}));
        QVERIFY(!encodeYoutubeBrokerResponse(1, challenge(), nodeOutput() + "{}"));
        QVERIFY(!encodeYoutubeBrokerResponse(1, challenge(), QByteArray(youtube_broker_max_response_bytes + 1, 'x')));
    }
    void normalizedResponseJsonChecksItsExactOneMiBBoundary_data() {
        QTest::addColumn<bool>("excess");
        QTest::newRow("exactly 1 MiB") << false;
        QTest::newRow("one byte too many") << true;
    }
    void normalizedResponseJsonChecksItsExactOneMiBBoundary() {
        QFETCH(bool, excess);
        auto value = challenge();
        value.requests.clear();
        QJsonArray values;
        for (std::size_t index = 0; index < youtube_broker_max_requests; ++index) {
            const auto key = QStringLiteral("key%1").arg(index);
            value.requests.push_back({QStringLiteral("n"), {key}});
            values.push_back(
                QJsonObject{{"type", "result"},
                            {"data", QJsonObject{{key, QString(youtube_broker_max_value_bytes, QLatin1Char('x'))}}}});
        }
        const auto target = youtube_broker_max_response_bytes + (excess ? 1 : 0);
        const auto remove = json(responseMessage(1, values)).size() - target;
        QVERIFY(remove > 0 && remove < youtube_broker_max_value_bytes);
        const auto key = value.requests.back().challenges.front();
        values[values.size() - 1] = QJsonObject{
            {"type", "result"},
            {"data", QJsonObject{{key, QString(youtube_broker_max_value_bytes - remove, QLatin1Char('x'))}}}};
        QCOMPARE(json(responseMessage(1, values)).size(), target);
        QVERIFY(nodeOutput(values).size() <= youtube_broker_max_response_bytes);
        const auto encoded = encodeYoutubeBrokerResponse(1, value, nodeOutput(values));
        if (excess)
            QVERIFY(!encoded);
        else {
            QVERIFY(encoded);
            QCOMPARE(encoded->size(), youtube_broker_max_response_bytes + 4);
        }
    }
    void nodeProgramPassesHostilePlayerAndChallengesAsSerializedDataInResourceOrder() {
        auto value = challenge();
        value.player = QStringLiteral("\"}); process.exit(9); //\n\\tail 日本\u2028\u2029");
        value.requests.front().challenges = {QStringLiteral("'); globalThis.pwned=true; //\n\\\"日本")};
        const auto program = youtubeBrokerNodeInput(value, core, library);
        QVERIFY(program);
        const auto library_at = program->indexOf(library), core_at = program->indexOf(core);
        auto assignment_at = program->indexOf("Object.assign(globalThis,lib)");
        if (assignment_at < 0)
            assignment_at = program->indexOf("Object.assign(globalThis, lib)");
        const QByteArray marker{"process.stdout.write(JSON.stringify(jsc("};
        const auto data_at = program->lastIndexOf(marker) + marker.size();
        const auto end = program->lastIndexOf(")))");
        QVERIFY(library_at >= 0 && assignment_at > library_at && core_at > assignment_at);
        QVERIFY(data_at >= marker.size() && end > data_at && core_at < data_at);
        const auto suffix = program->mid(end).trimmed();
        QVERIFY(suffix == QByteArray{")))"} || suffix == QByteArray{")));"});
        const auto data = QJsonDocument::fromJson(program->mid(data_at, end - data_at));
        QVERIFY(data.isObject());
        QCOMPARE(data.object(), QJsonObject({{"type", "player"},
                                             {"player", value.player},
                                             {"requests", requests(value)},
                                             {"output_preprocessed", false}}));
    }
    void nodeProgramAcceptsLargePlayerAndExactResourceSizesWithoutRunningNode() {
        auto value = challenge();
        value.player = QString(300 * 1024, QLatin1Char('p'));
        const auto large_core = script(core, youtube_broker_max_script_bytes);
        const auto large_library = script(library, youtube_broker_max_script_bytes);
        QCOMPARE(large_core.size(), youtube_broker_max_script_bytes);
        QCOMPARE(large_library.size(), youtube_broker_max_script_bytes);
        const auto program = youtubeBrokerNodeInput(value, large_core, large_library);
        QVERIFY(program);
        QVERIFY(program->size() > 256 * 1024);
        QVERIFY(program->size() <= youtube_broker_max_frame_bytes);
    }
    void nodeResourcesAndConstructedProgramAreStrictlyBounded_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode < 7; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void nodeResourcesAndConstructedProgramAreStrictlyBounded() {
        QFETCH(int, mode);
        auto value = challenge();
        auto source_core = core, source_library = library;
        switch (mode) {
        case 0:
            source_core.clear();
            break;
        case 1:
            source_library = QByteArray{" \n\t\r"};
            break;
        case 2:
            source_core = QByteArray("\xc0\xaf", 2);
            break;
        case 3:
            source_library = QByteArray("\xed\xa0\x80", 3);
            break;
        case 4:
            source_core = QByteArray(youtube_broker_max_script_bytes + 1, 'x');
            break;
        case 5:
            source_library = QByteArray(youtube_broker_max_script_bytes + 1, 'x');
            break;
        case 6:
            value.player = QString(youtube_broker_max_player_bytes, QLatin1Char('"'));
            break;
        }
        QVERIFY(!youtubeBrokerNodeInput(value, source_core, source_library));
    }
    void frameReaderHandlesEveryHeaderAndBodySplitAndResetsAfterTake() {
        const QByteArray body{"synthetic frame body"};
        const auto encoded = frame(body);
        for (qsizetype split = 1; split < encoded.size(); ++split) {
            YoutubeBrokerFrameReader reader;
            QVERIFY(reader.append(QByteArrayView(encoded).first(split)));
            QVERIFY(!reader.failed());
            QVERIFY(!reader.take());
            QVERIFY(reader.append(QByteArrayView(encoded).sliced(split)));
            const auto result = reader.take();
            QVERIFY(result);
            QCOMPARE(*result, body);
            QVERIFY(!reader.take());
            QVERIFY(reader.append(frame(QByteArrayLiteral("second"))));
            QCOMPARE(reader.take(), std::optional<QByteArray>{QByteArrayLiteral("second")});
            QVERIFY(reader.finish());
        }
    }
    void impossibleFrameLengthsFailWhenTheFourthPrefixByteArrives_data() {
        QTest::addColumn<quint32>("length");
        QTest::newRow("zero") << quint32{0};
        QTest::newRow("over 16 MiB") << static_cast<quint32>(youtube_broker_max_frame_bytes + 1);
        QTest::newRow("uint32 max") << std::numeric_limits<quint32>::max();
    }
    void impossibleFrameLengthsFailWhenTheFourthPrefixByteArrives() {
        QFETCH(quint32, length);
        YoutubeBrokerFrameReader reader;
        const auto header = prefix(length);
        QVERIFY(reader.append(QByteArrayView(header).first(3)));
        QVERIFY(!reader.failed());
        QVERIFY(!reader.append(QByteArrayView(header).sliced(3)));
        QVERIFY(reader.failed());
        QVERIFY(!reader.take());
        QVERIFY(!reader.append(frame(QByteArrayLiteral("later"))));
        QVERIFY(!reader.finish());
    }
    void readerRejectsTrailingAndPipelinedFramesWithoutPublishingTheFirst_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode < 4; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void readerRejectsTrailingAndPipelinedFramesWithoutPublishingTheFirst() {
        QFETCH(int, mode);
        YoutubeBrokerFrameReader reader;
        const auto first = frame(QByteArrayLiteral("first")), next = frame(QByteArrayLiteral("next"));
        if (mode < 2)
            QVERIFY(!reader.append(mode == 0 ? first + 'x' : first + next));
        else {
            QVERIFY(reader.append(first));
            QVERIFY(!reader.append(mode == 2 ? QByteArray{"x"} : next));
        }
        QVERIFY(reader.failed());
        QVERIFY(!reader.take());
        QVERIFY(!reader.finish());
    }
    void finishRejectsPartialInputPermanentlyButPreservesCompleteUntakenFrame() {
        const auto encoded = frame(QByteArrayLiteral("payload"));
        for (qsizetype bytes = 1; bytes < encoded.size(); ++bytes) {
            YoutubeBrokerFrameReader reader;
            QVERIFY(reader.append(QByteArrayView(encoded).first(bytes)));
            QVERIFY(!reader.finish());
            QVERIFY(reader.failed());
            QVERIFY(!reader.append(QByteArrayView(encoded).sliced(bytes)));
            QVERIFY(!reader.take());
        }
        YoutubeBrokerFrameReader complete;
        QVERIFY(complete.append(encoded));
        QVERIFY(complete.finish());
        QVERIFY(!complete.failed());
        QCOMPARE(complete.take(), std::optional<QByteArray>{QByteArrayLiteral("payload")});
        QVERIFY(!complete.append(QByteArrayView{}));
        QVERIFY(complete.failed());
        YoutubeBrokerFrameReader empty;
        QVERIFY(empty.finish());
        QVERIFY(!empty.append(QByteArrayView{}));
    }
    void readerOwnsRawInputCopiesAndAcceptsTheExactFrameCeiling() {
        const QByteArray original{"raw\0payload", 11};
        auto backing = frame(original);
        const auto raw = QByteArray::fromRawData(backing.data(), backing.size());
        YoutubeBrokerFrameReader reader;
        QVERIFY(reader.append(raw));
        backing.fill('X');
        QCOMPARE(reader.take(), std::optional<QByteArray>{original});
        const QByteArray maximum(youtube_broker_max_frame_bytes, 'x');
        QVERIFY(reader.append(frame(maximum)));
        const auto result = reader.take();
        QVERIFY(result);
        QCOMPARE(result->size(), youtube_broker_max_frame_bytes);
        QCOMPARE(*result, maximum);
        QVERIFY(reader.finish());
    }
};
QTEST_GUILESS_MAIN(YoutubeBrokerProtocolTests)
#include "YoutubeBrokerProtocolTests.moc"
