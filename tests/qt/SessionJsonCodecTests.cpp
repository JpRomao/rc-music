#include "adapters/SessionJsonCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <filesystem>
#include <limits>
#include <string>

using namespace Qt::StringLiterals;

namespace music_player::m0::test {
namespace {

[[nodiscard]] std::filesystem::path nativePath(const QString& value) {
#ifdef _WIN32
    return std::filesystem::path{value.toStdWString()};
#else
    const QByteArray utf8 = value.toUtf8();
    return std::filesystem::path{std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())}};
#endif
}

[[nodiscard]] SessionSnapshot exactSnapshot() {
    const std::filesystem::path duplicate = nativePath(uR"(C:\Música\Abertura 日本語.mp3)"_s);
    return {
        .queue =
            {
                .items =
                    {
                        {{1}, duplicate},
                        {{9'007'199'254'740'993ULL}, duplicate},
                        {{std::numeric_limits<std::uint64_t>::max()},
                         nativePath(uR"(\\servidor\Mídia compartilhada\Final ☕.flac)"_s)},
                    },
                .current_occurrence_id = LocalQueueOccurrenceId{std::numeric_limits<std::uint64_t>::max()},
            },
        .position_ms = std::numeric_limits<std::int64_t>::max(),
    };
}

[[nodiscard]] QJsonObject validObject() {
    return {
        {u"schemaVersion"_s, SessionJsonCodec::schema_version},
        {u"queue"_s,
         QJsonObject{
             {u"items"_s,
              QJsonArray{
                  QJsonObject{{u"occurrenceId"_s, u"1"_s}, {u"sourcePath"_s, uR"(C:\Música\Abertura 日本語.mp3)"_s}},
                  QJsonObject{{u"occurrenceId"_s, u"9007199254740993"_s},
                              {u"sourcePath"_s, uR"(C:\Música\Final.flac)"_s}},
              }},
             {u"currentOccurrenceId"_s, u"9007199254740993"_s},
         }},
        {u"positionMs"_s, u"123456"_s},
    };
}

[[nodiscard]] QByteArray bytes(const QJsonObject& object) {
    return QJsonDocument{object}.toJson(QJsonDocument::Compact);
}

[[nodiscard]] QSet<QString> keySet(const QJsonObject& object) {
    QSet<QString> keys;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
        keys.insert(iterator.key());
    return keys;
}

[[nodiscard]] QJsonObject withItemField(const QString& key, const QJsonValue& value) {
    QJsonObject root = validObject();
    QJsonObject queue = root.value(u"queue"_s).toObject();
    QJsonArray items = queue.value(u"items"_s).toArray();
    QJsonObject item = items.at(0).toObject();
    item.insert(key, value);
    items.replace(0, item);
    queue.insert(u"items"_s, items);
    root.insert(u"queue"_s, queue);
    return root;
}

[[nodiscard]] QJsonObject withQueueField(const QString& key, const QJsonValue& value) {
    QJsonObject root = validObject();
    QJsonObject queue = root.value(u"queue"_s).toObject();
    queue.insert(key, value);
    root.insert(u"queue"_s, queue);
    return root;
}

} // namespace

class SessionJsonCodecTests final : public QObject {
    Q_OBJECT

  private slots:
    void encodesExactDecimalStringsAndRoundTripsDeterministically() {
        const SessionSnapshot expected = exactSnapshot();

        const QByteArray first = SessionJsonCodec::encode(expected);
        QVERIFY(!first.isEmpty());
        QCOMPARE(SessionJsonCodec::encode(expected), first);

        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(first, &parse_error);
        QCOMPARE(parse_error.error, QJsonParseError::NoError);
        QVERIFY(document.isObject());
        const QJsonObject root = document.object();
        const QSet<QString> expected_root_keys{u"schemaVersion"_s, u"queue"_s, u"positionMs"_s};
        QCOMPARE(keySet(root), expected_root_keys);
        QCOMPARE(root.value(u"schemaVersion"_s), QJsonValue{1});
        QCOMPARE(root.value(u"positionMs"_s), QJsonValue{u"9223372036854775807"_s});

        const QJsonObject queue = root.value(u"queue"_s).toObject();
        const QSet<QString> expected_queue_keys{u"items"_s, u"currentOccurrenceId"_s};
        QCOMPARE(keySet(queue), expected_queue_keys);
        QCOMPARE(queue.value(u"currentOccurrenceId"_s), QJsonValue{u"18446744073709551615"_s});
        const QJsonArray items = queue.value(u"items"_s).toArray();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toObject().value(u"occurrenceId"_s), QJsonValue{u"1"_s});
        QCOMPARE(items.at(1).toObject().value(u"occurrenceId"_s), QJsonValue{u"9007199254740993"_s});
        QCOMPARE(items.at(2).toObject().value(u"occurrenceId"_s), QJsonValue{u"18446744073709551615"_s});
        for (const QJsonValue& item_value : items) {
            const QJsonObject item = item_value.toObject();
            const QSet<QString> expected_item_keys{u"occurrenceId"_s, u"sourcePath"_s};
            QCOMPARE(keySet(item), expected_item_keys);
            QVERIFY(item.value(u"occurrenceId"_s).isString());
            QVERIFY(item.value(u"sourcePath"_s).isString());
        }

        const SessionJsonDecodeResult decoded = SessionJsonCodec::decode(first);
        QCOMPARE(decoded.status, SessionJsonDecodeStatus::Valid);
        QVERIFY(decoded.snapshot.has_value());
        QCOMPARE(*decoded.snapshot, expected);
        QCOMPARE(SessionJsonCodec::encode(*decoded.snapshot), first);
    }

    void roundTripsEmptyAndUnselectedSessions() {
        const SessionSnapshot empty;
        const QByteArray empty_bytes = SessionJsonCodec::encode(empty);
        QVERIFY(!empty_bytes.isEmpty());
        const QJsonObject empty_root = QJsonDocument::fromJson(empty_bytes).object();
        const QJsonObject empty_queue = empty_root.value(u"queue"_s).toObject();
        QCOMPARE(empty_queue.value(u"items"_s), QJsonValue{QJsonArray{}});
        QVERIFY(empty_queue.value(u"currentOccurrenceId"_s).isNull());
        QCOMPARE(empty_root.value(u"positionMs"_s), QJsonValue{u"0"_s});
        QCOMPARE(SessionJsonCodec::decode(empty_bytes).snapshot, std::optional<SessionSnapshot>{empty});

        const SessionSnapshot unselected{
            .queue = {.items = {{{77}, nativePath(uR"(C:\Música\Sem seleção.wav)"_s)}}},
            .position_ms = 0,
        };
        const QByteArray unselected_bytes = SessionJsonCodec::encode(unselected);
        QVERIFY(!unselected_bytes.isEmpty());
        const SessionJsonDecodeResult decoded = SessionJsonCodec::decode(unselected_bytes);
        QCOMPARE(decoded.status, SessionJsonDecodeStatus::Valid);
        QCOMPARE(decoded.snapshot, std::optional<SessionSnapshot>{unselected});
    }

    void acceptsUnknownKeysAtEveryObjectLevel() {
        QJsonObject root = validObject();
        root.insert(u"futureRoot"_s, QJsonArray{true});
        QJsonObject queue = root.value(u"queue"_s).toObject();
        queue.insert(u"futureQueue"_s, 9);
        QJsonArray items = queue.value(u"items"_s).toArray();
        QJsonObject item = items.at(0).toObject();
        item.insert(u"futureItem"_s, QJsonObject{{u"nested"_s, true}});
        items.replace(0, item);
        queue.insert(u"items"_s, items);
        root.insert(u"queue"_s, queue);

        const SessionJsonDecodeResult result = SessionJsonCodec::decode(bytes(root));

        QCOMPARE(result.status, SessionJsonDecodeStatus::Valid);
        QVERIFY(result.snapshot.has_value());
    }

    void classifiesFutureVersionBeforeReadingItsFields_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("version only") << QByteArray{R"({"schemaVersion":2})"};
        QTest::newRow("missing current fields") << QByteArray{R"({"schemaVersion":99,"queue":false})"};
        QTest::newRow("large numeric version") << QByteArray{R"({"schemaVersion":2147483648})"};
        QTest::newRow("hostile current fields")
            << QByteArray{R"({"schemaVersion":2,"queue":"C:\\private\\session.json","positionMs":{}})"};
    }

    void classifiesFutureVersionBeforeReadingItsFields() {
        QFETCH(QByteArray, json);

        const SessionJsonDecodeResult result = SessionJsonCodec::decode(json);

        QCOMPARE(result.status, SessionJsonDecodeStatus::FutureVersion);
        QVERIFY(!result.snapshot.has_value());
    }

    void rejectsMalformedTruncatedAndNonObjectDocuments_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("whitespace") << QByteArray{" \r\n\t"};
        QTest::newRow("truncated") << QByteArray{R"({"schemaVersion":1)"};
        QTest::newRow("trailing comma") << QByteArray{R"({"schemaVersion":1,})"};
        QTest::newRow("array") << QByteArray{R"([])"};
        QTest::newRow("null") << QByteArray{R"(null)"};
        QTest::newRow("string") << QByteArray{R"("C:\\private\\session.json")"};
    }

    void rejectsMalformedTruncatedAndNonObjectDocuments() {
        QFETCH(QByteArray, json);

        const SessionJsonDecodeResult result = SessionJsonCodec::decode(json);

        QCOMPARE(result.status, SessionJsonDecodeStatus::Invalid);
        QVERIFY(!result.snapshot.has_value());
    }

    void rejectsInvalidSchemaVersions_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("zero") << QByteArray{R"({"schemaVersion":0})"};
        QTest::newRow("negative") << QByteArray{R"({"schemaVersion":-1})"};
        QTest::newRow("fractional") << QByteArray{R"({"schemaVersion":1.5})"};
        QTest::newRow("overflowing exponent") << QByteArray{R"({"schemaVersion":1e999})"};
    }

    void rejectsInvalidSchemaVersions() {
        QFETCH(QByteArray, json);

        QCOMPARE(SessionJsonCodec::decode(json).status, SessionJsonDecodeStatus::Invalid);
    }

    void rejectsMissingRequiredKeys_data() {
        QTest::addColumn<QByteArray>("json");
        const QJsonObject complete = validObject();
        for (const QString& key : {u"schemaVersion"_s, u"queue"_s, u"positionMs"_s}) {
            QJsonObject missing = complete;
            missing.remove(key);
            QTest::newRow(("root-" + key).toUtf8().constData()) << bytes(missing);
        }
        for (const QString& key : {u"items"_s, u"currentOccurrenceId"_s}) {
            QJsonObject root = complete;
            QJsonObject queue = root.value(u"queue"_s).toObject();
            queue.remove(key);
            root.insert(u"queue"_s, queue);
            QTest::newRow(("queue-" + key).toUtf8().constData()) << bytes(root);
        }
        for (const QString& key : {u"occurrenceId"_s, u"sourcePath"_s}) {
            QJsonObject root = complete;
            QJsonObject queue = root.value(u"queue"_s).toObject();
            QJsonArray items = queue.value(u"items"_s).toArray();
            QJsonObject item = items.at(0).toObject();
            item.remove(key);
            items.replace(0, item);
            queue.insert(u"items"_s, items);
            root.insert(u"queue"_s, queue);
            QTest::newRow(("item-" + key).toUtf8().constData()) << bytes(root);
        }
    }

    void rejectsMissingRequiredKeys() {
        QFETCH(QByteArray, json);
        QCOMPARE(SessionJsonCodec::decode(json).status, SessionJsonDecodeStatus::Invalid);
    }

    void rejectsWrongJsonTypes_data() {
        QTest::addColumn<QByteArray>("json");

        QJsonObject root = validObject();
        root.insert(u"schemaVersion"_s, u"1"_s);
        QTest::newRow("schema version string") << bytes(root);
        root = validObject();
        root.insert(u"queue"_s, QJsonArray{});
        QTest::newRow("queue array") << bytes(root);
        root = validObject();
        root.insert(u"positionMs"_s, 123456);
        QTest::newRow("position number") << bytes(root);
        QTest::newRow("items object") << bytes(withQueueField(u"items"_s, QJsonObject{}));
        QTest::newRow("current number") << bytes(withQueueField(u"currentOccurrenceId"_s, 1));
        QTest::newRow("item string") << bytes(withQueueField(u"items"_s, QJsonArray{u"not an item"_s}));
        QTest::newRow("occurrence number") << bytes(withItemField(u"occurrenceId"_s, 1));
        QTest::newRow("occurrence null") << bytes(withItemField(u"occurrenceId"_s, QJsonValue::Null));
        QTest::newRow("path object") << bytes(withItemField(u"sourcePath"_s, QJsonObject{}));
    }

    void rejectsWrongJsonTypes() {
        QFETCH(QByteArray, json);
        QCOMPARE(SessionJsonCodec::decode(json).status, SessionJsonDecodeStatus::Invalid);
    }

    void rejectsNonCanonicalAndOverflowingDecimalStrings_data() {
        QTest::addColumn<QByteArray>("json");
        const QStringList noncanonical{
            u""_s,
            u"0"_s,
            u"00"_s,
            u"01"_s,
            u"+1"_s,
            u"-1"_s,
            u" 1"_s,
            u"1 "_s,
            u"1.0"_s,
            u"1e3"_s,
            QString{QChar{0x0661}},
        };
        int row = 0;
        for (const QString& value : noncanonical) {
            const QByteArray row_name = QByteArray{"occurrence-"} + QByteArray::number(row++);
            QTest::newRow(row_name.constData()) << bytes(withItemField(u"occurrenceId"_s, value));
        }
        QTest::newRow("occurrence overflow") << bytes(withItemField(u"occurrenceId"_s, u"18446744073709551616"_s));
        QTest::newRow("current leading zero")
            << bytes(withQueueField(u"currentOccurrenceId"_s, u"09007199254740993"_s));
        rootPositionRows();
    }

    void rejectsNonCanonicalAndOverflowingDecimalStrings() {
        QFETCH(QByteArray, json);
        QCOMPARE(SessionJsonCodec::decode(json).status, SessionJsonDecodeStatus::Invalid);
    }

    void rejectsSemanticallyInvalidDecodedSnapshots() {
        QJsonObject duplicate = validObject();
        QJsonObject duplicate_queue = duplicate.value(u"queue"_s).toObject();
        QJsonArray duplicate_items = duplicate_queue.value(u"items"_s).toArray();
        QJsonObject duplicate_second = duplicate_items.at(1).toObject();
        duplicate_second.insert(u"occurrenceId"_s, u"1"_s);
        duplicate_items.replace(1, duplicate_second);
        duplicate_queue.insert(u"items"_s, duplicate_items);
        duplicate.insert(u"queue"_s, duplicate_queue);
        QCOMPARE(SessionJsonCodec::decode(bytes(duplicate)).status, SessionJsonDecodeStatus::Invalid);

        QCOMPARE(SessionJsonCodec::decode(bytes(withQueueField(u"currentOccurrenceId"_s, u"999"_s))).status,
                 SessionJsonDecodeStatus::Invalid);

        QJsonObject empty_with_current = validObject();
        QJsonObject empty_queue = empty_with_current.value(u"queue"_s).toObject();
        empty_queue.insert(u"items"_s, QJsonArray{});
        empty_queue.insert(u"currentOccurrenceId"_s, u"1"_s);
        empty_with_current.insert(u"queue"_s, empty_queue);
        QCOMPARE(SessionJsonCodec::decode(bytes(empty_with_current)).status, SessionJsonDecodeStatus::Invalid);

        QCOMPARE(SessionJsonCodec::decode(bytes(withItemField(u"sourcePath"_s, u""_s))).status,
                 SessionJsonDecodeStatus::Invalid);
        QString nul_path = uR"(C:\Música\A)"_s;
        nul_path.append(QChar{0});
        nul_path.append(u"B.mp3"_s);
        QCOMPARE(SessionJsonCodec::decode(bytes(withItemField(u"sourcePath"_s, nul_path))).status,
                 SessionJsonDecodeStatus::Invalid);

        QJsonObject position_without_current = withQueueField(u"currentOccurrenceId"_s, QJsonValue::Null);
        position_without_current.insert(u"positionMs"_s, u"1"_s);
        QCOMPARE(SessionJsonCodec::decode(bytes(position_without_current)).status, SessionJsonDecodeStatus::Invalid);
    }

    void refusesToEncodeInvalidSnapshots() {
        SessionSnapshot invalid = exactSnapshot();
        invalid.position_ms = -1;
        QVERIFY(SessionJsonCodec::encode(invalid).isEmpty());

        invalid = exactSnapshot();
        invalid.queue.items[1].occurrence_id = invalid.queue.items[0].occurrence_id;
        QVERIFY(SessionJsonCodec::encode(invalid).isEmpty());

        invalid = exactSnapshot();
        invalid.queue.items[0].source_path.clear();
        QVERIFY(SessionJsonCodec::encode(invalid).isEmpty());

        invalid = exactSnapshot();
        invalid.queue.current_occurrence_id.reset();
        QVERIFY(SessionJsonCodec::encode(invalid).isEmpty());

        invalid = exactSnapshot();
        std::wstring nul_path{L"C:\\Música\\A"};
        nul_path.push_back(L'\0');
        nul_path.append(L"B.mp3");
        invalid.queue.items[0].source_path = std::filesystem::path{nul_path};
        QVERIFY(SessionJsonCodec::encode(invalid).isEmpty());
    }

    void enforcesExactDocumentSizeBoundary() {
        QByteArray at_limit = bytes(validObject());
        QVERIFY(at_limit.size() < SessionJsonCodec::maximum_document_size);
        at_limit.append(SessionJsonCodec::maximum_document_size - at_limit.size(), ' ');
        QCOMPARE(at_limit.size(), SessionJsonCodec::maximum_document_size);
        QCOMPARE(SessionJsonCodec::decode(at_limit).status, SessionJsonDecodeStatus::Valid);

        at_limit.append(' ');
        QCOMPARE(SessionJsonCodec::decode(at_limit).status, SessionJsonDecodeStatus::Invalid);
    }

    void refusesToEncodeDocumentsAboveTheLimit() {
        const std::wstring huge_path(static_cast<std::size_t>(SessionJsonCodec::maximum_document_size), L'a');
        const SessionSnapshot snapshot{
            .queue =
                {
                    .items = {{{1}, std::filesystem::path{huge_path}}},
                    .current_occurrence_id = LocalQueueOccurrenceId{1},
                },
            .position_ms = 0,
        };
        QVERIFY(isValidSessionSnapshot(snapshot));

        QVERIFY(SessionJsonCodec::encode(snapshot).isEmpty());
    }

  private:
    static void rootPositionRows() {
        const QStringList invalid_positions{
            u""_s,
            u"00"_s,
            u"01"_s,
            u"+1"_s,
            u"-1"_s,
            u" 1"_s,
            u"1 "_s,
            u"1.0"_s,
            u"1e3"_s,
            QString{QChar{0x0661}},
            u"9223372036854775808"_s,
        };
        int row = 0;
        for (const QString& value : invalid_positions) {
            QJsonObject root = validObject();
            root.insert(u"positionMs"_s, value);
            const QByteArray row_name = QByteArray{"position-"} + QByteArray::number(row++);
            QTest::newRow(row_name.constData()) << bytes(root);
        }
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::SessionJsonCodecTests)

#include "SessionJsonCodecTests.moc"
