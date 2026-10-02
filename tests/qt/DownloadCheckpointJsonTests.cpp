#include "adapters/DownloadCheckpointJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <limits>

using namespace Qt::StringLiterals;

namespace music_player::m0::test {
namespace {

[[nodiscard]] std::filesystem::path nativePath(const QString& text) {
#ifdef _WIN32
    return std::filesystem::path{text.toStdWString()};
#else
    return std::filesystem::path{text.toUtf8().toStdString()};
#endif
}

[[nodiscard]] QString candidateText() {
#ifdef _WIN32
    return uR"(C:\Música com espaços\日本 ☕.mp3)"_s;
#else
    return u"/Música com espaços/日本 ☕.mp3"_s;
#endif
}

[[nodiscard]] DownloadCheckpoint sample() {
    return {
        .receipts = {{.request_id = "12345678-1234-4234-8234-123456789abc",
                      .fingerprint = std::string(64, 'a'),
                      .job_ids = {"job-a", "job-b"},
                      .retained_request_bytes = 128}},
        .jobs = {{.job_id = "job-a",
                  .title = "Abertura 日本 ☕",
                  .state = DownloadJobState::Completed,
                  .completed_units = 1,
                  .total_units = 1,
                  .completed_mp3 = nativePath(candidateText())},
                 {.job_id = "job-b",
                  .title = "Falhou",
                  .state = DownloadJobState::Failed,
                  .completed_units = std::numeric_limits<std::uint64_t>::max(),
                  .error = DownloadFailure::LimitExceeded}},
    };
}

[[nodiscard]] QJsonObject validObject() {
    return {
        {u"schemaVersion"_s, 1},
        {u"owner"_s, u"rc-music-download-checkpoint"_s},
        {u"receipts"_s, QJsonArray{QJsonObject{{u"requestId"_s, u"12345678-1234-4234-8234-123456789abc"_s},
                                               {u"fingerprint"_s, QString{64, QChar{'a'}}},
                                               {u"jobIds"_s, QJsonArray{u"job-a"_s, u"job-b"_s}},
                                               {u"retainedRequestBytes"_s, u"128"_s}}}},
        {u"jobs"_s, QJsonArray{QJsonObject{{u"jobId"_s, u"job-a"_s},
                                           {u"title"_s, u"Abertura 日本 ☕"_s},
                                           {u"state"_s, u"completed"_s},
                                           {u"completedUnits"_s, u"1"_s},
                                           {u"totalUnits"_s, u"1"_s},
                                           {u"completedMp3"_s, candidateText()},
                                           {u"error"_s, u"none"_s}},
                               QJsonObject{{u"jobId"_s, u"job-b"_s},
                                           {u"title"_s, u"Falhou"_s},
                                           {u"state"_s, u"failed"_s},
                                           {u"completedUnits"_s, u"18446744073709551615"_s},
                                           {u"totalUnits"_s, QJsonValue::Null},
                                           {u"completedMp3"_s, u""_s},
                                           {u"error"_s, u"limit-exceeded"_s}}}},
    };
}

[[nodiscard]] QByteArray bytes(const QJsonObject& object) {
    return QJsonDocument{object}.toJson(QJsonDocument::Compact);
}

[[nodiscard]] QJsonObject change(const QString& array_key, const QString& field, const QJsonValue& value) {
    auto root = validObject();
    auto array = root.value(array_key).toArray();
    auto object = array.at(0).toObject();
    object.insert(field, value);
    array.replace(0, object);
    root.insert(array_key, array);
    return root;
}

[[nodiscard]] QSet<QString> keys(const QJsonObject& object) {
    const auto names = object.keys();
    return QSet<QString>{names.begin(), names.end()};
}

[[nodiscard]] DownloadRequest request() {
    return {.request_id = "12345678-1234-4234-8234-123456789abc",
            .items = {{.url = "https://example.org/a.mp3", .title = "A", .kind = DownloadMediaKind::Audio}}};
}

} // namespace

class DownloadCheckpointJsonTests final : public QObject {
    Q_OBJECT

  private slots:
    void strictPrivateRoundTripPreservesNativeUnicodeAndUint64() {
        const auto expected = sample();
        const auto first = DownloadCheckpointJson::encode(expected);
        QVERIFY(!first.isEmpty());
        QCOMPARE(DownloadCheckpointJson::encode(expected), first);
        const auto object = QJsonDocument::fromJson(first).object();
        QCOMPARE(keys(object), (QSet<QString>{u"schemaVersion"_s, u"owner"_s, u"receipts"_s, u"jobs"_s}));
        QCOMPARE(object.value(u"owner"_s).toString(), u"rc-music-download-checkpoint"_s);
        const auto job = object.value(u"jobs"_s).toArray().at(1).toObject();
        QCOMPARE(job.value(u"completedUnits"_s), QJsonValue{u"18446744073709551615"_s});
        QVERIFY(job.value(u"totalUnits"_s).isNull());
        const auto receipt = object.value(u"receipts"_s).toArray().at(0).toObject();
        QVERIFY(receipt.value(u"retainedRequestBytes"_s).isString());
        QCOMPARE(keys(receipt),
                 (QSet<QString>{u"requestId"_s, u"fingerprint"_s, u"jobIds"_s, u"retainedRequestBytes"_s}));
        QCOMPARE(keys(job), (QSet<QString>{u"jobId"_s, u"title"_s, u"state"_s, u"completedUnits"_s, u"totalUnits"_s,
                                           u"completedMp3"_s, u"error"_s}));
        const auto decoded = DownloadCheckpointJson::decode(first);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
        QVERIFY(decoded.checkpoint.has_value());
        QCOMPARE(*decoded.checkpoint, expected);
        QCOMPARE(DownloadCheckpointJson::encode(*decoded.checkpoint), first);
        QVERIFY(!first.contains("https://"));
        QVERIFY(!first.contains("sourceUrl"));
        QVERIFY(!first.contains("query"));
    }

    void emptyCheckpointRoundTrips() {
        const DownloadCheckpoint empty;
        const auto encoded = DownloadCheckpointJson::encode(empty);
        QVERIFY(!encoded.isEmpty());
        const auto decoded = DownloadCheckpointJson::decode(encoded);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
        QCOMPARE(decoded.checkpoint, std::optional<DownloadCheckpoint>{empty});
    }

    void importingStateIsClosedRoundTripsAndRejectsMissingFinalEvidence() {
        auto checkpoint = sample();
        checkpoint.jobs[0].state = DownloadJobState::Importing;
        const auto encoded = DownloadCheckpointJson::encode(checkpoint);
        QVERIFY(encoded.contains("\"state\":\"importing\""));
        const auto decoded = DownloadCheckpointJson::decode(encoded);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
        QCOMPARE(decoded.checkpoint, std::optional<DownloadCheckpoint>{checkpoint});
        checkpoint.jobs[0].completed_mp3.clear();
        QVERIFY(DownloadCheckpointJson::encode(checkpoint).isEmpty());
        auto root = QJsonDocument::fromJson(encoded).object();
        auto jobs = root.value(u"jobs"_s).toArray();
        auto job = jobs.at(0).toObject();
        job.insert(u"completedMp3"_s, QString{});
        jobs[0] = job;
        root.insert(u"jobs"_s, jobs);
        QCOMPARE(DownloadCheckpointJson::decode(bytes(root)).status, DownloadCheckpointJsonStatus::Invalid);
    }

    void importedHistoryRoundTripsWithoutFabricatingCompletedProgress() {
        auto checkpoint = sample();
        auto& job = checkpoint.jobs[0];
        job.state = DownloadJobState::Imported;
        job.completed_units = 2;
        job.total_units = 7;
        const auto encoded = DownloadCheckpointJson::encode(checkpoint);
        QVERIFY(encoded.contains("\"state\":\"imported\""));
        const auto decoded = DownloadCheckpointJson::decode(encoded);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
        QCOMPARE(decoded.checkpoint, std::optional<DownloadCheckpoint>{checkpoint});
        auto root = QJsonDocument::fromJson(encoded).object();
        auto invalid = root;
        auto jobs = invalid.value(u"jobs"_s).toArray();
        auto malformed = jobs[0].toObject();
        malformed.insert(u"completedMp3"_s, QString{});
        jobs[0] = malformed;
        invalid.insert(u"jobs"_s, jobs);
        QCOMPARE(DownloadCheckpointJson::decode(bytes(invalid)).status, DownloadCheckpointJsonStatus::Invalid);
        checkpoint.jobs[0].completed_mp3.clear();
        QVERIFY(DownloadCheckpointJson::encode(checkpoint).isEmpty());
        job.completed_mp3 = nativePath(candidateText());
        job.error = DownloadFailure::Import;
        QVERIFY(DownloadCheckpointJson::encode(checkpoint).isEmpty());
        job.error = DownloadFailure::None;
        job.completed_units = 8;
        QVERIFY(DownloadCheckpointJson::encode(checkpoint).isEmpty());
    }

    void roundTripsAllClosedStatesAndErrors() {
        const DownloadJobState states[]{DownloadJobState::Queued,       DownloadJobState::Downloading,
                                        DownloadJobState::Converting,   DownloadJobState::Publishing,
                                        DownloadJobState::Cancelling,   DownloadJobState::Completed,
                                        DownloadJobState::Cancelled,    DownloadJobState::Failed,
                                        DownloadJobState::ImportFailed, DownloadJobState::Interrupted};
        for (const auto state : states) {
            auto checkpoint = sample();
            auto& job = checkpoint.jobs[0];
            job.state = state;
            job.completed_mp3.clear();
            job.error = DownloadFailure::None;
            if (state == DownloadJobState::Completed || state == DownloadJobState::ImportFailed)
                job.completed_mp3 = nativePath(candidateText());
            if (state == DownloadJobState::ImportFailed)
                job.error = DownloadFailure::Import;
            if (state == DownloadJobState::Failed || state == DownloadJobState::Interrupted)
                job.error = DownloadFailure::Unavailable;
            const auto decoded = DownloadCheckpointJson::decode(DownloadCheckpointJson::encode(checkpoint));
            QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
            QCOMPARE(decoded.checkpoint, std::optional<DownloadCheckpoint>{checkpoint});
        }
        for (int code = static_cast<int>(DownloadFailure::Unavailable);
             code <= static_cast<int>(DownloadFailure::InvalidResult); ++code) {
            auto checkpoint = sample();
            checkpoint.jobs[1].error = static_cast<DownloadFailure>(code);
            const auto decoded = DownloadCheckpointJson::decode(DownloadCheckpointJson::encode(checkpoint));
            QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
            QCOMPARE(decoded.checkpoint, std::optional<DownloadCheckpoint>{checkpoint});
        }
    }

    void futureSchemaIsDistinguishedBeforeVersionOneFields_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("version-only") << QByteArray{R"({"schemaVersion":2})"};
        QTest::newRow("foreign-fields") << QByteArray{R"({"schemaVersion":99,"jobs":false,"owner":7})"};
        QTest::newRow("large-future") << QByteArray{R"({"schemaVersion":2147483648})"};
    }

    void futureSchemaIsDistinguishedBeforeVersionOneFields() {
        QFETCH(QByteArray, json);
        const auto decoded = DownloadCheckpointJson::decode(json);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::FutureVersion);
        QVERIFY(!decoded.checkpoint.has_value());
    }

    void rejectsMalformedUnknownKeysTypesAndOwners_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("truncated") << QByteArray{R"({"schemaVersion":1)"};
        QTest::newRow("array") << QByteArray{"[]"};
        QTest::newRow("version-string") << QByteArray{R"({"schemaVersion":"1"})"};
        QTest::newRow("version-zero") << QByteArray{R"({"schemaVersion":0})"};
        QTest::newRow("version-fraction") << QByteArray{R"({"schemaVersion":1.5})"};
        QTest::newRow("duplicate-schema") << QByteArray{R"({"schemaVersion":1,"schemaVersion":2})"};
        auto root = validObject();
        root.insert(u"owner"_s, u"not-rc-music"_s);
        QTest::newRow("foreign-owner") << bytes(root);
        root = validObject();
        root.insert(u"sourceUrl"_s, u"https://example.org/private?token=secret"_s);
        QTest::newRow("source-url-root") << bytes(root);
        auto repeated = bytes(validObject());
        repeated.replace("\"owner\":", "\"owner\":\"rc-music-download-checkpoint\",\"\\u006fwner\":");
        QTest::newRow("duplicate-escaped-owner") << repeated;
        repeated = bytes(validObject());
        repeated.replace("\"completedUnits\":\"1\"", "\"completedUnits\":\"0\",\"completedUnits\":\"1\"");
        QTest::newRow("duplicate-nested-field") << repeated;
        QTest::newRow("source-url-job") << bytes(change(u"jobs"_s, u"sourceUrl"_s, u"secret"_s));
        QTest::newRow("source-url-receipt") << bytes(change(u"receipts"_s, u"sourceUrl"_s, u"secret"_s));
        QTest::newRow("unknown-state") << bytes(change(u"jobs"_s, u"state"_s, u"published"_s));
        QTest::newRow("unknown-error") << bytes(change(u"jobs"_s, u"error"_s, u"arbitrary"_s));
        QTest::newRow("state-number") << bytes(change(u"jobs"_s, u"state"_s, 0));
        QTest::newRow("error-number") << bytes(change(u"jobs"_s, u"error"_s, 0));
        QTest::newRow("path-null") << bytes(change(u"jobs"_s, u"completedMp3"_s, QJsonValue::Null));
        QTest::newRow("title-array") << bytes(change(u"jobs"_s, u"title"_s, QJsonArray{}));
        QTest::newRow("receipt-ids-object") << bytes(change(u"receipts"_s, u"jobIds"_s, QJsonObject{}));
        root = validObject();
        auto jobs = root.value(u"jobs"_s).toArray();
        jobs.replace(0, true);
        root.insert(u"jobs"_s, jobs);
        QTest::newRow("job-not-object") << bytes(root);
        root = validObject();
        root.insert(u"receipts"_s, QJsonObject{});
        QTest::newRow("receipts-not-array") << bytes(root);
        for (const auto& name : validObject().keys()) {
            root = validObject();
            root.remove(name);
            QTest::newRow(("missing-" + name).toUtf8().constData()) << bytes(root);
        }
        for (const auto& array_key : {u"receipts"_s, u"jobs"_s}) {
            const auto names = validObject().value(array_key).toArray().at(0).toObject().keys();
            for (const auto& name : names) {
                root = validObject();
                auto array = root.value(array_key).toArray();
                auto object = array.at(0).toObject();
                object.remove(name);
                array.replace(0, object);
                root.insert(array_key, array);
                QTest::newRow(("missing-" + array_key + "-" + name).toUtf8().constData()) << bytes(root);
            }
        }
        QTest::newRow("invalid-utf8") << QByteArray{"{\"schemaVersion\":2,\"x\":\"\xc0\xaf\"}"};
        repeated = bytes(validObject());
        repeated.replace(QString{u"Abertura 日本 ☕"}.toUtf8(), "\\ud800");
        QTest::newRow("lone-high-surrogate") << repeated;
        repeated = bytes(validObject());
        repeated.replace(QString{u"Abertura 日本 ☕"}.toUtf8(), "\\udc00");
        QTest::newRow("lone-low-surrogate") << repeated;
        const QByteArray deeply_nested = QByteArray{"{\"schemaVersion\":2,\"x\":"} + QByteArray(65, '[') +
                                         QByteArray{"0"} + QByteArray(65, ']') + QByteArray{"}"};
        QTest::newRow("excessive-json-depth") << deeply_nested;
    }

    void rejectsMalformedUnknownKeysTypesAndOwners() {
        QFETCH(QByteArray, json);
        const auto decoded = DownloadCheckpointJson::decode(json);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Invalid);
        QVERIFY(!decoded.checkpoint.has_value());
    }

    void rejectsNonCanonicalOrNumericUint64_data() {
        QTest::addColumn<QByteArray>("json");
        const QStringList bad{u""_s,
                              u"00"_s,
                              u"01"_s,
                              u"+1"_s,
                              u"-1"_s,
                              u" 1"_s,
                              u"1 "_s,
                              u"1.0"_s,
                              u"1e3"_s,
                              u"18446744073709551616"_s,
                              QString{QChar{0x0661}}};
        int row = 0;
        for (const auto& value : bad) {
            QTest::newRow(("completed-" + QByteArray::number(row)).constData())
                << bytes(change(u"jobs"_s, u"completedUnits"_s, value));
            QTest::newRow(("retained-" + QByteArray::number(row++)).constData())
                << bytes(change(u"receipts"_s, u"retainedRequestBytes"_s, value));
        }
        QTest::newRow("completed-double") << bytes(change(u"jobs"_s, u"completedUnits"_s, 1));
        QTest::newRow("total-double") << bytes(change(u"jobs"_s, u"totalUnits"_s, 1));
        QTest::newRow("retained-double") << bytes(change(u"receipts"_s, u"retainedRequestBytes"_s, 128));
    }

    void rejectsNonCanonicalOrNumericUint64() {
        QFETCH(QByteArray, json);
        QCOMPARE(DownloadCheckpointJson::decode(json).status, DownloadCheckpointJsonStatus::Invalid);
    }

    void appliesSemanticLimitsBeforeEncodingAndAfterDecoding() {
        auto invalid = sample();
        invalid.jobs[0].job_id = std::string(65, 'a');
        QVERIFY(DownloadCheckpointJson::encode(invalid).isEmpty());
        QCOMPARE(DownloadCheckpointJson::decode(bytes(change(u"jobs"_s, u"jobId"_s, QString{65, QChar{'a'}}))).status,
                 DownloadCheckpointJsonStatus::Invalid);
        invalid = sample();
        invalid.jobs[0].state = static_cast<DownloadJobState>(42);
        QVERIFY(DownloadCheckpointJson::encode(invalid).isEmpty());
        invalid = sample();
        invalid.jobs[0].error = static_cast<DownloadFailure>(42);
        QVERIFY(DownloadCheckpointJson::encode(invalid).isEmpty());
        invalid = sample();
        invalid.receipts[0].retained_request_bytes = download_checkpoint_max_bytes + 1;
        QVERIFY(DownloadCheckpointJson::encode(invalid).isEmpty());
        QCOMPARE(DownloadCheckpointJson::decode(bytes(change(u"receipts"_s, u"retainedRequestBytes"_s, u"1048577"_s)))
                     .status,
                 DownloadCheckpointJsonStatus::Invalid);
        invalid = sample();
        invalid.jobs[0].title = std::string{"\xc0\xaf"};
        QVERIFY(DownloadCheckpointJson::encode(invalid).isEmpty());
        auto root = validObject();
        auto jobs = root.value(u"jobs"_s).toArray();
        while (jobs.size() <= static_cast<qsizetype>(download_checkpoint_max_jobs))
            jobs.append(jobs.at(0));
        root.insert(u"jobs"_s, jobs);
        QCOMPARE(DownloadCheckpointJson::decode(bytes(root)).status, DownloadCheckpointJsonStatus::Invalid);
    }

    void exactDocumentBoundaryIsInclusive() {
        auto document = bytes(validObject());
        document.append(DownloadCheckpointJson::max_document_bytes - document.size(), ' ');
        QCOMPARE(DownloadCheckpointJson::decode(document).status, DownloadCheckpointJsonStatus::Valid);
        document.append(' ');
        QCOMPARE(DownloadCheckpointJson::decode(document).status, DownloadCheckpointJsonStatus::Invalid);
    }

    void depthBoundaryCountsContainersNotScalarValues() {
        const auto nested = [](const qsizetype arrays, const QByteArray& scalar) {
            return QByteArray{"{\"schemaVersion\":2,\"x\":"} + QByteArray(arrays, '[') + scalar +
                   QByteArray(arrays, ']') + QByteArray{"}"};
        };
        // The root object counts as one container; scalar values do not count.
        QCOMPARE(DownloadCheckpointJson::decode(nested(63, {})).status, DownloadCheckpointJsonStatus::FutureVersion);
        QCOMPARE(DownloadCheckpointJson::decode(nested(63, "0")).status, DownloadCheckpointJsonStatus::FutureVersion);
        QCOMPARE(DownloadCheckpointJson::decode(nested(64, {})).status, DownloadCheckpointJsonStatus::Invalid);
    }

    void requestFingerprintMatchesIndependentTypedSha256Vectors() {
        QtDownloadRequestIdentity identity;
        // Independently hashed literal hex: prefix + NUL + uint64BE count + kind + length/data pairs.
        auto audio = request();
        QCOMPARE(identity.fingerprint(audio),
                 std::string{"a44f3f0135066fbc8afbe02fd96cc25202d5b978985afddc20aef8d0916f63f4"});
        auto ordered = audio;
        ordered.items.insert(ordered.items.begin(),
                             {.url = "https://example.org/v.mp4", .title = "日本", .kind = DownloadMediaKind::Video});
        QCOMPARE(identity.fingerprint(ordered),
                 std::string{"c78767ac6e84df9263cf3e407ee9955e0c15f445bfee9cb793e41e2867b6fc5f"});
        audio.request_id = "an intentionally unrelated UUID";
        QCOMPARE(identity.fingerprint(audio), identity.fingerprint(request()));
    }

    void typedFramingSeparatesKindsOrderAndConcatenation() {
        QtDownloadRequestIdentity identity;
        const auto initial = request();
        const auto fingerprint = identity.fingerprint(initial);
        QVERIFY(fingerprint.size() == 64);
        auto changed = initial;
        changed.items[0].kind = DownloadMediaKind::Video;
        QVERIFY(identity.fingerprint(changed) != fingerprint);
        changed = initial;
        changed.items[0].url += 'b';
        changed.items[0].title = "c";
        auto other = initial;
        other.items[0].title = "bc";
        QVERIFY(identity.fingerprint(changed) != identity.fingerprint(other));
        changed.items.push_back(initial.items[0]);
        const auto ordered = identity.fingerprint(changed);
        std::reverse(changed.items.begin(), changed.items.end());
        QVERIFY(identity.fingerprint(changed) != ordered);
    }

    void invalidItemsNeverGetLossyOrUnboundedFingerprint() {
        QtDownloadRequestIdentity identity;
        auto invalid = request();
        invalid.items.clear();
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = request();
        invalid.items[0].kind = static_cast<DownloadMediaKind>(42);
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = request();
        invalid.items[0].url = std::string{"\xc0\xaf"};
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = request();
        invalid.items[0].title = std::string{"a\0b", 3};
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = request();
        invalid.items[0].url = std::string(8193, 'a');
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = request();
        invalid.items[0].title = std::string(801, 'a');
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = request();
        while (invalid.items.size() <= 20)
            invalid.items.push_back(invalid.items[0]);
        QVERIFY(identity.fingerprint(invalid).empty());
        auto exact = request();
        exact.items[0].url = std::string(8192, 'a');
        exact.items[0].title = std::string(800, 'b');
        while (exact.items.size() < 20)
            exact.items.push_back(exact.items[0]);
        QCOMPARE(identity.fingerprint(exact).size(), std::size_t{64});
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::DownloadCheckpointJsonTests)
#include "DownloadCheckpointJsonTests.moc"
