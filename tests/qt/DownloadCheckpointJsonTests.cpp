#include "adapters/DownloadCheckpointJson.hpp"
#include "m0/application/DownloadService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

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

[[nodiscard]] DownloadRequest youtubeRequest() {
    return {.request_id = "12345678-1234-4234-8234-123456789abc",
            .items = {{.url = "https://www.youtube.com/watch?v=AbCdEf_12-3",
                       .title = "Synthetic 日本",
                       .kind = DownloadMediaKind::Audio,
                       .youtube = YoutubeAudioItem{.video_id = "AbCdEf_12-3",
                                                   .title = "Synthetic 日本",
                                                   .artist = "Artist café",
                                                   .lyrics_page = "https://letras.mus.br/synthetic/opening/",
                                                   .fetch_lyrics = true}}}};
}

struct NoExecution final : DownloadExecution {
    unsigned int starts{};
    bool start(const std::string&, const DownloadItem&) override {
        ++starts;
        return false;
    }
    void cancel(std::string_view) override {}
    DownloadExecutionUpdate poll(std::string_view) override { return {}; }
};

struct NoImport final : DownloadImport {
    bool accept(const std::string&, const std::filesystem::path&) override { return false; }
};

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
        QCOMPARE(object.value(u"schemaVersion"_s), QJsonValue{1});
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

    void versionTwoRoundTripsArchiveWithClosedReceiptShape() {
        auto root = validObject();
        root.insert(u"schemaVersion"_s, 2);
        const QJsonObject archive{{u"requestId"_s, u"12345678-1234-4234-8234-123456789abd"_s},
                                  {u"fingerprint"_s, QString{64, QChar{'b'}}},
                                  {u"jobIds"_s, QJsonArray{u"archive-a"_s, u"archive-b"_s}}};
        root.insert(u"archivedReceipts"_s, QJsonArray{archive});
        const auto decoded = DownloadCheckpointJson::decode(bytes(root));
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
        QVERIFY(decoded.checkpoint);
        const std::vector<DownloadArchivedRequest> expected{
            {"12345678-1234-4234-8234-123456789abd", std::string(64, 'b'), {"archive-a", "archive-b"}}};
        QCOMPARE(decoded.checkpoint->archived_receipts, expected);
        QCOMPARE(DownloadCheckpointJson::encode(*decoded.checkpoint), bytes(root));

        auto wrong_version = root;
        wrong_version.insert(u"schemaVersion"_s, 1);
        QCOMPARE(DownloadCheckpointJson::decode(bytes(wrong_version)).status, DownloadCheckpointJsonStatus::Invalid);

        auto missing_archive = root;
        missing_archive.remove(u"archivedReceipts"_s);
        QCOMPARE(DownloadCheckpointJson::decode(bytes(missing_archive)).status, DownloadCheckpointJsonStatus::Invalid);

        for (const auto& field : {u"retainedRequestBytes"_s, u"sourceUrl"_s, u"completedMp3"_s}) {
            auto with_extra = archive;
            with_extra.insert(field, u"private"_s);
            root.insert(u"archivedReceipts"_s, QJsonArray{with_extra});
            QCOMPARE(DownloadCheckpointJson::decode(bytes(root)).status, DownloadCheckpointJsonStatus::Invalid);
        }
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

    void futureSchemaIsDistinguishedBeforeKnownVersionFields_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("version-only") << QByteArray{R"({"schemaVersion":3})"};
        QTest::newRow("foreign-fields") << QByteArray{R"({"schemaVersion":99,"jobs":false,"owner":7})"};
        QTest::newRow("large-future") << QByteArray{R"({"schemaVersion":2147483648})"};
    }

    void futureSchemaIsDistinguishedBeforeKnownVersionFields() {
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
            return QByteArray{"{\"schemaVersion\":3,\"x\":"} + QByteArray(arrays, '[') + scalar +
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

    void youtubeFingerprintSeparatesEveryFieldPresenceOrderAndFraming() {
        QtDownloadRequestIdentity identity;
        // Independent Python hashlib/struct vector: 150 bytes, domain NUL,
        // uint64BE count/lengths, audio byte and explicit lyrics-choice byte.
        const DownloadRequest stable{.request_id = "01234567-89ab-4cde-8fab-0123456789ab",
                                     .items = {{.url = "https://www.youtube.com/watch?v=aB0-_Cd1EF2",
                                                .title = "T",
                                                .kind = DownloadMediaKind::Audio,
                                                .youtube = YoutubeAudioItem{"aB0-_Cd1EF2", "T", "A", "", true}}}};
        QCOMPARE(identity.fingerprint(stable),
                 std::string("1ef716f29b85a9a66b6738168cb3f9e32675cd964178b1831f983bf4a562ec4f"));
        const auto original = youtubeRequest();
        const auto fingerprint = identity.fingerprint(original);
        QCOMPARE(fingerprint.size(), std::size_t{64});
        auto absent = original;
        absent.items.front().youtube.reset();
        QVERIFY(identity.fingerprint(absent) != fingerprint);
        auto changed = original;
        changed.items.front().youtube->artist += " changed";
        QVERIFY(identity.fingerprint(changed) != fingerprint);
        changed = original;
        changed.items.front().youtube->lyrics_page += "alternate/";
        QVERIFY(identity.fingerprint(changed) != fingerprint);
        changed = original;
        changed.items.front().youtube->fetch_lyrics = false;
        QVERIFY(identity.fingerprint(changed) != fingerprint);
        changed = original;
        changed.items.front().youtube->video_id = "ZbCdEf_12-3";
        changed.items.front().url = "https://www.youtube.com/watch?v=ZbCdEf_12-3";
        QVERIFY(identity.fingerprint(changed) != fingerprint);
        changed = original;
        changed.items.front().title += " changed";
        changed.items.front().youtube->title = changed.items.front().title;
        QVERIFY(identity.fingerprint(changed) != fingerprint);
        auto first = original;
        first.items.front().youtube->artist = "a";
        first.items.front().youtube->lyrics_page = "https://letras.mus.br/b/";
        auto other = first;
        other.items.front().youtube->artist = "ab";
        other.items.front().youtube->lyrics_page = "https://letras.mus.br/";
        // Both independently valid metadata combinations retain distinct identities.
        other.items.front().youtube->lyrics_page += "c/";
        QVERIFY(!identity.fingerprint(first).empty());
        QVERIFY(!identity.fingerprint(other).empty());
        QVERIFY(identity.fingerprint(first) != identity.fingerprint(other));
        auto ordered = original;
        ordered.items.push_back(changed.items.front());
        ordered.items.back().youtube->video_id = "ZbCdEf_12-3";
        ordered.items.back().url = "https://www.youtube.com/watch?v=ZbCdEf_12-3";
        const auto before = identity.fingerprint(ordered);
        QCOMPARE(before.size(), std::size_t{64});
        std::reverse(ordered.items.begin(), ordered.items.end());
        QVERIFY(identity.fingerprint(ordered) != before);
        changed = original;
        changed.request_id = "ABCDEFAB-CDEF-4ABC-8DEF-123456789ABC";
        QCOMPARE(identity.fingerprint(changed), fingerprint);
    }

    void youtubeFingerprintRejectsIncoherentOrInvalidMetadata_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QByteArray>("value");
        QTest::newRow("short-video-id") << u"video"_s << QByteArray{"short"};
        QTest::newRow("nonopaque-video-id") << u"video"_s << QByteArray{"/bCdEf_12-3"};
        QTest::newRow("different-video-id") << u"video"_s << QByteArray{"ZbCdEf_12-3"};
        QTest::newRow("empty-title") << u"title"_s << QByteArray{};
        QTest::newRow("different-title") << u"title"_s << QByteArray{"Other"};
        QTest::newRow("long-title") << u"both-titles"_s << QByteArray(513, 't');
        QTest::newRow("long-artist") << u"artist"_s << QByteArray(513, 'a');
        QTest::newRow("long-page") << u"page"_s << QByteArray(2049, 'p');
        QTest::newRow("malformed-title-utf8") << u"both-titles"_s << QByteArray::fromHex("c0af");
        QTest::newRow("malformed-artist-utf8") << u"artist"_s << QByteArray::fromHex("eda080");
        QTest::newRow("malformed-page-utf8") << u"page"_s << QByteArray::fromHex("ff");
        QTest::newRow("artist-nul") << u"artist"_s << QByteArray{"A\0B", 3};
        QTest::newRow("artist-newline") << u"artist"_s << QByteArray{"A\nB"};
        QTest::newRow("artist-leading-space") << u"artist"_s << QByteArray{" Artist"};
        QTest::newRow("artist-bidi") << u"artist"_s << QByteArray::fromHex("41e280ae42");
        QTest::newRow("page-wrong-host") << u"page"_s << QByteArray{"https://evil.example/a/"};
        QTest::newRow("page-userinfo") << u"page"_s << QByteArray{"https://@letras.mus.br/a/"};
        QTest::newRow("page-empty-query") << u"page"_s << QByteArray{"https://letras.mus.br/a/?"};
        QTest::newRow("page-dot-segment") << u"page"_s << QByteArray{"https://letras.mus.br/a/../b/"};
        QTest::newRow("page-double-escape") << u"page"_s << QByteArray{"https://letras.mus.br/%252e/"};
        QTest::newRow("direct-url") << u"url"_s << QByteArray{"https://example.org/a.mp3"};
        QTest::newRow("canonical-extra-query")
            << u"url"_s << QByteArray{"https://www.youtube.com/watch?v=AbCdEf_12-3&x=1"};
    }

    void youtubeFingerprintRejectsIncoherentOrInvalidMetadata() {
        QFETCH(QString, field);
        QFETCH(QByteArray, value);
        auto invalid = youtubeRequest();
        auto& item = invalid.items.front();
        auto& youtube = *item.youtube;
        if (field == u"video"_s)
            youtube.video_id = value.toStdString();
        else if (field == u"title"_s)
            youtube.title = value.toStdString();
        else if (field == u"both-titles"_s)
            youtube.title = item.title = value.toStdString();
        else if (field == u"artist"_s)
            youtube.artist = value.toStdString();
        else if (field == u"page"_s)
            youtube.lyrics_page = value.toStdString();
        else if (field == u"url"_s)
            item.url = value.toStdString();
        else
            QFAIL("Unknown synthetic test field");
        QtDownloadRequestIdentity identity;
        QVERIFY(identity.fingerprint(invalid).empty());
    }

    void youtubeFingerprintBoundsTheWholeBatchAndKeepsOptionalFieldsOptional() {
        QtDownloadRequestIdentity identity;
        auto valid = youtubeRequest();
        auto exact = valid;
        exact.items.front().title = std::string(512, 't');
        exact.items.front().youtube->title = exact.items.front().title;
        exact.items.front().youtube->artist = std::string(512, 'a');
        constexpr std::string_view lyrics_prefix{"https://letras.mus.br/"};
        exact.items.front().youtube->lyrics_page =
            std::string(lyrics_prefix) + std::string(2048 - lyrics_prefix.size(), 'p');
        QCOMPARE(identity.fingerprint(exact).size(), std::size_t{64});
        valid.items.front().youtube->artist.clear();
        valid.items.front().youtube->lyrics_page.clear();
        valid.items.front().youtube->fetch_lyrics = false;
        QCOMPARE(identity.fingerprint(valid).size(), std::size_t{64});
        auto invalid = valid;
        invalid.items.front().kind = DownloadMediaKind::Video;
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = valid;
        invalid.items.push_back(invalid.items.front());
        QVERIFY(identity.fingerprint(invalid).empty());
        invalid = valid;
        invalid.items.clear();
        for (int index = 0; index < 20; ++index) {
            auto item = valid.items.front();
            item.youtube->video_id = "AbCdEf_12" + std::string(index < 10 ? "0" : "1") + std::to_string(index % 10);
            item.url = "https://www.youtube.com/watch?v=" + item.youtube->video_id;
            item.title.assign(512, '"');
            item.youtube->title = item.title;
            item.youtube->artist.assign(512, '"');
            constexpr std::string_view prefix{"https://letras.mus.br/"};
            item.youtube->lyrics_page = std::string(prefix) + std::string(2048 - prefix.size(), 'p');
            invalid.items.push_back(std::move(item));
        }
        // Escaping otherwise bounded strings makes this typed batch exceed its 64 KiB wire budget.
        QVERIFY(identity.fingerprint(invalid).empty());
    }

    void youtubeReplayAfterCheckpointRoundTripUsesAllMetadataWithoutPersistingSourceData() {
        QtDownloadRequestIdentity identity;
        NoExecution first_execution;
        NoImport first_import;
        DownloadService first{first_execution, first_import, "before", {}, &identity};
        const auto original = youtubeRequest();
        const auto accepted = first.submitStaged(original);
        QCOMPARE(accepted.error, DownloadSubmissionError::None);
        const auto checkpoint = first.checkpoint();
        QVERIFY(checkpoint);
        const auto encoded = DownloadCheckpointJson::encode(*checkpoint);
        QVERIFY(!encoded.isEmpty());
        const auto root = QJsonDocument::fromJson(encoded).object();
        QCOMPARE(keys(root), (QSet<QString>{u"schemaVersion"_s, u"owner"_s, u"receipts"_s, u"jobs"_s}));
        QCOMPARE(keys(root.value(u"receipts"_s).toArray().at(0).toObject()),
                 (QSet<QString>{u"requestId"_s, u"fingerprint"_s, u"jobIds"_s, u"retainedRequestBytes"_s}));
        QVERIFY(!encoded.contains("https://"));
        QVERIFY(!encoded.contains("googlevideo"));
        QVERIFY(!encoded.contains("lyricsPage"));
        QVERIFY(!encoded.contains("fetchLyrics"));
        QVERIFY(!encoded.contains("Artist caf"));
        const auto decoded = DownloadCheckpointJson::decode(encoded);
        QCOMPARE(decoded.status, DownloadCheckpointJsonStatus::Valid);
        QVERIFY(decoded.checkpoint);
        NoExecution restored_execution;
        NoImport restored_import;
        DownloadService restored{restored_execution, restored_import, "after", {}, &identity};
        QVERIFY(restored.restore(*decoded.checkpoint));
        QCOMPARE(restored.submitStaged(original).job_ids, accepted.job_ids);
        for (int field = 0; field < 3; ++field) {
            auto changed = original;
            if (field == 0)
                changed.items.front().youtube->artist += " changed";
            else if (field == 1)
                changed.items.front().youtube->lyrics_page += "alternate/";
            else
                changed.items.front().youtube->fetch_lyrics = false;
            QCOMPARE(restored.submitStaged(changed).error, DownloadSubmissionError::InvalidRequest);
        }
        QCOMPARE(restored.submitStaged(original).job_ids, accepted.job_ids);
        QCOMPARE(first_execution.starts, 0U);
        QCOMPARE(restored_execution.starts, 0U);
        QCOMPARE(restored.snapshot().front().state, DownloadJobState::Interrupted);
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
