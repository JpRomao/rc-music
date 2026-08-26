#include "m0/adapters/fixture/FixtureRepository.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace music_player::m0 {
namespace {

[[noreturn]] void fail(const QString& message) { throw FixtureValidationError(message); }

void require(const bool condition, const QString& message) {
    if (!condition) {
        fail(message);
    }
}

void requireExactKeys(const QJsonObject& object, const std::initializer_list<const char*> keys,
                      const QString& context) {
    QSet<QString> expected;
    for (const char* key : keys) {
        expected.insert(QString::fromLatin1(key));
    }
    QSet<QString> actual;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        actual.insert(iterator.key());
    }
    require(actual == expected, context + QStringLiteral(" keys differ from the frozen schema"));
}

[[nodiscard]] QJsonDocument readDocument(const QString& path, const QString& context,
                                         const QByteArray& expected_sha256) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), context + QStringLiteral(" cannot be opened read-only"));
    const QByteArray bytes = file.readAll();
    require(file.error() == QFileDevice::NoError, context + QStringLiteral(" cannot be read completely"));
    require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == expected_sha256,
            context + QStringLiteral(" bytes differ from the protocol-anchored fixture manifest"));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    require(parse_error.error == QJsonParseError::NoError && document.isObject(),
            context + QStringLiteral(" is not a valid JSON object"));
    return document;
}

[[nodiscard]] QString requiredString(const QJsonObject& object, const char* key, const QString& context) {
    const QJsonValue value = object.value(QString::fromLatin1(key));
    require(value.isString(),
            context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" must be a string"));
    return value.toString();
}

[[nodiscard]] bool requiredBool(const QJsonObject& object, const char* key, const QString& context) {
    const QJsonValue value = object.value(QString::fromLatin1(key));
    require(value.isBool(),
            context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" must be a bool"));
    return value.toBool();
}

[[nodiscard]] std::int64_t requiredInteger(const QJsonObject& object, const char* key, const QString& context) {
    const QJsonValue value = object.value(QString::fromLatin1(key));
    require(value.isDouble(),
            context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" must be an integer"));
    const double number = value.toDouble();
    constexpr double maximum_exact_json_integer = 9'007'199'254'740'991.0;
    require(std::isfinite(number) && std::trunc(number) == number && number >= -maximum_exact_json_integer &&
                number <= maximum_exact_json_integer,
            context + QStringLiteral(".") + QString::fromLatin1(key) +
                QStringLiteral(" is outside the exact JSON integer range"));
    return static_cast<std::int64_t>(number);
}

[[nodiscard]] int requiredInt(const QJsonObject& object, const char* key, const QString& context) {
    const std::int64_t number = requiredInteger(object, key, context);
    require(number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max(),
            context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" is outside int range"));
    return static_cast<int>(number);
}

[[nodiscard]] QJsonObject requiredObject(const QJsonObject& object, const char* key, const QString& context) {
    const QJsonValue value = object.value(QString::fromLatin1(key));
    require(value.isObject(),
            context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" must be an object"));
    return value.toObject();
}

[[nodiscard]] QJsonArray requiredArray(const QJsonObject& object, const char* key, const QString& context) {
    const QJsonValue value = object.value(QString::fromLatin1(key));
    require(value.isArray(),
            context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" must be an array"));
    return value.toArray();
}

[[nodiscard]] QString nullableString(const QJsonObject& object, const char* key, const QString& context) {
    return object.value(QString::fromLatin1(key)).isNull() ? QString{} : requiredString(object, key, context);
}

[[nodiscard]] std::optional<std::int64_t> nullableInteger(const QJsonObject& object, const char* key,
                                                          const QString& context) {
    return object.value(QString::fromLatin1(key)).isNull()
               ? std::nullopt
               : std::optional<std::int64_t>{requiredInteger(object, key, context)};
}

[[nodiscard]] const QueueItem& itemById(const QList<QueueItem>& queue, const QString& occurrence_id) {
    for (const QueueItem& item : queue) {
        if (item.occurrence_id == occurrence_id) {
            return item;
        }
    }
    fail(QStringLiteral("required hostile item is missing: ") + occurrence_id);
}

void validateFixtureStates(const QJsonArray& states, FixtureBundle& bundle) {
    require(states.size() == 3, QStringLiteral("fixture must contain exactly three states"));
    const QStringList expected_ids{QStringLiteral("empty"), QStringLiteral("playing"), QStringLiteral("error")};
    const QStringList expected_playback{QStringLiteral("unloaded"), QStringLiteral("playing"), QStringLiteral("error")};
    for (qsizetype index = 0; index < states.size(); ++index) {
        require(states.at(index).isObject(), QStringLiteral("fixture state must be an object"));
        const QJsonObject state = states.at(index).toObject();
        requireExactKeys(state,
                         {"state_id", "playback_state", "queue_loaded", "current_occurrence_id",
                          "context_occurrence_id", "position_ms", "duration_ms", "seekable", "asset_id", "message_key",
                          "primary_action_id", "error_code"},
                         QStringLiteral("fixture state"));
        FixtureState parsed{
            .state_id = requiredString(state, "state_id", QStringLiteral("fixture state")),
            .playback_state = requiredString(state, "playback_state", QStringLiteral("fixture state")),
            .queue_loaded = requiredBool(state, "queue_loaded", QStringLiteral("fixture state")),
            .current_occurrence_id = nullableString(state, "current_occurrence_id", QStringLiteral("fixture state")),
            .position_ms = nullableInteger(state, "position_ms", QStringLiteral("fixture state")),
            .duration_ms = nullableInteger(state, "duration_ms", QStringLiteral("fixture state")),
            .seekable = requiredBool(state, "seekable", QStringLiteral("fixture state")),
            .error_code = nullableString(state, "error_code", QStringLiteral("fixture state")),
        };
        require(parsed.state_id == expected_ids.at(index) && parsed.playback_state == expected_playback.at(index),
                QStringLiteral("fixture state identity/order drifted"));
        bundle.states.append(std::move(parsed));
    }
    const FixtureState& empty = bundle.states.at(0);
    const FixtureState& playing = bundle.states.at(1);
    const FixtureState& error = bundle.states.at(2);
    require(!empty.queue_loaded && empty.current_occurrence_id.isEmpty() && !empty.position_ms.has_value() &&
                !empty.duration_ms.has_value() && !empty.seekable && playing.queue_loaded &&
                playing.current_occurrence_id == QStringLiteral("queue-0017") && playing.position_ms == 84'000 &&
                playing.duration_ms == 248'000 && playing.seekable && error.queue_loaded &&
                error.current_occurrence_id == QStringLiteral("queue-0999") && !error.position_ms.has_value() &&
                !error.duration_ms.has_value() && !error.seekable &&
                error.error_code == QStringLiteral("fixture_source_unavailable"),
            QStringLiteral("fixture state semantics drifted"));
}

void validateFixtureControls(const QJsonArray& controls, FixtureBundle& bundle) {
    const QList<FixtureControl> expected{
        {QStringLiteral("open-audio"), QStringLiteral("button"), QStringLiteral("action_open_audio")},
        {QStringLiteral("play-pause"), QStringLiteral("button"), QStringLiteral("action_pause")},
        {QStringLiteral("previous"), QStringLiteral("button"), QStringLiteral("action_previous")},
        {QStringLiteral("next"), QStringLiteral("button"), QStringLiteral("action_next")},
        {QStringLiteral("skip-backward"), QStringLiteral("button"), QStringLiteral("skip_backward_name")},
        {QStringLiteral("skip-forward"), QStringLiteral("button"), QStringLiteral("skip_forward_name")},
        {QStringLiteral("progress"), QStringLiteral("slider"), QStringLiteral("progress_name")},
        {QStringLiteral("volume"), QStringLiteral("slider"), QStringLiteral("volume_name")},
        {QStringLiteral("mute"), QStringLiteral("button"), QStringLiteral("action_mute")},
        {QStringLiteral("queue"), QStringLiteral("button"), QStringLiteral("queue_title")},
        {QStringLiteral("preferences"), QStringLiteral("button"), QStringLiteral("preferences_title")},
    };
    require(controls.size() == expected.size(), QStringLiteral("fixture control count drifted"));
    for (qsizetype index = 0; index < controls.size(); ++index) {
        require(controls.at(index).isObject(), QStringLiteral("fixture control must be an object"));
        const QJsonObject control = controls.at(index).toObject();
        requireExactKeys(control, {"control_id", "role", "name_key"}, QStringLiteral("fixture control"));
        FixtureControl parsed{requiredString(control, "control_id", QStringLiteral("fixture control")),
                              requiredString(control, "role", QStringLiteral("fixture control")),
                              requiredString(control, "name_key", QStringLiteral("fixture control"))};
        const FixtureControl& wanted = expected.at(index);
        require(parsed.control_id == wanted.control_id && parsed.role == wanted.role &&
                    parsed.name_key == wanted.name_key,
                QStringLiteral("fixture control order/content drifted"));
        bundle.controls.append(std::move(parsed));
    }
}

void validateFixedCases(const QJsonArray& fixed_cases) {
    const QStringList ids{QStringLiteral("duplicate-exact"), QStringLiteral("unicode-normalization"),
                          QStringLiteral("unicode-non-bmp"), QStringLiteral("literal-markup"),
                          QStringLiteral("long-text"),       QStringLiteral("unavailable")};
    const QList<QStringList> occurrence_ids{{QStringLiteral("queue-0007"), QStringLiteral("queue-0707")},
                                            {QStringLiteral("queue-0017"), QStringLiteral("queue-0018")},
                                            {QStringLiteral("queue-0023")},
                                            {QStringLiteral("queue-0042")},
                                            {QStringLiteral("queue-0512")},
                                            {QStringLiteral("queue-0999")}};
    const QStringList expectations{QStringLiteral("distinct_occurrences_same_source"),
                                   QStringLiteral("distinct_presentations_same_nfc_source"),
                                   QStringLiteral("preserve_unicode_scalars"),
                                   QStringLiteral("render_as_plain_text"),
                                   QStringLiteral("512_unicode_scalars"),
                                   QStringLiteral("recoverable_error_preserves_queue")};
    require(fixed_cases.size() == ids.size(), QStringLiteral("fixture fixed-case count drifted"));
    for (qsizetype index = 0; index < fixed_cases.size(); ++index) {
        const QJsonObject fixed_case = fixed_cases.at(index).toObject();
        requireExactKeys(fixed_case, {"case_id", "occurrence_ids", "expectation"}, QStringLiteral("fixed case"));
        require(requiredString(fixed_case, "case_id", QStringLiteral("fixed case")) == ids.at(index) &&
                    requiredString(fixed_case, "expectation", QStringLiteral("fixed case")) == expectations.at(index),
                QStringLiteral("fixed-case identity/expectation drifted"));
        const QJsonArray actual_ids = requiredArray(fixed_case, "occurrence_ids", QStringLiteral("fixed case"));
        require(actual_ids.size() == occurrence_ids.at(index).size(), QStringLiteral("fixed-case coverage drifted"));
        for (qsizetype item_index = 0; item_index < actual_ids.size(); ++item_index) {
            require(actual_ids.at(item_index).isString() &&
                        actual_ids.at(item_index).toString() == occurrence_ids.at(index).at(item_index),
                    QStringLiteral("fixed-case occurrence ID drifted"));
        }
    }
}

void validateFixture(const QJsonObject& fixture, FixtureBundle& bundle) {
    requireExactKeys(fixture,
                     {"schema_id", "schema_version", "fixture_id", "locale", "default_state_id", "window",
                      "layout_tokens", "strings", "controls", "themes", "motion", "preferences", "fake_backend",
                      "states", "queue", "fixed_cases", "track_details", "assets"},
                     QStringLiteral("fixture"));
    bundle.schema_id = requiredString(fixture, "schema_id", QStringLiteral("fixture"));
    bundle.schema_version = requiredInt(fixture, "schema_version", QStringLiteral("fixture"));
    bundle.fixture_id = requiredString(fixture, "fixture_id", QStringLiteral("fixture"));
    bundle.locale = requiredString(fixture, "locale", QStringLiteral("fixture"));
    bundle.default_state_id = requiredString(fixture, "default_state_id", QStringLiteral("fixture"));
    require(bundle.schema_id == QStringLiteral("m0.shell-fixture.v1") && bundle.schema_version == 1 &&
                bundle.fixture_id == QStringLiteral("M0-SHELL-F1") && bundle.locale == QStringLiteral("pt-BR") &&
                bundle.default_state_id == QStringLiteral("empty"),
            QStringLiteral("fixture identity/locale/default state drifted"));
    validateFixtureStates(requiredArray(fixture, "states", QStringLiteral("fixture")), bundle);
    validateFixtureControls(requiredArray(fixture, "controls", QStringLiteral("fixture")), bundle);

    const QJsonObject preferences = requiredObject(fixture, "preferences", QStringLiteral("fixture"));
    requireExactKeys(preferences, {"defaults", "constraints", "rules"}, QStringLiteral("preferences"));
    const QJsonObject defaults = requiredObject(preferences, "defaults", QStringLiteral("preferences"));
    requireExactKeys(defaults,
                     {"theme", "motion", "volume_percent", "muted", "skip_backward_seconds", "skip_forward_seconds",
                      "close_behavior", "restore_last_session", "start_minimized", "queue_end_behavior"},
                     QStringLiteral("preference defaults"));
    bundle.defaults = {
        requiredString(defaults, "theme", QStringLiteral("defaults")),
        requiredString(defaults, "motion", QStringLiteral("defaults")),
        requiredInt(defaults, "volume_percent", QStringLiteral("defaults")),
        requiredBool(defaults, "muted", QStringLiteral("defaults")),
        requiredInt(defaults, "skip_backward_seconds", QStringLiteral("defaults")),
        requiredInt(defaults, "skip_forward_seconds", QStringLiteral("defaults")),
        requiredString(defaults, "close_behavior", QStringLiteral("defaults")),
        requiredBool(defaults, "restore_last_session", QStringLiteral("defaults")),
        requiredBool(defaults, "start_minimized", QStringLiteral("defaults")),
        requiredString(defaults, "queue_end_behavior", QStringLiteral("defaults")),
    };
    require(bundle.defaults.theme == QStringLiteral("system") && bundle.defaults.motion == QStringLiteral("system") &&
                bundle.defaults.volume_percent == 80 && !bundle.defaults.muted &&
                bundle.defaults.skip_backward_seconds == 5 && bundle.defaults.skip_forward_seconds == 5 &&
                bundle.defaults.close_behavior == QStringLiteral("minimize_to_tray") &&
                bundle.defaults.restore_last_session && !bundle.defaults.start_minimized &&
                bundle.defaults.queue_end_behavior == QStringLiteral("stop_keep_selection"),
            QStringLiteral("preference defaults drifted"));

    const QJsonObject queue_contract = requiredObject(fixture, "queue", QStringLiteral("fixture"));
    requireExactKeys(queue_contract, {"relative_path", "queue_id", "expected_item_count", "load_action_id"},
                     QStringLiteral("fixture queue contract"));
    require(requiredString(queue_contract, "relative_path", QStringLiteral("fixture queue contract")) ==
                    QStringLiteral("queue.v1.json") &&
                requiredString(queue_contract, "queue_id", QStringLiteral("fixture queue contract")) ==
                    QStringLiteral("M0-SHELL-Q1000") &&
                requiredInteger(queue_contract, "expected_item_count", QStringLiteral("fixture queue contract")) ==
                    1000 &&
                requiredString(queue_contract, "load_action_id", QStringLiteral("fixture queue contract")) ==
                    QStringLiteral("load-queue-1000"),
            QStringLiteral("fixture queue contract drifted"));
    validateFixedCases(requiredArray(fixture, "fixed_cases", QStringLiteral("fixture")));
}

void validateQueue(const QJsonObject& queue_object, FixtureBundle& bundle) {
    requireExactKeys(queue_object,
                     {"schema_id", "schema_version", "queue_id", "generation_recipe_id", "item_count", "items"},
                     QStringLiteral("queue"));
    bundle.queue_schema_id = requiredString(queue_object, "schema_id", QStringLiteral("queue"));
    bundle.queue_id = requiredString(queue_object, "queue_id", QStringLiteral("queue"));
    require(bundle.queue_schema_id == QStringLiteral("m0.shell-queue.v1") &&
                requiredInteger(queue_object, "schema_version", QStringLiteral("queue")) == 1 &&
                bundle.queue_id == QStringLiteral("M0-SHELL-Q1000") &&
                requiredInteger(queue_object, "item_count", QStringLiteral("queue")) == 1000 &&
                !requiredString(queue_object, "generation_recipe_id", QStringLiteral("queue")).isEmpty(),
            QStringLiteral("queue identity/count drifted"));
    const QJsonArray items = requiredArray(queue_object, "items", QStringLiteral("queue"));
    require(items.size() == 1000, QStringLiteral("queue must contain exactly 1000 items"));
    bundle.queue.reserve(items.size());
    QSet<QString> seen_ids;
    for (qsizetype index = 0; index < items.size(); ++index) {
        const QJsonObject object = items.at(index).toObject();
        requireExactKeys(
            object, {"order_index", "occurrence_id", "source", "metadata", "duration_ms", "availability", "error_code"},
            QStringLiteral("queue item"));
        const QJsonObject source = requiredObject(object, "source", QStringLiteral("queue item"));
        requireExactKeys(source, {"semantic_key", "presentation_form", "synthetic_path_segments", "file_name"},
                         QStringLiteral("queue source"));
        const QJsonArray segments = requiredArray(source, "synthetic_path_segments", QStringLiteral("queue source"));
        require(segments.size() >= 2, QStringLiteral("queue source segments are incomplete"));
        for (const QJsonValue& segment : segments) {
            require(segment.isString(), QStringLiteral("queue source segment must be a string"));
        }
        const QJsonObject metadata = requiredObject(object, "metadata", QStringLiteral("queue item"));
        requireExactKeys(metadata, {"title", "artist", "album"}, QStringLiteral("queue metadata"));
        const QString availability = requiredString(object, "availability", QStringLiteral("queue item"));
        require(availability == QStringLiteral("available") || availability == QStringLiteral("unavailable"),
                QStringLiteral("queue availability is invalid"));
        QueueItem item{
            .order_index = requiredInt(object, "order_index", QStringLiteral("queue item")),
            .occurrence_id = requiredString(object, "occurrence_id", QStringLiteral("queue item")),
            .semantic_key = requiredString(source, "semantic_key", QStringLiteral("queue source")),
            .presentation_form = requiredString(source, "presentation_form", QStringLiteral("queue source")),
            .file_name = requiredString(source, "file_name", QStringLiteral("queue source")),
            .title = requiredString(metadata, "title", QStringLiteral("queue metadata")),
            .artist = requiredString(metadata, "artist", QStringLiteral("queue metadata")),
            .album = requiredString(metadata, "album", QStringLiteral("queue metadata")),
            .duration_ms = nullableInteger(object, "duration_ms", QStringLiteral("queue item")),
            .available = availability == QStringLiteral("available"),
            .error_code = nullableString(object, "error_code", QStringLiteral("queue item")),
        };
        const QString expected_id = QStringLiteral("queue-%1").arg(index + 1, 4, 10, QLatin1Char('0'));
        require(item.order_index == index && item.occurrence_id == expected_id &&
                    !seen_ids.contains(item.occurrence_id),
                QStringLiteral("queue order/occurrence identity drifted"));
        seen_ids.insert(item.occurrence_id);
        require(item.presentation_form == QStringLiteral("nfc") || item.presentation_form == QStringLiteral("nfd"),
                QStringLiteral("queue presentation form is invalid"));
        if (item.available) {
            require(item.duration_ms.has_value() && item.duration_ms.value() > 0 && item.error_code.isEmpty(),
                    QStringLiteral("available queue item contract drifted"));
        } else {
            require(item.occurrence_id == QStringLiteral("queue-0999") && !item.duration_ms.has_value() &&
                        item.error_code == QStringLiteral("fixture_source_unavailable"),
                    QStringLiteral("unavailable queue item contract drifted"));
        }
        bundle.queue.append(std::move(item));
    }

    const QueueItem& duplicate_a = itemById(bundle.queue, QStringLiteral("queue-0007"));
    const QueueItem& duplicate_b = itemById(bundle.queue, QStringLiteral("queue-0707"));
    require(duplicate_a.occurrence_id != duplicate_b.occurrence_id &&
                duplicate_a.semantic_key == duplicate_b.semantic_key && duplicate_a.title == duplicate_b.title,
            QStringLiteral("duplicate hostile case drifted"));
    const QueueItem& nfc = itemById(bundle.queue, QStringLiteral("queue-0017"));
    const QueueItem& nfd = itemById(bundle.queue, QStringLiteral("queue-0018"));
    require(nfc.semantic_key == nfd.semantic_key && nfc.presentation_form == QStringLiteral("nfc") &&
                nfd.presentation_form == QStringLiteral("nfd") && nfc.title != nfd.title &&
                nfc.title.normalized(QString::NormalizationForm_C) ==
                    nfd.title.normalized(QString::NormalizationForm_C),
            QStringLiteral("normalization hostile case drifted"));
    require(itemById(bundle.queue, QStringLiteral("queue-0023")).title.contains(QString::fromUcs4(U"🎧")),
            QStringLiteral("non-BMP hostile case drifted"));
    require(itemById(bundle.queue, QStringLiteral("queue-0042")).title ==
                QStringLiteral("<b>Não interpretar</b> & \"aspas\" 'simples'"),
            QStringLiteral("literal-markup hostile case drifted"));
    require(itemById(bundle.queue, QStringLiteral("queue-0512")).title.toUcs4().size() == 512,
            QStringLiteral("long-text hostile case drifted"));
    const QueueItem& unavailable = itemById(bundle.queue, QStringLiteral("queue-0999"));
    require(!unavailable.available && !unavailable.duration_ms.has_value() &&
                unavailable.error_code == QStringLiteral("fixture_source_unavailable"),
            QStringLiteral("unavailable hostile case drifted"));
}

} // namespace

FixtureValidationError::FixtureValidationError(const QString& message) : std::runtime_error(message.toStdString()) {}

FixtureBundle FixtureRepository::load(const QString& fixture_path, const QString& queue_path) const {
    require(!fixture_path.isEmpty() && !queue_path.isEmpty(), QStringLiteral("fixture paths must not be empty"));
    FixtureBundle bundle;
    validateFixture(
        readDocument(fixture_path, QStringLiteral("fixture"), QByteArray(M0_FROZEN_FIXTURE_SHA256)).object(), bundle);
    validateQueue(readDocument(queue_path, QStringLiteral("queue"), QByteArray(M0_FROZEN_QUEUE_SHA256)).object(),
                  bundle);
    return bundle;
}

} // namespace music_player::m0
