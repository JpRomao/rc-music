#include "adapters/fixture/FixtureDecoder.hpp"

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

[[nodiscard]] QByteArray readAuthenticatedBytes(const QString& path, const QString& context,
                                                const QByteArray& expected_sha256) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), context + QStringLiteral(" cannot be opened read-only"));
    const QByteArray bytes = file.readAll();
    require(file.error() == QFileDevice::NoError, context + QStringLiteral(" cannot be read completely"));
    require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == expected_sha256,
            context + QStringLiteral(" bytes differ from the protocol-anchored fixture manifest"));
    return bytes;
}

[[nodiscard]] QJsonObject parseDocument(const QByteArray& bytes, const QString& context) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    require(parse_error.error == QJsonParseError::NoError && document.isObject(),
            context + QStringLiteral(" is not a valid JSON object"));
    return document.object();
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

[[nodiscard]] QStringList requiredStringList(const QJsonObject& object, const char* key, const QString& context) {
    const QJsonArray values = requiredArray(object, key, context);
    QStringList result;
    result.reserve(values.size());
    for (const QJsonValue& value : values) {
        require(value.isString(),
                context + QStringLiteral(".") + QString::fromLatin1(key) + QStringLiteral(" must contain strings"));
        result.append(value.toString());
    }
    return result;
}

[[nodiscard]] std::optional<QString> optionalString(const QJsonObject& object, const char* key,
                                                    const QString& context) {
    return object.value(QString::fromLatin1(key)).isNull()
               ? std::nullopt
               : std::optional<QString>{requiredString(object, key, context)};
}

[[nodiscard]] std::optional<int> optionalInt(const QJsonObject& object, const char* key, const QString& context) {
    return object.value(QString::fromLatin1(key)).isNull() ? std::nullopt
                                                           : std::optional<int>{requiredInt(object, key, context)};
}

[[nodiscard]] std::optional<std::int64_t> nullableInteger(const QJsonObject& object, const char* key,
                                                          const QString& context) {
    return object.value(QString::fromLatin1(key)).isNull()
               ? std::nullopt
               : std::optional<std::int64_t>{requiredInteger(object, key, context)};
}

[[nodiscard]] bool isCanonicalColor(const QString& value) {
    if (value.size() != 7 || value.front() != QLatin1Char('#')) {
        return false;
    }
    for (qsizetype index = 1; index < value.size(); ++index) {
        const QChar character = value.at(index);
        const bool decimal = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool uppercase_hex = character >= QLatin1Char('A') && character <= QLatin1Char('F');
        if (!decimal && !uppercase_hex) {
            return false;
        }
    }
    return true;
}

void validateFixtureWindow(const QJsonObject& window, FixtureBundle& bundle) {
    requireExactKeys(window, {"client_width_px", "client_height_px", "minimum_width_px", "minimum_height_px"},
                     QStringLiteral("fixture window"));
    bundle.window = {
        requiredInt(window, "client_width_px", QStringLiteral("fixture window")),
        requiredInt(window, "client_height_px", QStringLiteral("fixture window")),
        requiredInt(window, "minimum_width_px", QStringLiteral("fixture window")),
        requiredInt(window, "minimum_height_px", QStringLiteral("fixture window")),
    };
    require(bundle.window.client_width_px == 960 && bundle.window.client_height_px == 640 &&
                bundle.window.minimum_width_px == 720 && bundle.window.minimum_height_px == 520 &&
                bundle.window.minimum_width_px <= bundle.window.client_width_px &&
                bundle.window.minimum_height_px <= bundle.window.client_height_px,
            QStringLiteral("fixture window contract drifted"));
}

void validateFixtureLayoutTokens(const QJsonObject& layout, FixtureBundle& bundle) {
    requireExactKeys(layout,
                     {"queue_panel_min_width_px", "queue_panel_max_width_px", "control_target_min_px",
                      "control_target_preferred_px", "radius_small_px", "radius_medium_px", "spacing_unit_px"},
                     QStringLiteral("fixture layout tokens"));
    bundle.layout_tokens = {
        requiredInt(layout, "queue_panel_min_width_px", QStringLiteral("fixture layout tokens")),
        requiredInt(layout, "queue_panel_max_width_px", QStringLiteral("fixture layout tokens")),
        requiredInt(layout, "control_target_min_px", QStringLiteral("fixture layout tokens")),
        requiredInt(layout, "control_target_preferred_px", QStringLiteral("fixture layout tokens")),
        requiredInt(layout, "radius_small_px", QStringLiteral("fixture layout tokens")),
        requiredInt(layout, "radius_medium_px", QStringLiteral("fixture layout tokens")),
        requiredInt(layout, "spacing_unit_px", QStringLiteral("fixture layout tokens")),
    };
    require(bundle.layout_tokens.queue_panel_min_width_px == 320 &&
                bundle.layout_tokens.queue_panel_max_width_px == 380 &&
                bundle.layout_tokens.control_target_min_px == 40 &&
                bundle.layout_tokens.control_target_preferred_px == 44 && bundle.layout_tokens.radius_small_px == 8 &&
                bundle.layout_tokens.radius_medium_px == 12 && bundle.layout_tokens.spacing_unit_px == 4 &&
                bundle.layout_tokens.queue_panel_min_width_px <= bundle.layout_tokens.queue_panel_max_width_px &&
                bundle.layout_tokens.control_target_min_px <= bundle.layout_tokens.control_target_preferred_px,
            QStringLiteral("fixture layout-token contract drifted"));
}

void requireFrozenString(const QString& actual, const QString& expected, const char* key) {
    const QString context = QStringLiteral("fixture strings.") + QString::fromLatin1(key);
    require(!actual.isEmpty() && actual.normalized(QString::NormalizationForm_C) == actual,
            context + QStringLiteral(" must be non-empty NFC text"));
    require(actual == expected, context + QStringLiteral(" drifted"));
}

void validateFixtureStrings(const QJsonObject& strings, FixtureBundle& bundle) {
    requireExactKeys(
        strings,
        {"app_name",           "action_open_audio",  "empty_title",       "empty_hint",          "action_play",
         "action_pause",       "action_previous",    "action_next",       "skip_backward_short", "skip_forward_short",
         "skip_backward_name", "skip_forward_name",  "progress_name",     "volume_name",         "action_mute",
         "action_unmute",      "queue_title",        "preferences_title", "details_title",       "section_general",
         "section_playback",   "section_appearance", "theme_system",      "theme_light",         "theme_dark",
         "motion_system",      "motion_reduced",     "error_title",       "error_body",          "action_skip",
         "action_retry",       "not_informed"},
        QStringLiteral("fixture strings"));
    bundle.strings = {
        requiredString(strings, "app_name", QStringLiteral("fixture strings")),
        requiredString(strings, "action_open_audio", QStringLiteral("fixture strings")),
        requiredString(strings, "empty_title", QStringLiteral("fixture strings")),
        requiredString(strings, "empty_hint", QStringLiteral("fixture strings")),
        requiredString(strings, "action_play", QStringLiteral("fixture strings")),
        requiredString(strings, "action_pause", QStringLiteral("fixture strings")),
        requiredString(strings, "action_previous", QStringLiteral("fixture strings")),
        requiredString(strings, "action_next", QStringLiteral("fixture strings")),
        requiredString(strings, "skip_backward_short", QStringLiteral("fixture strings")),
        requiredString(strings, "skip_forward_short", QStringLiteral("fixture strings")),
        requiredString(strings, "skip_backward_name", QStringLiteral("fixture strings")),
        requiredString(strings, "skip_forward_name", QStringLiteral("fixture strings")),
        requiredString(strings, "progress_name", QStringLiteral("fixture strings")),
        requiredString(strings, "volume_name", QStringLiteral("fixture strings")),
        requiredString(strings, "action_mute", QStringLiteral("fixture strings")),
        requiredString(strings, "action_unmute", QStringLiteral("fixture strings")),
        requiredString(strings, "queue_title", QStringLiteral("fixture strings")),
        requiredString(strings, "preferences_title", QStringLiteral("fixture strings")),
        requiredString(strings, "details_title", QStringLiteral("fixture strings")),
        requiredString(strings, "section_general", QStringLiteral("fixture strings")),
        requiredString(strings, "section_playback", QStringLiteral("fixture strings")),
        requiredString(strings, "section_appearance", QStringLiteral("fixture strings")),
        requiredString(strings, "theme_system", QStringLiteral("fixture strings")),
        requiredString(strings, "theme_light", QStringLiteral("fixture strings")),
        requiredString(strings, "theme_dark", QStringLiteral("fixture strings")),
        requiredString(strings, "motion_system", QStringLiteral("fixture strings")),
        requiredString(strings, "motion_reduced", QStringLiteral("fixture strings")),
        requiredString(strings, "error_title", QStringLiteral("fixture strings")),
        requiredString(strings, "error_body", QStringLiteral("fixture strings")),
        requiredString(strings, "action_skip", QStringLiteral("fixture strings")),
        requiredString(strings, "action_retry", QStringLiteral("fixture strings")),
        requiredString(strings, "not_informed", QStringLiteral("fixture strings")),
    };

    requireFrozenString(bundle.strings.app_name, QStringLiteral("Player de música"), "app_name");
    requireFrozenString(bundle.strings.action_open_audio, QStringLiteral("Abrir áudio"), "action_open_audio");
    requireFrozenString(bundle.strings.empty_title, QStringLiteral("Abra um áudio para começar"), "empty_title");
    requireFrozenString(bundle.strings.empty_hint, QStringLiteral("ou arraste arquivos para esta janela"),
                        "empty_hint");
    requireFrozenString(bundle.strings.action_play, QStringLiteral("Reproduzir"), "action_play");
    requireFrozenString(bundle.strings.action_pause, QStringLiteral("Pausar"), "action_pause");
    requireFrozenString(bundle.strings.action_previous, QStringLiteral("Faixa anterior"), "action_previous");
    requireFrozenString(bundle.strings.action_next, QStringLiteral("Próxima faixa"), "action_next");
    requireFrozenString(bundle.strings.skip_backward_short, QStringLiteral("−{seconds}"), "skip_backward_short");
    requireFrozenString(bundle.strings.skip_forward_short, QStringLiteral("+{seconds}"), "skip_forward_short");
    requireFrozenString(bundle.strings.skip_backward_name, QStringLiteral("Voltar {seconds} segundos"),
                        "skip_backward_name");
    requireFrozenString(bundle.strings.skip_forward_name, QStringLiteral("Avançar {seconds} segundos"),
                        "skip_forward_name");
    requireFrozenString(bundle.strings.progress_name, QStringLiteral("Posição da faixa"), "progress_name");
    requireFrozenString(bundle.strings.volume_name, QStringLiteral("Volume"), "volume_name");
    requireFrozenString(bundle.strings.action_mute, QStringLiteral("Silenciar"), "action_mute");
    requireFrozenString(bundle.strings.action_unmute, QStringLiteral("Ativar som"), "action_unmute");
    requireFrozenString(bundle.strings.queue_title, QStringLiteral("Fila"), "queue_title");
    requireFrozenString(bundle.strings.preferences_title, QStringLiteral("Preferências"), "preferences_title");
    requireFrozenString(bundle.strings.details_title, QStringLiteral("Detalhes da faixa"), "details_title");
    requireFrozenString(bundle.strings.section_general, QStringLiteral("Geral"), "section_general");
    requireFrozenString(bundle.strings.section_playback, QStringLiteral("Reprodução"), "section_playback");
    requireFrozenString(bundle.strings.section_appearance, QStringLiteral("Aparência"), "section_appearance");
    requireFrozenString(bundle.strings.theme_system, QStringLiteral("Sistema"), "theme_system");
    requireFrozenString(bundle.strings.theme_light, QStringLiteral("Claro"), "theme_light");
    requireFrozenString(bundle.strings.theme_dark, QStringLiteral("Escuro"), "theme_dark");
    requireFrozenString(bundle.strings.motion_system, QStringLiteral("Sistema"), "motion_system");
    requireFrozenString(bundle.strings.motion_reduced, QStringLiteral("Reduzido"), "motion_reduced");
    requireFrozenString(bundle.strings.error_title, QStringLiteral("Não foi possível abrir esta faixa"), "error_title");
    requireFrozenString(bundle.strings.error_body, QStringLiteral("A faixa de teste está indisponível."), "error_body");
    requireFrozenString(bundle.strings.action_skip, QStringLiteral("Pular"), "action_skip");
    requireFrozenString(bundle.strings.action_retry, QStringLiteral("Tentar novamente"), "action_retry");
    requireFrozenString(bundle.strings.not_informed, QStringLiteral("Não informado"), "not_informed");

    const QString placeholder = QStringLiteral("{seconds}");
    require(bundle.strings.skip_backward_short.count(placeholder) == 1 &&
                bundle.strings.skip_forward_short.count(placeholder) == 1 &&
                bundle.strings.skip_backward_name.count(placeholder) == 1 &&
                bundle.strings.skip_forward_name.count(placeholder) == 1,
            QStringLiteral("fixture skip-string placeholders drifted"));
}

[[nodiscard]] FixturePalette parseFixturePalette(const QJsonObject& palette, const QString& context) {
    requireExactKeys(palette,
                     {"canvas", "surface", "surface-raised", "text-primary", "text-secondary", "accent", "on-accent",
                      "border", "focus", "error", "warning", "success"},
                     context);
    FixturePalette parsed{
        requiredString(palette, "canvas", context),         requiredString(palette, "surface", context),
        requiredString(palette, "surface-raised", context), requiredString(palette, "text-primary", context),
        requiredString(palette, "text-secondary", context), requiredString(palette, "accent", context),
        requiredString(palette, "on-accent", context),      requiredString(palette, "border", context),
        requiredString(palette, "focus", context),          requiredString(palette, "error", context),
        requiredString(palette, "warning", context),        requiredString(palette, "success", context),
    };
    const std::initializer_list<const QString*> colors{
        &parsed.canvas,         &parsed.surface, &parsed.surface_raised, &parsed.text_primary,
        &parsed.text_secondary, &parsed.accent,  &parsed.on_accent,      &parsed.border,
        &parsed.focus,          &parsed.error,   &parsed.warning,        &parsed.success,
    };
    for (const QString* color : colors) {
        require(isCanonicalColor(*color), context + QStringLiteral(" contains a non-canonical color"));
    }
    return parsed;
}

void validateFixtureThemes(const QJsonObject& themes, FixtureBundle& bundle) {
    requireExactKeys(themes, {"available_modes", "palettes"}, QStringLiteral("fixture themes"));
    const QJsonObject palettes = requiredObject(themes, "palettes", QStringLiteral("fixture themes"));
    requireExactKeys(palettes, {"light", "dark"}, QStringLiteral("fixture palettes"));
    bundle.themes = {
        requiredStringList(themes, "available_modes", QStringLiteral("fixture themes")),
        parseFixturePalette(requiredObject(palettes, "light", QStringLiteral("fixture palettes")),
                            QStringLiteral("fixture light palette")),
        parseFixturePalette(requiredObject(palettes, "dark", QStringLiteral("fixture palettes")),
                            QStringLiteral("fixture dark palette")),
    };
    require(bundle.themes.available_modes ==
                QStringList{QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")},
            QStringLiteral("fixture theme modes drifted"));
    const FixturePalette& light = bundle.themes.light;
    const FixturePalette& dark = bundle.themes.dark;
    require(light.canvas == QStringLiteral("#F6F7F9") && light.surface == QStringLiteral("#FFFFFF") &&
                light.surface_raised == QStringLiteral("#FFFFFF") && light.text_primary == QStringLiteral("#15171A") &&
                light.text_secondary == QStringLiteral("#4F5660") && light.accent == QStringLiteral("#4F46E5") &&
                light.on_accent == QStringLiteral("#FFFFFF") && light.border == QStringLiteral("#8A929E") &&
                light.focus == QStringLiteral("#2457D6") && light.error == QStringLiteral("#B42318") &&
                light.warning == QStringLiteral("#8A4B08") && light.success == QStringLiteral("#167344"),
            QStringLiteral("fixture light palette drifted"));
    require(dark.canvas == QStringLiteral("#111318") && dark.surface == QStringLiteral("#181B21") &&
                dark.surface_raised == QStringLiteral("#21252D") && dark.text_primary == QStringLiteral("#F5F7FA") &&
                dark.text_secondary == QStringLiteral("#BBC2CC") && dark.accent == QStringLiteral("#8B83FF") &&
                dark.on_accent == QStringLiteral("#0B0C10") && dark.border == QStringLiteral("#69717D") &&
                dark.focus == QStringLiteral("#A9C2FF") && dark.error == QStringLiteral("#FF8A80") &&
                dark.warning == QStringLiteral("#FFC46B") && dark.success == QStringLiteral("#6DDA9B"),
            QStringLiteral("fixture dark palette drifted"));
}

[[nodiscard]] FixtureMotionValues parseMotionValues(const QJsonObject& values, const QString& context) {
    requireExactKeys(values, {"panel_transition_ms", "feedback_transition_ms", "continuous_animation"}, context);
    return {
        requiredInt(values, "panel_transition_ms", context),
        requiredInt(values, "feedback_transition_ms", context),
        requiredBool(values, "continuous_animation", context),
    };
}

void validateFixtureMotion(const QJsonObject& motion, FixtureBundle& bundle) {
    requireExactKeys(motion, {"available_modes", "normal", "reduced"}, QStringLiteral("fixture motion"));
    bundle.motion = {
        requiredStringList(motion, "available_modes", QStringLiteral("fixture motion")),
        parseMotionValues(requiredObject(motion, "normal", QStringLiteral("fixture motion")),
                          QStringLiteral("fixture normal motion")),
        parseMotionValues(requiredObject(motion, "reduced", QStringLiteral("fixture motion")),
                          QStringLiteral("fixture reduced motion")),
    };
    require(bundle.motion.available_modes == QStringList{QStringLiteral("system"), QStringLiteral("reduced")} &&
                bundle.motion.normal.panel_transition_ms == 160 && bundle.motion.normal.feedback_transition_ms == 120 &&
                !bundle.motion.normal.continuous_animation && bundle.motion.reduced.panel_transition_ms == 0 &&
                bundle.motion.reduced.feedback_transition_ms == 0 && !bundle.motion.reduced.continuous_animation,
            QStringLiteral("fixture motion contract drifted"));
}

void validateFixtureFakeBackend(const QJsonObject& fake_backend, FixtureBundle& bundle) {
    requireExactKeys(fake_backend, {"clock", "snapshots", "seek"}, QStringLiteral("fixture fake backend"));
    const QJsonObject clock = requiredObject(fake_backend, "clock", QStringLiteral("fixture fake backend"));
    requireExactKeys(clock, {"kind", "initial_monotonic_ms"}, QStringLiteral("fixture fake clock"));
    const QJsonObject snapshots = requiredObject(fake_backend, "snapshots", QStringLiteral("fixture fake backend"));
    requireExactKeys(
        snapshots, {"frequency_hz", "interval_ms", "start_position_ms", "duration_ms", "increment_ms", "end_behavior"},
        QStringLiteral("fixture fake snapshots"));
    const QJsonObject seek = requiredObject(fake_backend, "seek", QStringLiteral("fixture fake backend"));
    requireExactKeys(seek, {"target_ms", "preview_immediate", "ack_after_ticks", "ack_position_ms"},
                     QStringLiteral("fixture fake seek"));

    bundle.fake_backend = {
        {requiredString(clock, "kind", QStringLiteral("fixture fake clock")),
         requiredInteger(clock, "initial_monotonic_ms", QStringLiteral("fixture fake clock"))},
        {requiredInt(snapshots, "frequency_hz", QStringLiteral("fixture fake snapshots")),
         requiredInteger(snapshots, "interval_ms", QStringLiteral("fixture fake snapshots")),
         requiredInteger(snapshots, "start_position_ms", QStringLiteral("fixture fake snapshots")),
         requiredInteger(snapshots, "duration_ms", QStringLiteral("fixture fake snapshots")),
         requiredInteger(snapshots, "increment_ms", QStringLiteral("fixture fake snapshots")),
         requiredString(snapshots, "end_behavior", QStringLiteral("fixture fake snapshots"))},
        {requiredInteger(seek, "target_ms", QStringLiteral("fixture fake seek")),
         requiredBool(seek, "preview_immediate", QStringLiteral("fixture fake seek")),
         requiredInt(seek, "ack_after_ticks", QStringLiteral("fixture fake seek")),
         requiredInteger(seek, "ack_position_ms", QStringLiteral("fixture fake seek"))},
    };
    const FixtureFakeSnapshots& parsed_snapshots = bundle.fake_backend.snapshots;
    const FixtureFakeSeek& parsed_seek = bundle.fake_backend.seek;
    require(bundle.fake_backend.clock.kind == QStringLiteral("deterministic") &&
                bundle.fake_backend.clock.initial_monotonic_ms == 0 && parsed_snapshots.frequency_hz == 5 &&
                parsed_snapshots.interval_ms == 200 && parsed_snapshots.start_position_ms == 84'000 &&
                parsed_snapshots.duration_ms == 248'000 && parsed_snapshots.increment_ms == 200 &&
                parsed_snapshots.end_behavior == QStringLiteral("clamp_paused") && parsed_seek.target_ms == 124'000 &&
                parsed_seek.preview_immediate && parsed_seek.ack_after_ticks == 1 &&
                parsed_seek.ack_position_ms == 124'000 &&
                static_cast<std::int64_t>(parsed_snapshots.frequency_hz) * parsed_snapshots.interval_ms == 1'000 &&
                parsed_snapshots.increment_ms == parsed_snapshots.interval_ms &&
                parsed_seek.target_ms == parsed_seek.ack_position_ms &&
                parsed_seek.target_ms <= parsed_snapshots.duration_ms,
            QStringLiteral("fixture fake-backend contract drifted"));
}

void validateFixtureTrackDetails(const QJsonObject& track_details, FixtureBundle& bundle) {
    requireExactKeys(track_details,
                     {"occurrence_id", "synthetic", "size_bytes", "modified_utc", "container", "codec", "bitrate_bps",
                      "sample_rate_hz", "channels", "bit_depth"},
                     QStringLiteral("fixture track details"));
    bundle.track_details = {
        requiredString(track_details, "occurrence_id", QStringLiteral("fixture track details")),
        requiredBool(track_details, "synthetic", QStringLiteral("fixture track details")),
        requiredInteger(track_details, "size_bytes", QStringLiteral("fixture track details")),
        requiredString(track_details, "modified_utc", QStringLiteral("fixture track details")),
        requiredString(track_details, "container", QStringLiteral("fixture track details")),
        requiredString(track_details, "codec", QStringLiteral("fixture track details")),
        requiredInt(track_details, "bitrate_bps", QStringLiteral("fixture track details")),
        requiredInt(track_details, "sample_rate_hz", QStringLiteral("fixture track details")),
        requiredInt(track_details, "channels", QStringLiteral("fixture track details")),
        optionalInt(track_details, "bit_depth", QStringLiteral("fixture track details")),
    };
}

void validateFrozenTrackDetails(const FixtureBundle& bundle) {
    require(bundle.track_details.occurrence_id == QStringLiteral("queue-0017") && bundle.track_details.synthetic &&
                bundle.track_details.size_bytes == 7'483'921 &&
                bundle.track_details.modified_utc == QStringLiteral("2026-08-23T00:00:00Z") &&
                bundle.track_details.container == QStringLiteral("M4A") &&
                bundle.track_details.codec == QStringLiteral("AAC-LC") && bundle.track_details.bitrate_bps == 256'000 &&
                bundle.track_details.sample_rate_hz == 48'000 && bundle.track_details.channels == 2 &&
                !bundle.track_details.bit_depth.has_value(),
            QStringLiteral("fixture track-details contract drifted"));
}

void validateFixtureAssets(const QJsonArray& assets, FixtureBundle& bundle) {
    require(assets.size() == 2, QStringLiteral("fixture must contain exactly two ordered assets"));
    const QList<FixtureAsset> expected{
        {QStringLiteral("cover-placeholder"), QStringLiteral("assets/cover-placeholder.svg"),
         QStringLiteral("empty_title")},
        {QStringLiteral("cover-track"), QStringLiteral("assets/cover-track.svg"), QStringLiteral("details_title")},
    };
    bundle.assets.reserve(assets.size());
    for (qsizetype index = 0; index < assets.size(); ++index) {
        require(assets.at(index).isObject(), QStringLiteral("fixture asset must be an object"));
        const QJsonObject asset = assets.at(index).toObject();
        requireExactKeys(asset, {"asset_id", "relative_path", "alt_key"}, QStringLiteral("fixture asset"));
        FixtureAsset parsed{
            requiredString(asset, "asset_id", QStringLiteral("fixture asset")),
            requiredString(asset, "relative_path", QStringLiteral("fixture asset")),
            requiredString(asset, "alt_key", QStringLiteral("fixture asset")),
        };
        const FixtureAsset& wanted = expected.at(index);
        require(parsed.asset_id == wanted.asset_id && parsed.relative_path == wanted.relative_path &&
                    parsed.alt_key == wanted.alt_key,
                QStringLiteral("fixture asset order/content drifted"));
        bundle.assets.append(std::move(parsed));
    }
}

[[nodiscard]] const QueueItem& itemById(const QList<QueueItem>& queue, const QString& occurrence_id) {
    for (const QueueItem& item : queue) {
        if (item.occurrence_id == occurrence_id) {
            return item;
        }
    }
    fail(QStringLiteral("required hostile item is missing: ") + occurrence_id);
}

[[nodiscard]] bool containsQueueItem(const QList<QueueItem>& queue, const QString& occurrence_id) {
    for (const QueueItem& item : queue) {
        if (item.occurrence_id == occurrence_id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool containsAsset(const QList<FixtureAsset>& assets, const QString& asset_id) {
    for (const FixtureAsset& asset : assets) {
        if (asset.asset_id == asset_id) {
            return true;
        }
    }
    return false;
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
            .current_occurrence_id = optionalString(state, "current_occurrence_id", QStringLiteral("fixture state")),
            .context_occurrence_id = optionalString(state, "context_occurrence_id", QStringLiteral("fixture state")),
            .position_ms = nullableInteger(state, "position_ms", QStringLiteral("fixture state")),
            .duration_ms = nullableInteger(state, "duration_ms", QStringLiteral("fixture state")),
            .seekable = requiredBool(state, "seekable", QStringLiteral("fixture state")),
            .asset_id = requiredString(state, "asset_id", QStringLiteral("fixture state")),
            .message_key = optionalString(state, "message_key", QStringLiteral("fixture state")),
            .primary_action_id = requiredString(state, "primary_action_id", QStringLiteral("fixture state")),
            .error_code = optionalString(state, "error_code", QStringLiteral("fixture state")),
        };
        require(parsed.state_id == expected_ids.at(index) && parsed.playback_state == expected_playback.at(index),
                QStringLiteral("fixture state identity/order drifted"));
        bundle.states.append(std::move(parsed));
    }

    require(!bundle.states.at(0).current_occurrence_id.has_value(),
            QStringLiteral("fixture empty state current_occurrence_id must be null"));
}

void validateFrozenStateSemantics(const FixtureBundle& bundle) {
    const FixtureState& empty = bundle.states.at(0);
    const FixtureState& playing = bundle.states.at(1);
    const FixtureState& error = bundle.states.at(2);
    require(
        !empty.queue_loaded && !empty.current_occurrence_id.has_value() && !empty.context_occurrence_id.has_value() &&
            !empty.position_ms.has_value() && !empty.duration_ms.has_value() && !empty.seekable &&
            empty.asset_id == QStringLiteral("cover-placeholder") && empty.message_key.has_value() &&
            *empty.message_key == QStringLiteral("empty_title") &&
            empty.primary_action_id == QStringLiteral("open-audio") && !empty.error_code.has_value() &&
            playing.queue_loaded && playing.current_occurrence_id.has_value() &&
            *playing.current_occurrence_id == QStringLiteral("queue-0017") &&
            !playing.context_occurrence_id.has_value() && playing.position_ms == 84'000 &&
            playing.duration_ms == 248'000 && playing.seekable && playing.asset_id == QStringLiteral("cover-track") &&
            !playing.message_key.has_value() && playing.primary_action_id == QStringLiteral("play-pause") &&
            !playing.error_code.has_value() && error.queue_loaded && error.current_occurrence_id.has_value() &&
            *error.current_occurrence_id == QStringLiteral("queue-0999") && error.context_occurrence_id.has_value() &&
            *error.context_occurrence_id == QStringLiteral("queue-0017") && !error.position_ms.has_value() &&
            !error.duration_ms.has_value() && !error.seekable && error.asset_id == QStringLiteral("cover-track") &&
            error.message_key.has_value() && *error.message_key == QStringLiteral("error_body") &&
            error.primary_action_id == QStringLiteral("skip") && error.error_code.has_value() &&
            *error.error_code == QStringLiteral("fixture_source_unavailable"),
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

[[nodiscard]] FixturePreferenceRange parsePreferenceRange(const QJsonObject& range, const QString& context) {
    requireExactKeys(range, {"minimum", "maximum"}, context);
    return {
        requiredInt(range, "minimum", context),
        requiredInt(range, "maximum", context),
    };
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
    validateFixtureWindow(requiredObject(fixture, "window", QStringLiteral("fixture")), bundle);
    validateFixtureLayoutTokens(requiredObject(fixture, "layout_tokens", QStringLiteral("fixture")), bundle);
    validateFixtureStrings(requiredObject(fixture, "strings", QStringLiteral("fixture")), bundle);
    validateFixtureStates(requiredArray(fixture, "states", QStringLiteral("fixture")), bundle);
    validateFixtureControls(requiredArray(fixture, "controls", QStringLiteral("fixture")), bundle);
    validateFixtureThemes(requiredObject(fixture, "themes", QStringLiteral("fixture")), bundle);
    validateFixtureMotion(requiredObject(fixture, "motion", QStringLiteral("fixture")), bundle);

    const QJsonObject preferences = requiredObject(fixture, "preferences", QStringLiteral("fixture"));
    requireExactKeys(preferences, {"defaults", "constraints", "rules"}, QStringLiteral("preferences"));
    const QJsonObject defaults = requiredObject(preferences, "defaults", QStringLiteral("preferences"));
    requireExactKeys(defaults,
                     {"theme", "motion", "volume_percent", "muted", "skip_backward_seconds", "skip_forward_seconds",
                      "close_behavior", "restore_last_session", "start_minimized", "queue_end_behavior"},
                     QStringLiteral("preference defaults"));
    bundle.preferences.defaults = {
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

    const QJsonObject constraints = requiredObject(preferences, "constraints", QStringLiteral("preferences"));
    requireExactKeys(constraints, {"volume_percent", "skip_seconds", "close_behavior", "queue_end_behavior"},
                     QStringLiteral("preference constraints"));
    bundle.preferences.constraints = {
        parsePreferenceRange(requiredObject(constraints, "volume_percent", QStringLiteral("preference constraints")),
                             QStringLiteral("preference volume-percent constraint")),
        parsePreferenceRange(requiredObject(constraints, "skip_seconds", QStringLiteral("preference constraints")),
                             QStringLiteral("preference skip-seconds constraint")),
        requiredStringList(constraints, "close_behavior", QStringLiteral("preference constraints")),
        requiredStringList(constraints, "queue_end_behavior", QStringLiteral("preference constraints")),
    };

    const QJsonObject rules = requiredObject(preferences, "rules", QStringLiteral("preferences"));
    requireExactKeys(rules,
                     {"close_without_tray", "start_minimized_requires_tray", "restored_playback_state",
                      "unmute_restores_last_nonzero_volume"},
                     QStringLiteral("preference rules"));
    bundle.preferences.rules = {
        requiredString(rules, "close_without_tray", QStringLiteral("preference rules")),
        requiredBool(rules, "start_minimized_requires_tray", QStringLiteral("preference rules")),
        requiredString(rules, "restored_playback_state", QStringLiteral("preference rules")),
        requiredBool(rules, "unmute_restores_last_nonzero_volume", QStringLiteral("preference rules")),
    };

    const FixtureDefaults& parsed_defaults = bundle.preferences.defaults;
    const FixturePreferenceConstraints& parsed_constraints = bundle.preferences.constraints;
    const FixturePreferenceRules& parsed_rules = bundle.preferences.rules;
    require(parsed_constraints.volume_percent.minimum <= parsed_constraints.volume_percent.maximum &&
                parsed_constraints.skip_seconds.minimum <= parsed_constraints.skip_seconds.maximum,
            QStringLiteral("preference numeric constraint ranges are invalid"));
    require(parsed_defaults.volume_percent >= parsed_constraints.volume_percent.minimum &&
                parsed_defaults.volume_percent <= parsed_constraints.volume_percent.maximum &&
                parsed_defaults.skip_backward_seconds >= parsed_constraints.skip_seconds.minimum &&
                parsed_defaults.skip_backward_seconds <= parsed_constraints.skip_seconds.maximum &&
                parsed_defaults.skip_forward_seconds >= parsed_constraints.skip_seconds.minimum &&
                parsed_defaults.skip_forward_seconds <= parsed_constraints.skip_seconds.maximum,
            QStringLiteral("preference defaults fall outside numeric constraints"));
    require(parsed_constraints.close_behavior.contains(parsed_defaults.close_behavior) &&
                parsed_constraints.queue_end_behavior.contains(parsed_defaults.queue_end_behavior),
            QStringLiteral("preference defaults are not allowed by option constraints"));
    require(bundle.themes.available_modes.contains(parsed_defaults.theme) &&
                bundle.motion.available_modes.contains(parsed_defaults.motion),
            QStringLiteral("preference defaults do not resolve to fixture modes"));
    require(parsed_defaults.theme == QStringLiteral("system") && parsed_defaults.motion == QStringLiteral("system") &&
                parsed_defaults.volume_percent == 80 && !parsed_defaults.muted &&
                parsed_defaults.skip_backward_seconds == 5 && parsed_defaults.skip_forward_seconds == 5 &&
                parsed_defaults.close_behavior == QStringLiteral("minimize_to_tray") &&
                parsed_defaults.restore_last_session && !parsed_defaults.start_minimized &&
                parsed_defaults.queue_end_behavior == QStringLiteral("stop_keep_selection"),
            QStringLiteral("preference defaults drifted"));
    require(parsed_constraints.volume_percent.minimum == 0 && parsed_constraints.volume_percent.maximum == 100 &&
                parsed_constraints.skip_seconds.minimum == 1 && parsed_constraints.skip_seconds.maximum == 300,
            QStringLiteral("preference constraint ranges drifted"));
    require(parsed_constraints.close_behavior ==
                    QStringList{QStringLiteral("minimize_to_tray"), QStringLiteral("exit")} &&
                parsed_constraints.queue_end_behavior ==
                    QStringList{QStringLiteral("stop_keep_selection"), QStringLiteral("stop_clear_selection")},
            QStringLiteral("preference constraint options drifted"));
    require(parsed_rules.close_without_tray == QStringLiteral("minimize_window") &&
                parsed_rules.start_minimized_requires_tray &&
                parsed_rules.restored_playback_state == QStringLiteral("paused") &&
                parsed_rules.unmute_restores_last_nonzero_volume,
            QStringLiteral("preference rules drifted"));

    validateFixtureFakeBackend(requiredObject(fixture, "fake_backend", QStringLiteral("fixture")), bundle);
    const FixtureState& playing = bundle.states.at(1);
    require(playing.position_ms == bundle.fake_backend.snapshots.start_position_ms &&
                playing.duration_ms == bundle.fake_backend.snapshots.duration_ms,
            QStringLiteral("playing state and fake snapshot timing differ"));
    validateFixtureTrackDetails(requiredObject(fixture, "track_details", QStringLiteral("fixture")), bundle);
    validateFixtureAssets(requiredArray(fixture, "assets", QStringLiteral("fixture")), bundle);

    const QJsonObject queue_contract = requiredObject(fixture, "queue", QStringLiteral("fixture"));
    requireExactKeys(queue_contract, {"relative_path", "queue_id", "expected_item_count", "load_action_id"},
                     QStringLiteral("fixture queue contract"));
    bundle.queue_contract = {
        requiredString(queue_contract, "relative_path", QStringLiteral("fixture queue contract")),
        requiredString(queue_contract, "queue_id", QStringLiteral("fixture queue contract")),
        requiredInt(queue_contract, "expected_item_count", QStringLiteral("fixture queue contract")),
        requiredString(queue_contract, "load_action_id", QStringLiteral("fixture queue contract")),
    };
    require(bundle.queue_contract.relative_path == QStringLiteral("queue.v1.json") &&
                bundle.queue_contract.queue_id == QStringLiteral("M0-SHELL-Q1000") &&
                bundle.queue_contract.expected_item_count == 1000 &&
                bundle.queue_contract.load_action_id == QStringLiteral("load-queue-1000"),
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
            .error_code = optionalString(object, "error_code", QStringLiteral("queue item")),
        };
        const QString expected_id = QStringLiteral("queue-%1").arg(index + 1, 4, 10, QLatin1Char('0'));
        require(item.order_index == index && item.occurrence_id == expected_id &&
                    !seen_ids.contains(item.occurrence_id),
                QStringLiteral("queue order/occurrence identity drifted"));
        seen_ids.insert(item.occurrence_id);
        require(item.presentation_form == QStringLiteral("nfc") || item.presentation_form == QStringLiteral("nfd"),
                QStringLiteral("queue presentation form is invalid"));
        if (item.available) {
            require(item.duration_ms.has_value() && item.duration_ms.value() > 0 && !item.error_code.has_value(),
                    QStringLiteral("available queue item contract drifted"));
        } else {
            require(item.occurrence_id == QStringLiteral("queue-0999") && !item.duration_ms.has_value() &&
                        item.error_code.has_value() && *item.error_code == QStringLiteral("fixture_source_unavailable"),
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
    require(!unavailable.available && !unavailable.duration_ms.has_value() && unavailable.error_code.has_value() &&
                *unavailable.error_code == QStringLiteral("fixture_source_unavailable"),
            QStringLiteral("unavailable hostile case drifted"));
}

} // namespace

namespace detail {

FixtureBundle decodeAuthenticatedDocuments(const QJsonObject& fixture, const QJsonObject& queue) {
    FixtureBundle bundle;
    validateFixture(fixture, bundle);
    validateQueue(queue, bundle);
    require(bundle.queue_contract.queue_id == bundle.queue_id &&
                bundle.queue_contract.expected_item_count == bundle.queue.size(),
            QStringLiteral("fixture queue contract does not match the loaded queue"));
    for (const FixtureState& state : bundle.states) {
        require(containsAsset(bundle.assets, state.asset_id), QStringLiteral("fixture state asset does not resolve"));
        if (state.current_occurrence_id.has_value()) {
            require(containsQueueItem(bundle.queue, *state.current_occurrence_id),
                    QStringLiteral("fixture state current occurrence does not resolve in queue"));
        }
        if (state.context_occurrence_id.has_value()) {
            require(containsQueueItem(bundle.queue, *state.context_occurrence_id),
                    QStringLiteral("fixture state context occurrence does not resolve in queue"));
        }
    }
    require(containsQueueItem(bundle.queue, bundle.track_details.occurrence_id),
            QStringLiteral("track details occurrence does not resolve in queue"));
    require(bundle.states.at(1).current_occurrence_id.has_value() &&
                bundle.track_details.occurrence_id == *bundle.states.at(1).current_occurrence_id,
            QStringLiteral("track details do not describe the playing occurrence"));
    validateFrozenStateSemantics(bundle);
    validateFrozenTrackDetails(bundle);
    return bundle;
}

} // namespace detail

FixtureValidationError::FixtureValidationError(const QString& message) : std::runtime_error(message.toStdString()) {}

FixtureBundle FixtureRepository::load(const QString& fixture_path, const QString& queue_path) const {
    require(!fixture_path.isEmpty() && !queue_path.isEmpty(), QStringLiteral("fixture paths must not be empty"));
    const QByteArray fixture_bytes =
        readAuthenticatedBytes(fixture_path, QStringLiteral("fixture"), QByteArray(M0_FROZEN_FIXTURE_SHA256));
    const QByteArray queue_bytes =
        readAuthenticatedBytes(queue_path, QStringLiteral("queue"), QByteArray(M0_FROZEN_QUEUE_SHA256));
    const QJsonObject fixture = parseDocument(fixture_bytes, QStringLiteral("fixture"));
    const QJsonObject queue = parseDocument(queue_bytes, QStringLiteral("queue"));
    return detail::decodeAuthenticatedDocuments(fixture, queue);
}

} // namespace music_player::m0
