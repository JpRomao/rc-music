#include "adapters/DownloadMediaProfileJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <utility>

using namespace Qt::StringLiterals;
namespace music_player::m0::test {
namespace {
constexpr std::array stages{DownloadMediaProfileStage::Planned, DownloadMediaProfileStage::Created,
                            DownloadMediaProfileStage::InUse,   DownloadMediaProfileStage::CleanupPending,
                            DownloadMediaProfileStage::Removed, DownloadMediaProfileStage::Abandoned};
constexpr std::string_view operation = "12345678-1234-4234-8234-123456789abc";
constexpr std::string_view other_operation = "87654321-4321-4321-b432-cba987654321";
constexpr auto sid_hex = "010800000000000f0200000001000000020000000300000004000000050000000600000007000000";
DownloadMediaProfileSid sid() {
    DownloadMediaProfileSid result{};
    result[0] = 1;
    result[1] = 8;
    result[7] = 15;
    result[8] = 2;
    for (std::size_t i = 1; i < 8; ++i)
        result[8 + 4 * i] = static_cast<std::uint8_t>(i);
    return result;
}
DownloadMediaProfileRecord sample(const DownloadMediaProfileStage stage = DownloadMediaProfileStage::Planned) {
    DownloadMediaProfileRecord result{.job_id = "job-1", .operation_id = std::string{operation}, .stage = stage};
    if (stage != DownloadMediaProfileStage::Planned && stage != DownloadMediaProfileStage::Abandoned)
        result.sid = sid();
    return result;
}
QJsonObject object() {
    return {{u"schemaVersion"_s, 1},
            {u"owner"_s, u"rc-music-download-media-profile"_s},
            {u"jobId"_s, u"job-1"_s},
            {u"operationId"_s, QString::fromLatin1(operation.data(), static_cast<qsizetype>(operation.size()))},
            {u"stage"_s, u"created"_s},
            {u"sid"_s, QString::fromLatin1(sid_hex)}};
}
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
} // namespace

class DownloadMediaProfileJsonTests final : public QObject {
    Q_OBJECT
  private slots:
    void everyStageRoundTripsWithOnlyTheClosedFields() {
        const QStringList expected_stages{"planned", "created", "in-use", "cleanup-pending", "removed", "abandoned"};
        for (std::size_t i = 0; i < stages.size(); ++i) {
            const auto expected = sample(stages[i]);
            QVERIFY(validDownloadMediaProfileRecord(expected));
            const auto encoded = DownloadMediaProfileJson::encode(expected);
            QVERIFY(!encoded.isEmpty());
            const auto fields = QJsonDocument::fromJson(encoded).object();
            QCOMPARE(fields.size(), 6);
            QCOMPARE(fields.value(u"schemaVersion"_s), QJsonValue{1});
            QCOMPARE(fields.value(u"owner"_s), QJsonValue{u"rc-music-download-media-profile"_s});
            QCOMPARE(fields.value(u"stage"_s).toString(), expected_stages[static_cast<qsizetype>(i)]);
            if (expected.sid)
                QCOMPARE(fields.value(u"sid"_s).toString(), QString::fromLatin1(sid_hex));
            else
                QVERIFY(fields.value(u"sid"_s).isNull());
            const auto decoded = DownloadMediaProfileJson::decode(encoded);
            QCOMPARE(decoded.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(decoded.record && *decoded.record == expected);
            QVERIFY(!encoded.contains("profileName") && !encoded.contains("path") && !encoded.contains("url"));
        }
    }
    void independentDocumentPreservesTheExactSidAndIdentity() {
        const auto decoded = DownloadMediaProfileJson::decode(json(object()));
        QCOMPARE(decoded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(decoded.record && *decoded.record == sample(DownloadMediaProfileStage::Created));
    }
    void jobAlphabetAndLengthAreInclusive() {
        for (const auto& job : {std::string{"a"}, std::string{"AZaz09_-"}, std::string(64, 'Z')}) {
            auto record = sample();
            record.job_id = job;
            QVERIFY(validDownloadMediaProfileRecord(record));
            const auto loaded = DownloadMediaProfileJson::decode(DownloadMediaProfileJson::encode(record));
            QVERIFY(loaded.record && *loaded.record == record);
        }
    }
    void invalidRecords_data() {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i < 22; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidRecords() {
        QFETCH(int, mutation);
        auto record = sample(DownloadMediaProfileStage::Created);
        switch (mutation) {
        case 0:
            record.job_id.clear();
            break;
        case 1:
            record.job_id.assign(65, 'a');
            break;
        case 2:
            record.job_id = "a.b";
            break;
        case 3:
            record.job_id = "a/b";
            break;
        case 4:
            record.job_id = "a b";
            break;
        case 5:
            record.job_id = "\xc3\xa9";
            break;
        case 6:
            record.operation_id.clear();
            break;
        case 7:
            record.operation_id[35] = 'C';
            break;
        case 8:
            record.operation_id[14] = '1';
            break;
        case 9:
            record.operation_id[19] = '7';
            break;
        case 10:
            record.operation_id[8] = '_';
            break;
        case 11:
            record.operation_id = '{' + record.operation_id + '}';
            break;
        case 12:
            record.stage = static_cast<DownloadMediaProfileStage>(99);
            break;
        case 13:
            record.sid.reset();
            break;
        case 14:
            record.stage = DownloadMediaProfileStage::Planned;
            break;
        case 15:
            record.stage = DownloadMediaProfileStage::Abandoned;
            break;
        case 16:
            (*record.sid)[0] = 2;
            break;
        case 17:
            (*record.sid)[1] = 7;
            break;
        case 18:
            (*record.sid)[6] = 1;
            break;
        case 19:
            (*record.sid)[7] = 16;
            break;
        case 20:
            (*record.sid)[8] = 3;
            break;
        case 21:
            (*record.sid)[8] = 0;
            (*record.sid)[11] = 2;
            break;
        }
        QVERIFY(!validDownloadMediaProfileRecord(record));
        QVERIFY(DownloadMediaProfileJson::encode(record).isEmpty());
        QVERIFY(!mayReplaceDownloadMediaProfileRecord(record, record));
    }
    void profileNameIsDerivedOnlyFromACanonicalUuid_data() {
        QTest::addColumn<QString>("uuid");
        QTest::addColumn<bool>("valid");
        for (const auto variant : {'8', '9', 'a', 'b'}) {
            auto value = std::string{operation};
            value[19] = variant;
            QTest::newRow(value.c_str()) << QString::fromStdString(value) << true;
        }
        for (const auto& value : {"", "existing-profile", "../profile", "{12345678-1234-4234-8234-123456789abc}",
                                  "12345678-1234-4234-8234-123456789abC", "12345678-1234-1234-8234-123456789abc",
                                  "12345678-1234-4234-c234-123456789abc"})
            QTest::newRow(*value ? value : "empty") << QString::fromLatin1(value) << false;
    }
    void profileNameIsDerivedOnlyFromACanonicalUuid() {
        QFETCH(QString, uuid);
        QFETCH(bool, valid);
        const auto name = downloadMediaProfileName(uuid.toStdString());
        if (valid)
            QCOMPARE(name, "RCMusic.Media." + uuid.toStdString());
        else
            QVERIFY(name.empty());
    }
    void transitionMatrix_data() {
        QTest::addColumn<int>("before");
        QTest::addColumn<int>("after");
        QTest::addColumn<bool>("allowed");
        constexpr bool allowed[6][6]{
            {true, true, false, false, false, true},   {false, true, true, true, false, false},
            {false, false, true, true, false, false},  {false, false, false, true, true, false},
            {false, false, false, false, true, false}, {false, false, false, false, false, true}};
        for (int before = 0; before < 6; ++before)
            for (int after = 0; after < 6; ++after)
                QTest::newRow(qPrintable(QString::number(before) + '-' + QString::number(after)))
                    << before << after << allowed[before][after];
    }
    void transitionMatrix() {
        QFETCH(int, before);
        QFETCH(int, after);
        QFETCH(bool, allowed);
        QCOMPARE(mayReplaceDownloadMediaProfileRecord(sample(stages[static_cast<std::size_t>(before)]),
                                                      sample(stages[static_cast<std::size_t>(after)])),
                 allowed);
    }
    void replayCannotRebindJobOrSidAndInvalidRecordsCannotReplace() {
        for (const auto stage : stages) {
            const auto previous = sample(stage);
            auto changed = previous;
            changed.job_id = "other-job";
            QVERIFY(!mayReplaceDownloadMediaProfileRecord(previous, changed));
            if (changed.sid) {
                changed = previous;
                ++(*changed.sid)[12];
                QVERIFY(!mayReplaceDownloadMediaProfileRecord(previous, changed));
            }
            changed = previous;
            changed.job_id.clear();
            QVERIFY(!mayReplaceDownloadMediaProfileRecord(previous, changed));
            QVERIFY(!mayReplaceDownloadMediaProfileRecord(changed, previous));
        }
    }
    void advancingCannotRebindTheJobOrAnExistingSid() {
        constexpr std::array transitions{
            std::pair{DownloadMediaProfileStage::Planned, DownloadMediaProfileStage::Created},
            std::pair{DownloadMediaProfileStage::Planned, DownloadMediaProfileStage::Abandoned},
            std::pair{DownloadMediaProfileStage::Created, DownloadMediaProfileStage::InUse},
            std::pair{DownloadMediaProfileStage::Created, DownloadMediaProfileStage::CleanupPending},
            std::pair{DownloadMediaProfileStage::InUse, DownloadMediaProfileStage::CleanupPending},
            std::pair{DownloadMediaProfileStage::CleanupPending, DownloadMediaProfileStage::Removed}};
        for (const auto& [before, after] : transitions) {
            const auto previous = sample(before);
            auto changed = sample(after);
            changed.job_id = "another-job";
            QVERIFY(!mayReplaceDownloadMediaProfileRecord(previous, changed));
            if (previous.sid) {
                changed = sample(after);
                ++(*changed.sid)[12];
                QVERIFY(!mayReplaceDownloadMediaProfileRecord(previous, changed));
            }
        }
    }
    void newOperationCanReplaceOnlyATerminalSlotWithPlanned() {
        for (const auto before : stages) {
            for (const auto after : stages) {
                auto next = sample(after);
                next.operation_id = std::string{other_operation};
                const bool allowed =
                    (before == DownloadMediaProfileStage::Removed || before == DownloadMediaProfileStage::Abandoned) &&
                    after == DownloadMediaProfileStage::Planned;
                QCOMPARE(mayReplaceDownloadMediaProfileRecord(sample(before), next), allowed);
                next.job_id = "another-job";
                QCOMPARE(mayReplaceDownloadMediaProfileRecord(sample(before), next), allowed);
            }
        }
    }
    void malformedDocuments_data() {
        QTest::addColumn<QByteArray>("document");
        auto add = [](const char* name, QJsonObject value) { QTest::newRow(name) << json(value); };
        auto value = object();
        value.insert(u"owner"_s, u"another-owner"_s);
        add("owner", value);
        value = object();
        value.insert(u"url"_s, u"secret"_s);
        add("extra-root", value);
        for (const auto& key : object().keys()) {
            value = object();
            value.remove(key);
            add(qPrintable("missing-" + key), value);
            value = object();
            value.insert(key, QJsonValue::Null);
            add(qPrintable("null-" + key), value);
        }
        value = object();
        value.insert(u"schemaVersion"_s, 0);
        add("zero-version", value);
        value = object();
        value.insert(u"schemaVersion"_s, 1.5);
        add("fraction-version", value);
        value = object();
        value.insert(u"schemaVersion"_s, u"1"_s);
        add("text-version", value);
        value = object();
        value.insert(u"jobId"_s, u"invalid.job"_s);
        add("invalid-job", value);
        value = object();
        value.insert(u"operationId"_s, u"12345678-1234-1234-8234-123456789abc"_s);
        add("invalid-uuid", value);
        value = object();
        value.insert(u"stage"_s, u"future-stage"_s);
        add("unknown-stage", value);
        value = object();
        value.insert(u"stage"_s, u"planned"_s);
        add("planned-with-sid", value);
        value = object();
        value.insert(u"sid"_s, QString::fromLatin1(sid_hex).toUpper());
        add("uppercase-sid", value);
        value = object();
        value.insert(u"sid"_s, QString::fromLatin1(sid_hex).chopped(2));
        add("short-sid", value);
        value = object();
        value.insert(u"sid"_s, QString::fromLatin1(sid_hex) + "00");
        add("long-sid", value);
        value = object();
        value.insert(u"sid"_s, QJsonArray{});
        add("array-sid", value);
        value = object();
        value.insert(u"sid"_s, u"010800000000000f0000000201000000020000000300000004000000050000000600000007000000"_s);
        add("wrong-endian-base-rid", value);
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("root-array") << QByteArray{"[]"};
        QTest::newRow("duplicate-root") << QByteArray{
            R"({"schemaVersion":1,"schemaVersion":2,"owner":"rc-music-download-media-profile"})"};
        QTest::newRow("escaped-duplicate")
            << QByteArray{R"({"schemaVersion":1,"\u0073chemaVersion":2,"owner":"rc-music-download-media-profile"})"};
        QTest::newRow("invalid-utf8") << QByteArray{"{\"x\":\"\xff\"}"};
        QTest::newRow("surrogate") << QByteArray{
            R"({"schemaVersion":2,"owner":"rc-music-download-media-profile","x":"\ud800"})"};
        QTest::newRow("deep") << QByteArray(65, '[') + "0" + QByteArray(65, ']');
        QTest::newRow("trailing") << json(object()) + "garbage";
        QTest::newRow("oversized") << QByteArray(DownloadMediaProfileJson::maximum_file_size + 1, ' ');
    }
    void malformedDocuments() {
        QFETCH(QByteArray, document);
        const auto result = DownloadMediaProfileJson::decode(document);
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::Invalid);
        QVERIFY(!result.record);
    }
    void futureVersionRequiresKnownOwnerAndValidLexicalJson() {
        auto value = object();
        value.insert(u"schemaVersion"_s, 2);
        value.remove(u"sid"_s);
        value.insert(u"future-field"_s, true);
        const auto result = DownloadMediaProfileJson::decode(json(value));
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::FutureVersion);
        QVERIFY(!result.record);
        value.insert(u"owner"_s, u"another-owner"_s);
        QCOMPARE(DownloadMediaProfileJson::decode(json(value)).status, DownloadCheckpointLoadStatus::Invalid);
    }
    void eightKiBIsInclusiveAndAdditionalWhitespaceIsRejected() {
        auto encoded = json(object());
        encoded.append(QByteArray(DownloadMediaProfileJson::maximum_file_size - encoded.size(), ' '));
        QCOMPARE(encoded.size(), DownloadMediaProfileJson::maximum_file_size);
        const auto accepted = DownloadMediaProfileJson::decode(encoded);
        QCOMPARE(accepted.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(accepted.record && *accepted.record == sample(DownloadMediaProfileStage::Created));
        encoded.append(' ');
        QCOMPARE(DownloadMediaProfileJson::decode(encoded).status, DownloadCheckpointLoadStatus::Invalid);
    }
};
} // namespace music_player::m0::test
QTEST_GUILESS_MAIN(music_player::m0::test::DownloadMediaProfileJsonTests)
#include "DownloadMediaProfileJsonTests.moc"
