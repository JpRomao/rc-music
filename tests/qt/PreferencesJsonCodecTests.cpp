#include "adapters/PreferencesJsonCodec.hpp"

#include "m0/domain/WindowPlacement.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace music_player::m0::test {
namespace {

[[nodiscard]] QJsonObject validVersionOneObject() {
    return {{u"schemaVersion"_s, 1},
            {u"theme"_s, u"system"_s},
            {u"motion"_s, u"system"_s},
            {u"volume"_s, 0.8},
            {u"muted"_s, false},
            {u"skipBackwardSeconds"_s, 5},
            {u"skipForwardSeconds"_s, 5},
            {u"closeBehavior"_s, u"minimize_to_tray"_s},
            {u"restoreLastSession"_s, true},
            {u"startMinimized"_s, false},
            {u"queueEndBehavior"_s, u"stop_keep_selection"_s}};
}

[[nodiscard]] QJsonObject validObject() {
    QJsonObject object = validVersionOneObject();
    object.insert(u"schemaVersion"_s, 2);
    object.insert(u"windowPlacement"_s, QJsonValue::Null);
    return object;
}

[[nodiscard]] QByteArray bytes(const QJsonObject& object) {
    return QJsonDocument{object}.toJson(QJsonDocument::Compact);
}

[[nodiscard]] QString themeName(const ThemeMode theme) {
    switch (theme) {
    case ThemeMode::System:
        return u"system"_s;
    case ThemeMode::Light:
        return u"light"_s;
    case ThemeMode::Dark:
        return u"dark"_s;
    }
    return {};
}

[[nodiscard]] QString motionName(const MotionMode motion) {
    return motion == MotionMode::Reduced ? u"reduced"_s : u"system"_s;
}

[[nodiscard]] QString closeBehaviorName(const CloseBehavior behavior) {
    return behavior == CloseBehavior::Exit ? u"exit"_s : u"minimize_to_tray"_s;
}

[[nodiscard]] QString queueEndBehaviorName(const QueueEndBehavior behavior) {
    return behavior == QueueEndBehavior::StopClearSelection ? u"stop_clear_selection"_s : u"stop_keep_selection"_s;
}

[[nodiscard]] QJsonValue windowPlacementValue(const std::optional<WindowPlacement>& placement) {
    if (!placement)
        return QJsonValue::Null;
    return QJsonObject{{u"x"_s, placement->x},
                       {u"y"_s, placement->y},
                       {u"width"_s, placement->width},
                       {u"height"_s, placement->height}};
}

[[nodiscard]] QJsonObject expectedObject(const Preferences& preferences) {
    return {{u"schemaVersion"_s, PreferencesJsonCodec::schema_version},
            {u"theme"_s, themeName(preferences.theme)},
            {u"motion"_s, motionName(preferences.motion)},
            {u"volume"_s, preferences.volume},
            {u"muted"_s, preferences.muted},
            {u"skipBackwardSeconds"_s, preferences.skip_backward_seconds},
            {u"skipForwardSeconds"_s, preferences.skip_forward_seconds},
            {u"closeBehavior"_s, closeBehaviorName(preferences.close_behavior)},
            {u"restoreLastSession"_s, preferences.restore_last_session},
            {u"startMinimized"_s, preferences.start_minimized},
            {u"queueEndBehavior"_s, queueEndBehaviorName(preferences.queue_end_behavior)},
            {u"windowPlacement"_s, windowPlacementValue(preferences.window_placement)},
            {u"repeatQueueEnabled"_s, preferences.repeat_queue_enabled},
            {u"shuffleQueueEnabled"_s, preferences.shuffle_queue_enabled},
            {u"lyricsTextSize"_s, preferences.lyrics_text_size}};
}

void comparePreferences(const Preferences& actual, const Preferences& expected) {
    QCOMPARE(actual.theme, expected.theme);
    QCOMPARE(actual.motion, expected.motion);
    QCOMPARE(actual.volume, expected.volume);
    QCOMPARE(actual.muted, expected.muted);
    QCOMPARE(actual.skip_backward_seconds, expected.skip_backward_seconds);
    QCOMPARE(actual.skip_forward_seconds, expected.skip_forward_seconds);
    QCOMPARE(actual.close_behavior, expected.close_behavior);
    QCOMPARE(actual.restore_last_session, expected.restore_last_session);
    QCOMPARE(actual.start_minimized, expected.start_minimized);
    QCOMPARE(actual.queue_end_behavior, expected.queue_end_behavior);
    QVERIFY(actual.window_placement == expected.window_placement);
    QCOMPARE(actual.repeat_queue_enabled, expected.repeat_queue_enabled);
    QCOMPARE(actual.shuffle_queue_enabled, expected.shuffle_queue_enabled);
    QCOMPARE(actual.lyrics_text_size, expected.lyrics_text_size);
}

[[nodiscard]] QByteArray jsonWithVolumeToken(const QByteArrayView token) {
    return QByteArray{"{\"schemaVersion\":2,\"theme\":\"system\",\"motion\":\"system\",\"volume\":"} +
           token.toByteArray() +
           QByteArray{",\"muted\":false,\"skipBackwardSeconds\":5,\"skipForwardSeconds\":5,"
                      "\"closeBehavior\":\"minimize_to_tray\",\"restoreLastSession\":true,"
                      "\"startMinimized\":false,\"queueEndBehavior\":\"stop_keep_selection\","
                      "\"windowPlacement\":null}"};
}

} // namespace

class PreferencesJsonCodecTests final : public QObject {
    Q_OBJECT

  private slots:
    void downloadDirectoryRoundTripsAndDefaultsForLegacyPreferences() {
        auto object = validObject();
        const auto directory = u"C:/Música/Publicações"_s;
        object.insert(u"downloadDirectory"_s, directory);
        const auto decoded = PreferencesJsonCodec::decode(bytes(object));
        QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Valid);
        QVERIFY(decoded.preferences);
        QCOMPARE(QJsonDocument::fromJson(PreferencesJsonCodec::encode(*decoded.preferences))
                     .object()
                     .value(u"downloadDirectory"_s),
                 QJsonValue{directory});
        for (const auto& legacy : {validVersionOneObject(), validObject()}) {
            const auto old = PreferencesJsonCodec::decode(bytes(legacy));
            QVERIFY(old.preferences);
            QVERIFY(!QJsonDocument::fromJson(PreferencesJsonCodec::encode(*old.preferences))
                         .object()
                         .contains(u"downloadDirectory"_s));
        }
    }

    void rejectsUnsupportedDownloadDirectoryValues() {
        for (const auto& value : {QJsonValue{false}, QJsonValue{u"//server/music"_s}, QJsonValue{u"C:/a/../b"_s},
                                  QJsonValue{u"C:/alternate:stream"_s}, QJsonValue{u"C:/a "_s}}) {
            auto object = validObject();
            object.insert(u"downloadDirectory"_s, value);
            const auto decoded = PreferencesJsonCodec::decode(bytes(object));
            QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Invalid);
        }
    }

    void repeatQueueRoundTripsAndDefaultsForLegacyVersions() {
        for (const bool enabled : {false, true}) {
            auto object = validObject();
            object.insert(u"repeatQueueEnabled"_s, enabled);
            const auto decoded = PreferencesJsonCodec::decode(bytes(object));
            QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Valid);
            QVERIFY(decoded.preferences.has_value());
            const auto encoded = QJsonDocument::fromJson(PreferencesJsonCodec::encode(*decoded.preferences)).object();
            QCOMPARE(encoded.value(u"repeatQueueEnabled"_s), QJsonValue{enabled});
        }
        for (const auto& object : {validVersionOneObject(), validObject()}) {
            const auto decoded = PreferencesJsonCodec::decode(bytes(object));
            QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Valid);
            QVERIFY(decoded.preferences.has_value());
            const auto encoded = QJsonDocument::fromJson(PreferencesJsonCodec::encode(*decoded.preferences)).object();
            QCOMPARE(encoded.value(u"repeatQueueEnabled"_s), QJsonValue{false});
        }
    }

    void shuffleQueueRoundTripsAndDefaultsForLegacyVersions() {
        for (const bool enabled : {false, true}) {
            auto object = validObject();
            object.insert(u"shuffleQueueEnabled"_s, enabled);
            const auto decoded = PreferencesJsonCodec::decode(bytes(object));
            QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Valid);
            QVERIFY(decoded.preferences.has_value());
            const auto encoded = QJsonDocument::fromJson(PreferencesJsonCodec::encode(*decoded.preferences)).object();
            QCOMPARE(encoded.value(u"shuffleQueueEnabled"_s), QJsonValue{enabled});
        }
        for (const auto& object : {validVersionOneObject(), validObject()}) {
            const auto decoded = PreferencesJsonCodec::decode(bytes(object));
            QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Valid);
            QVERIFY(decoded.preferences.has_value());
            const auto encoded = QJsonDocument::fromJson(PreferencesJsonCodec::encode(*decoded.preferences)).object();
            QCOMPARE(encoded.value(u"shuffleQueueEnabled"_s), QJsonValue{false});
        }
    }

    void rejectsInvalidRepeatQueueValues() {
        for (const auto& value : {QJsonValue{QJsonValue::Null}, QJsonValue{1}, QJsonValue{u"true"_s},
                                  QJsonValue{QJsonObject{}}, QJsonValue{QJsonArray{}}}) {
            auto object = validObject();
            object.insert(u"repeatQueueEnabled"_s, value);
            const auto decoded = PreferencesJsonCodec::decode(bytes(object));
            QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Invalid);
            QVERIFY(!decoded.preferences.has_value());
        }
    }

    void rejectsInvalidShuffleQueueValue() {
        auto object = validObject();
        object.insert(u"shuffleQueueEnabled"_s, u"true"_s);
        QCOMPARE(PreferencesJsonCodec::decode(bytes(object)).status, PreferencesJsonDecodeStatus::Invalid);
    }

    void encodesEveryFieldAndRoundTripsDeterministically_data() {
        QTest::addColumn<Preferences>("expected");

        QTest::newRow("defaults") << Preferences{};
        QTest::newRow("approved 7 and 20 second values")
            << Preferences{.theme = ThemeMode::Dark,
                           .motion = MotionMode::Reduced,
                           .volume = 0.375,
                           .muted = true,
                           .skip_backward_seconds = 7,
                           .skip_forward_seconds = 20,
                           .close_behavior = CloseBehavior::Exit,
                           .restore_last_session = false,
                           .start_minimized = true,
                           .queue_end_behavior = QueueEndBehavior::StopClearSelection,
                           .window_placement = WindowPlacement{-1'920, 40, 960, 640},
                           .repeat_queue_enabled = true,
                           .shuffle_queue_enabled = true,
                           .lyrics_text_size = 24};
        QTest::newRow("numeric boundaries") << Preferences{
            .theme = ThemeMode::Light,
            .motion = MotionMode::System,
            .volume = 1.0,
            .muted = false,
            .skip_backward_seconds = 1,
            .skip_forward_seconds = 300,
            .close_behavior = CloseBehavior::MinimizeToTray,
            .restore_last_session = true,
            .start_minimized = false,
            .queue_end_behavior = QueueEndBehavior::StopKeepSelection,
            .window_placement = WindowPlacement{std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
                                                std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}};
    }

    void encodesEveryFieldAndRoundTripsDeterministically() {
        QFETCH(Preferences, expected);

        const QByteArray first = PreferencesJsonCodec::encode(expected);
        QVERIFY(!first.isEmpty());
        QCOMPARE(PreferencesJsonCodec::encode(expected), first);

        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(first, &parse_error);
        QCOMPARE(parse_error.error, QJsonParseError::NoError);
        QVERIFY(document.isObject());
        const QJsonObject object = document.object();
        QCOMPARE(object.size(), 15);
        QSet<QString> actual_keys;
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
            actual_keys.insert(iterator.key());
        const QSet<QString> expected_keys{
            u"schemaVersion"_s,
            u"theme"_s,
            u"motion"_s,
            u"volume"_s,
            u"muted"_s,
            u"skipBackwardSeconds"_s,
            u"skipForwardSeconds"_s,
            u"closeBehavior"_s,
            u"restoreLastSession"_s,
            u"startMinimized"_s,
            u"queueEndBehavior"_s,
            u"windowPlacement"_s,
            u"repeatQueueEnabled"_s,
            u"shuffleQueueEnabled"_s,
            u"lyricsTextSize"_s,
        };
        QCOMPARE(actual_keys, expected_keys);
        QCOMPARE(object.value(u"schemaVersion"_s), QJsonValue{2});
        QCOMPARE(object, expectedObject(expected));

        const PreferencesJsonDecodeResult decoded = PreferencesJsonCodec::decode(first);
        QCOMPARE(decoded.status, PreferencesJsonDecodeStatus::Valid);
        QVERIFY(decoded.preferences.has_value());
        comparePreferences(*decoded.preferences, expected);
        QCOMPARE(PreferencesJsonCodec::encode(*decoded.preferences), first);
    }

    void refusesToEncodeValuesOutsideTheSchema_data() {
        QTest::addColumn<Preferences>("invalid");

        Preferences invalid;
        invalid.volume = std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("NaN volume") << invalid;
        invalid = {};
        invalid.volume = std::numeric_limits<double>::infinity();
        QTest::newRow("infinite volume") << invalid;
        invalid = {};
        invalid.volume = -0.001;
        QTest::newRow("negative volume") << invalid;
        invalid = {};
        invalid.volume = 1.001;
        QTest::newRow("volume above one") << invalid;
        invalid = {};
        invalid.skip_backward_seconds = 0;
        QTest::newRow("backward skip below one") << invalid;
        invalid = {};
        invalid.skip_forward_seconds = 301;
        QTest::newRow("forward skip above 300") << invalid;
        invalid = {};
        invalid.theme = static_cast<ThemeMode>(99);
        QTest::newRow("unknown theme") << invalid;
        invalid = {};
        invalid.motion = static_cast<MotionMode>(99);
        QTest::newRow("unknown motion") << invalid;
        invalid = {};
        invalid.close_behavior = static_cast<CloseBehavior>(99);
        QTest::newRow("unknown close behavior") << invalid;
        invalid = {};
        invalid.queue_end_behavior = static_cast<QueueEndBehavior>(99);
        QTest::newRow("unknown queue end behavior") << invalid;
        invalid = {};
        invalid.window_placement = WindowPlacement{0, 0, 0, 520};
        QTest::newRow("zero window width") << invalid;
        invalid = {};
        invalid.window_placement = WindowPlacement{0, 0, 720, -1};
        QTest::newRow("negative window height") << invalid;
    }

    void refusesToEncodeValuesOutsideTheSchema() {
        QFETCH(Preferences, invalid);
        QVERIFY(PreferencesJsonCodec::encode(invalid).isEmpty());
    }

    void acceptsBoundaryValuesAndUnknownKeys() {
        QJsonObject object = validObject();
        object.insert(u"volume"_s, 0.0);
        object.insert(u"skipBackwardSeconds"_s, 1);
        object.insert(u"skipForwardSeconds"_s, 300);
        object.insert(u"futureOptionalField"_s, QJsonObject{{u"nested"_s, true}});

        const PreferencesJsonDecodeResult result = PreferencesJsonCodec::decode(bytes(object));

        QCOMPARE(result.status, PreferencesJsonDecodeStatus::Valid);
        QVERIFY(result.preferences.has_value());
        QCOMPARE(result.preferences->volume, 0.0);
        QCOMPARE(result.preferences->skip_backward_seconds, 1);
        QCOMPARE(result.preferences->skip_forward_seconds, 300);
    }

    void decodesVersionOneWithoutRequiringWindowPlacement() {
        QJsonObject object = validVersionOneObject();
        object.insert(u"futureOptionalField"_s, QJsonObject{{u"nested"_s, true}});

        const PreferencesJsonDecodeResult result = PreferencesJsonCodec::decode(bytes(object));

        QCOMPARE(result.status, PreferencesJsonDecodeStatus::Valid);
        QVERIFY(result.preferences.has_value());
        QVERIFY(!result.preferences->window_placement.has_value());
    }

    void decodesVersionTwoWindowPlacementWithNegativeCoordinates() {
        QJsonObject object = validObject();
        object.insert(u"windowPlacement"_s,
                      QJsonObject{{u"x"_s, -1'920}, {u"y"_s, -40}, {u"width"_s, 960}, {u"height"_s, 640}});

        const PreferencesJsonDecodeResult result = PreferencesJsonCodec::decode(bytes(object));

        QCOMPARE(result.status, PreferencesJsonDecodeStatus::Valid);
        QVERIFY(result.preferences.has_value());
        const std::optional<WindowPlacement> expected{WindowPlacement{-1'920, -40, 960, 640}};
        QCOMPARE(result.preferences->window_placement, expected);
    }

    void classifiesFutureVersionBeforeReadingItsFields_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("version only") << QByteArray{R"({"schemaVersion":3})"};
        QTest::newRow("missing current fields") << QByteArray{R"({"schemaVersion":99,"theme":false})"};
        QTest::newRow("future fields have hostile types")
            << QByteArray{R"({"schemaVersion":3,"volume":"C:\\private\\preferences.json"})"};
    }

    void classifiesFutureVersionBeforeReadingItsFields() {
        QFETCH(QByteArray, json);

        const PreferencesJsonDecodeResult result = PreferencesJsonCodec::decode(json);

        QCOMPARE(result.status, PreferencesJsonDecodeStatus::FutureVersion);
        QVERIFY(!result.preferences.has_value());
    }

    void rejectsMalformedTruncatedAndNonObjectDocuments_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("whitespace") << QByteArray{" \r\n\t"};
        QTest::newRow("truncated") << QByteArray{R"({"schemaVersion":1)"};
        QTest::newRow("trailing comma") << QByteArray{R"({"schemaVersion":1,})"};
        QTest::newRow("array") << QByteArray{R"([])"};
        QTest::newRow("null") << QByteArray{R"(null)"};
        QTest::newRow("string") << QByteArray{R"("C:\\private\\preferences.json")"};
    }

    void rejectsMalformedTruncatedAndNonObjectDocuments() {
        QFETCH(QByteArray, json);

        const PreferencesJsonDecodeResult result = PreferencesJsonCodec::decode(json);

        QCOMPARE(result.status, PreferencesJsonDecodeStatus::Invalid);
        QVERIFY(!result.preferences.has_value());
    }

    void rejectsMissingFields_data() {
        QTest::addColumn<QByteArray>("json");
        const QJsonObject complete = validObject();
        for (auto iterator = complete.constBegin(); iterator != complete.constEnd(); ++iterator) {
            QJsonObject missing = complete;
            missing.remove(iterator.key());
            QTest::newRow(iterator.key().toUtf8().constData()) << bytes(missing);
        }
    }

    void rejectsMissingFields() {
        QFETCH(QByteArray, json);
        const PreferencesJsonDecodeResult result = PreferencesJsonCodec::decode(json);
        QCOMPARE(result.status, PreferencesJsonDecodeStatus::Invalid);
        QVERIFY(!result.preferences.has_value());
    }

    void rejectsWrongJsonTypes_data() {
        QTest::addColumn<QByteArray>("json");
        const QList<std::pair<QString, QJsonValue>> wrong_values{
            {u"schemaVersion"_s, u"1"_s},
            {u"theme"_s, false},
            {u"motion"_s, QJsonObject{}},
            {u"volume"_s, u"0.8"_s},
            {u"muted"_s, 0},
            {u"skipBackwardSeconds"_s, u"7"_s},
            {u"skipForwardSeconds"_s, false},
            {u"closeBehavior"_s, 1},
            {u"restoreLastSession"_s, QJsonArray{}},
            {u"startMinimized"_s, QJsonValue::Null},
            {u"queueEndBehavior"_s, true},
            {u"windowPlacement"_s, QJsonArray{}},
        };
        for (const auto& [key, wrong_value] : wrong_values) {
            QJsonObject object = validObject();
            object.insert(key, wrong_value);
            QTest::newRow(key.toUtf8().constData()) << bytes(object);
        }
    }

    void rejectsWrongJsonTypes() {
        QFETCH(QByteArray, json);
        QCOMPARE(PreferencesJsonCodec::decode(json).status, PreferencesJsonDecodeStatus::Invalid);
    }

    void rejectsUnknownEnumValues_data() {
        QTest::addColumn<QByteArray>("json");
        const QList<std::pair<QString, QString>> unknown_values{
            {u"theme"_s, u"SYSTEM"_s},
            {u"motion"_s, u"normal"_s},
            {u"closeBehavior"_s, u"tray"_s},
            {u"queueEndBehavior"_s, u"loop"_s},
        };
        for (const auto& [key, unknown_value] : unknown_values) {
            QJsonObject object = validObject();
            object.insert(key, unknown_value);
            QTest::newRow(key.toUtf8().constData()) << bytes(object);
        }
    }

    void rejectsUnknownEnumValues() {
        QFETCH(QByteArray, json);
        QCOMPARE(PreferencesJsonCodec::decode(json).status, PreferencesJsonDecodeStatus::Invalid);
    }

    void rejectsInvalidNumbers_data() {
        QTest::addColumn<QByteArray>("json");

        const QList<std::pair<QString, double>> invalid_numbers{
            {u"schemaVersion"_s, 0.0},
            {u"schemaVersion"_s, -1.0},
            {u"schemaVersion"_s, 1.5},
            {u"volume"_s, -0.001},
            {u"volume"_s, 1.001},
            {u"skipBackwardSeconds"_s, 0.0},
            {u"skipBackwardSeconds"_s, 301.0},
            {u"skipBackwardSeconds"_s, 7.5},
            {u"skipForwardSeconds"_s, 0.0},
            {u"skipForwardSeconds"_s, 301.0},
            {u"skipForwardSeconds"_s, 20.25},
        };
        int row = 0;
        for (const auto& [key, invalid_number] : invalid_numbers) {
            QJsonObject object = validObject();
            object.insert(key, invalid_number);
            const QByteArray row_name = key.toUtf8() + QByteArray::number(row++);
            QTest::newRow(row_name.constData()) << bytes(object);
        }
        QTest::newRow("NaN") << jsonWithVolumeToken("NaN");
        QTest::newRow("positive infinity") << jsonWithVolumeToken("Infinity");
        QTest::newRow("negative infinity") << jsonWithVolumeToken("-Infinity");
        QTest::newRow("overflowing exponent") << jsonWithVolumeToken("1e999");
    }

    void rejectsInvalidNumbers() {
        QFETCH(QByteArray, json);
        QCOMPARE(PreferencesJsonCodec::decode(json).status, PreferencesJsonDecodeStatus::Invalid);
    }

    void rejectsInvalidWindowPlacement_data() {
        QTest::addColumn<QJsonValue>("windowPlacement");

        const QJsonObject valid{{u"x"_s, 0}, {u"y"_s, 0}, {u"width"_s, 720}, {u"height"_s, 520}};
        for (const QString& key : {u"x"_s, u"y"_s, u"width"_s, u"height"_s}) {
            QJsonObject missing = valid;
            missing.remove(key);
            QTest::newRow(qPrintable(u"missing "_s + key)) << QJsonValue{missing};
        }

        QJsonObject invalid = valid;
        invalid.insert(u"x"_s, u"0"_s);
        QTest::newRow("string x") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"height"_s, false);
        QTest::newRow("boolean height") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"x"_s, 0.5);
        QTest::newRow("fractional x") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"width"_s, 720.5);
        QTest::newRow("fractional width") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"x"_s, static_cast<double>(std::numeric_limits<int>::max()) + 1.0);
        QTest::newRow("x above int") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"y"_s, static_cast<double>(std::numeric_limits<int>::min()) - 1.0);
        QTest::newRow("y below int") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"width"_s, 0);
        QTest::newRow("zero width") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"height"_s, -1);
        QTest::newRow("negative height") << QJsonValue{invalid};
        invalid = valid;
        invalid.insert(u"height"_s, static_cast<double>(std::numeric_limits<int>::max()) + 1.0);
        QTest::newRow("height above int") << QJsonValue{invalid};
        QTest::newRow("wrong outer type") << QJsonValue{u"automatic"_s};
    }

    void rejectsInvalidWindowPlacement() {
        QFETCH(QJsonValue, windowPlacement);
        QJsonObject object = validObject();
        object.insert(u"windowPlacement"_s, windowPlacement);

        QCOMPARE(PreferencesJsonCodec::decode(bytes(object)).status, PreferencesJsonDecodeStatus::Invalid);
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::PreferencesJsonCodecTests)

#include "PreferencesJsonCodecTests.moc"
