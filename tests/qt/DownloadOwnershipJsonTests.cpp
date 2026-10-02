#include "adapters/DownloadOwnershipJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;
namespace music_player::m0::test {
namespace {
DownloadFileIdentity identity(const std::uint8_t id) {
    DownloadFileIdentity result{.volume_serial = std::numeric_limits<std::uint64_t>::max()};
    result.file_id[0] = id;
    return result;
}
DownloadOwnershipRecord sample(const DownloadOwnershipStage stage = DownloadOwnershipStage::Planned) {
    DownloadOwnershipRecord result{.job_id = "job-1",
                                   .workspace_id = "12345678-1234-4234-8234-123456789abc",
                                   .stage = stage,
                                   .staging_parent = identity(1),
                                   .publish_parent = identity(2)};
    if (stage != DownloadOwnershipStage::Planned) {
        result.job_directory = identity(3);
        result.input = identity(4);
        result.output = identity(5);
    }
    if (stage == DownloadOwnershipStage::PublicationPrepared || stage == DownloadOwnershipStage::Published) {
        result.input_bytes = 2;
        result.output_bytes = 3;
        result.output_sha256 = std::string(64, 'a');
    }
    return result;
}
QJsonObject identityObject(const std::uint8_t id) {
    QByteArray bytes(16, '\0');
    bytes[0] = static_cast<char>(id);
    return {{u"volumeSerial"_s, u"18446744073709551615"_s}, {u"fileId"_s, QString::fromLatin1(bytes.toHex())}};
}
QJsonObject object() {
    return {{u"schemaVersion"_s, 1},
            {u"owner"_s, u"rc-music-download-ownership"_s},
            {u"records"_s, QJsonArray{QJsonObject{{u"jobId"_s, u"job-1"_s},
                                                  {u"workspaceId"_s, u"12345678-1234-4234-8234-123456789abc"_s},
                                                  {u"stage"_s, u"publication-prepared"_s},
                                                  {u"stagingParent"_s, identityObject(1)},
                                                  {u"publishParent"_s, identityObject(2)},
                                                  {u"jobDirectory"_s, identityObject(3)},
                                                  {u"input"_s, identityObject(4)},
                                                  {u"output"_s, identityObject(5)},
                                                  {u"maximumWorkspaceBytes"_s, u"536870912"_s},
                                                  {u"inputBytes"_s, u"2"_s},
                                                  {u"outputBytes"_s, u"3"_s},
                                                  {u"outputSha256"_s, QString(64, u'a')},
                                                  {u"cleanupComplete"_s, false}}}}};
}
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
} // namespace
class DownloadOwnershipJsonTests final : public QObject {
    Q_OBJECT
  private slots:
    void emptyAndEveryStageRoundTrip() {
        auto encoded = DownloadOwnershipJson::encode({});
        QVERIFY(!encoded.isEmpty());
        auto result = DownloadOwnershipJson::decode(encoded);
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(result.journal && result.journal->records.empty());
        for (const auto stage : {DownloadOwnershipStage::Planned, DownloadOwnershipStage::WorkspaceReady,
                                 DownloadOwnershipStage::PublicationPrepared, DownloadOwnershipStage::Published}) {
            for (bool clean : {false, true}) {
                auto record = sample(stage);
                record.cleanup_complete = clean;
                const DownloadOwnershipJournal expected{{record}};
                QVERIFY(validDownloadOwnershipJournal(expected));
                encoded = DownloadOwnershipJson::encode(expected);
                QVERIFY(!encoded.isEmpty());
                result = DownloadOwnershipJson::decode(encoded);
                QVERIFY(result.journal);
                QVERIFY(*result.journal == expected);
                QVERIFY(!encoded.contains("http") && !encoded.contains("title") && !encoded.contains("path"));
            }
        }
    }
    void independentDocumentPreservesExactIdentity() {
        const auto result = DownloadOwnershipJson::decode(json(object()));
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(result.journal);
        QVERIFY(result.journal->records == std::vector{sample(DownloadOwnershipStage::PublicationPrepared)});
    }
    void invalidRecord_data() {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i < 20; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidRecord() {
        QFETCH(int, mutation);
        auto record = sample(DownloadOwnershipStage::PublicationPrepared);
        switch (mutation) {
        case 0:
            record.job_id = "../job";
            break;
        case 1:
            record.job_id = std::string(65, 'a');
            break;
        case 2:
            record.workspace_id[14] = '3';
            break;
        case 3:
            record.workspace_id[0] = 'A';
            break;
        case 4:
            record.stage = static_cast<DownloadOwnershipStage>(99);
            break;
        case 5:
            record.staging_parent.file_id = {};
            break;
        case 6:
            record.output.reset();
            break;
        case 7:
            record.output->volume_serial = 1;
            break;
        case 8:
            record.output = record.input;
            break;
        case 9:
            record.maximum_workspace_bytes = 0;
            break;
        case 10:
            ++record.maximum_workspace_bytes;
            break;
        case 11:
            record.input_bytes = 0;
            break;
        case 12:
            record.output_bytes = 0;
            break;
        case 13:
            record.output_bytes = record.maximum_workspace_bytes;
            break;
        case 14:
            record.input_bytes = std::numeric_limits<std::uint64_t>::max();
            break;
        case 15:
            record.output_sha256[0] = 'A';
            break;
        case 16:
            record.output_sha256.pop_back();
            break;
        case 17:
            record.stage = DownloadOwnershipStage::Planned;
            break;
        case 18:
            record.stage = DownloadOwnershipStage::WorkspaceReady;
            break;
        case 19:
            record.job_directory = record.staging_parent;
            break;
        }
        QVERIFY(!validDownloadOwnershipRecord(record));
        QVERIFY(DownloadOwnershipJson::encode({{record}}).isEmpty());
    }
    void countAndUniquenessAreInclusive() {
        DownloadOwnershipJournal journal;
        for (unsigned i = 0; i < 128; ++i) {
            auto record = sample();
            record.job_id = "job-" + std::to_string(i);
            record.workspace_id = QString(u"%1-1234-4234-8234-123456789abc"_s).arg(i, 8, 16, u'0').toStdString();
            journal.records.push_back(record);
        }
        QVERIFY(validDownloadOwnershipJournal(journal));
        QVERIFY(DownloadOwnershipJson::decode(DownloadOwnershipJson::encode(journal)).journal);
        journal.records.push_back(sample());
        QVERIFY(!validDownloadOwnershipJournal(journal));
        journal.records.pop_back();
        journal.records[1].job_id = journal.records[0].job_id;
        QVERIFY(!validDownloadOwnershipJournal(journal));
        journal.records[1].job_id = "distinct";
        journal.records[1].workspace_id = journal.records[0].workspace_id;
        QVERIFY(!validDownloadOwnershipJournal(journal));
    }
    void replacementsCannotRegressOrRebindEvidence() {
        auto previous = sample();
        for (const auto stage : {DownloadOwnershipStage::WorkspaceReady, DownloadOwnershipStage::PublicationPrepared,
                                 DownloadOwnershipStage::Published}) {
            auto next = sample(stage);
            QVERIFY(mayReplaceDownloadOwnershipRecord(previous, next));
            QVERIFY(!mayReplaceDownloadOwnershipRecord(next, previous));
            QVERIFY(mayReplaceDownloadOwnershipRecord(next, next));
            auto rebound = next;
            rebound.publish_parent = identity(8);
            QVERIFY(!mayReplaceDownloadOwnershipRecord(previous, rebound));
            previous = next;
        }
        auto cleaned = previous;
        cleaned.cleanup_complete = true;
        QVERIFY(mayReplaceDownloadOwnershipRecord(previous, cleaned));
        QVERIFY(!mayReplaceDownloadOwnershipRecord(cleaned, previous));
        auto modified = previous;
        modified.output_sha256[0] = 'b';
        QVERIFY(!mayReplaceDownloadOwnershipRecord(previous, modified));
        QVERIFY(!mayReplaceDownloadOwnershipRecord(sample(), sample(DownloadOwnershipStage::Published)));
    }
    void malformedShape_data() {
        QTest::addColumn<QByteArray>("document");
        auto add = [](const char* name, QJsonObject value) { QTest::newRow(name) << json(value); };
        auto value = object();
        value.insert(u"owner"_s, u"another"_s);
        add("owner", value);
        value = object();
        value.insert(u"url"_s, u"secret"_s);
        add("extra-root", value);
        value = object();
        value.remove(u"records"_s);
        add("missing-root", value);
        value = object();
        value.insert(u"schemaVersion"_s, 0);
        add("zero-version", value);
        value = object();
        value.insert(u"schemaVersion"_s, 1.5);
        add("fraction-version", value);
        value = object();
        value.insert(u"records"_s, QJsonObject{});
        add("non-array", value);
        const auto fields = QJsonArray{u"stage"_s,        u"jobId"_s,           u"maximumWorkspaceBytes"_s,
                                       u"outputSha256"_s, u"cleanupComplete"_s, u"jobDirectory"_s};
        for (const auto& field : fields) {
            value = object();
            auto array = value.value(u"records"_s).toArray();
            auto record = array[0].toObject();
            record.insert(field.toString(), QJsonValue::Null);
            array[0] = record;
            value.insert(u"records"_s, array);
            add(qPrintable(field.toString()), value);
        }
        for (const auto& number : {u"01"_s, u"-1"_s, u"18446744073709551616"_s, u"１"_s}) {
            value = object();
            auto array = value.value(u"records"_s).toArray();
            auto record = array[0].toObject();
            record.insert(u"inputBytes"_s, number);
            array[0] = record;
            value.insert(u"records"_s, array);
            add(qPrintable(number), value);
        }
        QTest::newRow("duplicate-root") << QByteArray(
            R"({"schemaVersion":1,"schemaVersion":2,"owner":"rc-music-download-ownership","records":[]})");
        QTest::newRow("escaped-duplicate") << QByteArray(
            R"({"schemaVersion":1,"\u0073chemaVersion":2,"owner":"rc-music-download-ownership","records":[]})");
        QTest::newRow("invalid-utf8") << QByteArray("{\"x\":\"\xff\"}");
        QTest::newRow("surrogate") << QByteArray(R"({"schemaVersion":2,"x":"\ud800"})");
        QTest::newRow("deep") << QByteArray(65, '[') + "0" + QByteArray(65, ']');
        QTest::newRow("oversized") << QByteArray(DownloadOwnershipJson::maximum_file_size + 1, ' ');
        QTest::newRow("trailing") << json(object()) + "garbage";
    }
    void malformedShape() {
        QFETCH(QByteArray, document);
        const auto result = DownloadOwnershipJson::decode(document);
        QCOMPARE(result.status, DownloadCheckpointLoadStatus::Invalid);
        QVERIFY(!result.journal);
    }
    void futureVersionIsPreservedAndLimitAllowsWhitespace() {
        auto value = object();
        value.insert(u"schemaVersion"_s, 2);
        QCOMPARE(DownloadOwnershipJson::decode(json(value)).status, DownloadCheckpointLoadStatus::FutureVersion);
        auto encoded = json(object());
        encoded.append(QByteArray(DownloadOwnershipJson::maximum_file_size - encoded.size(), ' '));
        QCOMPARE(DownloadOwnershipJson::decode(encoded).status, DownloadCheckpointLoadStatus::Loaded);
    }
};
} // namespace music_player::m0::test
QTEST_GUILESS_MAIN(music_player::m0::test::DownloadOwnershipJsonTests)
#include "DownloadOwnershipJsonTests.moc"
