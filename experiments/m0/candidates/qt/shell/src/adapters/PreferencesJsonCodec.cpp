#include "adapters/PreferencesJsonCodec.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>

namespace music_player::m0 {
namespace {

[[nodiscard]] QString encodeTheme(const ThemeMode theme) {
    switch (theme) {
    case ThemeMode::System:
        return QStringLiteral("system");
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    }
    return {};
}

[[nodiscard]] QString encodeMotion(const MotionMode motion) {
    switch (motion) {
    case MotionMode::System:
        return QStringLiteral("system");
    case MotionMode::Reduced:
        return QStringLiteral("reduced");
    }
    return {};
}

[[nodiscard]] QString encodeCloseBehavior(const CloseBehavior behavior) {
    switch (behavior) {
    case CloseBehavior::MinimizeToTray:
        return QStringLiteral("minimize_to_tray");
    case CloseBehavior::Exit:
        return QStringLiteral("exit");
    }
    return {};
}

[[nodiscard]] QString encodeQueueEndBehavior(const QueueEndBehavior behavior) {
    switch (behavior) {
    case QueueEndBehavior::StopKeepSelection:
        return QStringLiteral("stop_keep_selection");
    case QueueEndBehavior::StopClearSelection:
        return QStringLiteral("stop_clear_selection");
    }
    return {};
}

[[nodiscard]] QJsonValue encodeWindowPlacement(const std::optional<WindowPlacement>& placement) {
    if (!placement)
        return QJsonValue::Null;
    return QJsonObject{{QStringLiteral("x"), placement->x},
                       {QStringLiteral("y"), placement->y},
                       {QStringLiteral("width"), placement->width},
                       {QStringLiteral("height"), placement->height}};
}

[[nodiscard]] std::optional<ThemeMode> decodeTheme(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const QString encoded = value.toString();
    if (encoded == QStringLiteral("system"))
        return ThemeMode::System;
    if (encoded == QStringLiteral("light"))
        return ThemeMode::Light;
    if (encoded == QStringLiteral("dark"))
        return ThemeMode::Dark;
    return std::nullopt;
}

[[nodiscard]] std::optional<MotionMode> decodeMotion(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const QString encoded = value.toString();
    if (encoded == QStringLiteral("system"))
        return MotionMode::System;
    if (encoded == QStringLiteral("reduced"))
        return MotionMode::Reduced;
    return std::nullopt;
}

[[nodiscard]] std::optional<CloseBehavior> decodeCloseBehavior(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const QString encoded = value.toString();
    if (encoded == QStringLiteral("minimize_to_tray"))
        return CloseBehavior::MinimizeToTray;
    if (encoded == QStringLiteral("exit"))
        return CloseBehavior::Exit;
    return std::nullopt;
}

[[nodiscard]] std::optional<QueueEndBehavior> decodeQueueEndBehavior(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const QString encoded = value.toString();
    if (encoded == QStringLiteral("stop_keep_selection"))
        return QueueEndBehavior::StopKeepSelection;
    if (encoded == QStringLiteral("stop_clear_selection"))
        return QueueEndBehavior::StopClearSelection;
    return std::nullopt;
}

[[nodiscard]] std::optional<int> decodeBoundedInteger(const QJsonValue& value, const int minimum, const int maximum) {
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number || number < minimum || number > maximum)
        return std::nullopt;
    return static_cast<int>(number);
}

struct WindowPlacementDecodeResult final {
    bool valid{false};
    std::optional<WindowPlacement> placement{std::nullopt};
};

[[nodiscard]] WindowPlacementDecodeResult decodeWindowPlacement(const QJsonValue& value) {
    if (value.isNull())
        return {.valid = true};
    if (!value.isObject())
        return {};

    const QJsonObject object = value.toObject();
    constexpr int minimum_coordinate = std::numeric_limits<int>::min();
    constexpr int maximum_coordinate = std::numeric_limits<int>::max();
    const std::optional<int> x =
        decodeBoundedInteger(object.value(QStringLiteral("x")), minimum_coordinate, maximum_coordinate);
    const std::optional<int> y =
        decodeBoundedInteger(object.value(QStringLiteral("y")), minimum_coordinate, maximum_coordinate);
    const std::optional<int> width = decodeBoundedInteger(object.value(QStringLiteral("width")), 1, maximum_coordinate);
    const std::optional<int> height =
        decodeBoundedInteger(object.value(QStringLiteral("height")), 1, maximum_coordinate);
    if (!x || !y || !width || !height)
        return {};

    return {.valid = true, .placement = WindowPlacement{*x, *y, *width, *height}};
}

[[nodiscard]] bool validPreferences(const Preferences& preferences) {
    return !encodeTheme(preferences.theme).isEmpty() && !encodeMotion(preferences.motion).isEmpty() &&
           std::isfinite(preferences.volume) && preferences.volume >= 0.0 && preferences.volume <= 1.0 &&
           preferences.skip_backward_seconds >= 1 && preferences.skip_backward_seconds <= 300 &&
           preferences.skip_forward_seconds >= 1 && preferences.skip_forward_seconds <= 300 &&
           !encodeCloseBehavior(preferences.close_behavior).isEmpty() &&
           !encodeQueueEndBehavior(preferences.queue_end_behavior).isEmpty() &&
           (!preferences.window_placement ||
            (preferences.window_placement->width > 0 && preferences.window_placement->height > 0));
}

} // namespace

QByteArray PreferencesJsonCodec::encode(const Preferences& preferences) {
    if (!validPreferences(preferences))
        return {};

    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), schema_version);
    object.insert(QStringLiteral("theme"), encodeTheme(preferences.theme));
    object.insert(QStringLiteral("motion"), encodeMotion(preferences.motion));
    object.insert(QStringLiteral("volume"), preferences.volume);
    object.insert(QStringLiteral("muted"), preferences.muted);
    object.insert(QStringLiteral("skipBackwardSeconds"), preferences.skip_backward_seconds);
    object.insert(QStringLiteral("skipForwardSeconds"), preferences.skip_forward_seconds);
    object.insert(QStringLiteral("closeBehavior"), encodeCloseBehavior(preferences.close_behavior));
    object.insert(QStringLiteral("restoreLastSession"), preferences.restore_last_session);
    object.insert(QStringLiteral("startMinimized"), preferences.start_minimized);
    object.insert(QStringLiteral("queueEndBehavior"), encodeQueueEndBehavior(preferences.queue_end_behavior));
    object.insert(QStringLiteral("windowPlacement"), encodeWindowPlacement(preferences.window_placement));
    return QJsonDocument{object}.toJson(QJsonDocument::Compact);
}

PreferencesJsonDecodeResult PreferencesJsonCodec::decode(const QByteArrayView bytes) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes.toByteArray(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return {};

    const QJsonObject object = document.object();
    const QJsonValue version_value = object.value(QStringLiteral("schemaVersion"));
    if (!version_value.isDouble())
        return {};
    const double version = version_value.toDouble();
    if (!std::isfinite(version) || std::trunc(version) != version || version <= 0.0)
        return {};
    if (version > schema_version)
        return {.status = PreferencesJsonDecodeStatus::FutureVersion};
    if (version != 1.0 && version != schema_version)
        return {};

    const std::optional<ThemeMode> theme = decodeTheme(object.value(QStringLiteral("theme")));
    const std::optional<MotionMode> motion = decodeMotion(object.value(QStringLiteral("motion")));
    const QJsonValue volume_value = object.value(QStringLiteral("volume"));
    const double volume = volume_value.toDouble();
    const QJsonValue muted_value = object.value(QStringLiteral("muted"));
    const std::optional<int> skip_backward =
        decodeBoundedInteger(object.value(QStringLiteral("skipBackwardSeconds")), 1, 300);
    const std::optional<int> skip_forward =
        decodeBoundedInteger(object.value(QStringLiteral("skipForwardSeconds")), 1, 300);
    const std::optional<CloseBehavior> close_behavior =
        decodeCloseBehavior(object.value(QStringLiteral("closeBehavior")));
    const QJsonValue restore_last_session_value = object.value(QStringLiteral("restoreLastSession"));
    const QJsonValue start_minimized_value = object.value(QStringLiteral("startMinimized"));
    const std::optional<QueueEndBehavior> queue_end_behavior =
        decodeQueueEndBehavior(object.value(QStringLiteral("queueEndBehavior")));
    WindowPlacementDecodeResult window_placement{.valid = true};
    if (version == schema_version)
        window_placement = decodeWindowPlacement(object.value(QStringLiteral("windowPlacement")));

    if (!theme.has_value() || !motion.has_value() || !volume_value.isDouble() || !std::isfinite(volume) ||
        volume < 0.0 || volume > 1.0 || !muted_value.isBool() || !skip_backward.has_value() ||
        !skip_forward.has_value() || !close_behavior.has_value() || !restore_last_session_value.isBool() ||
        !start_minimized_value.isBool() || !queue_end_behavior.has_value() || !window_placement.valid) {
        return {};
    }

    return {.status = PreferencesJsonDecodeStatus::Valid,
            .preferences = Preferences{.theme = *theme,
                                       .motion = *motion,
                                       .volume = volume,
                                       .muted = muted_value.toBool(),
                                       .skip_backward_seconds = *skip_backward,
                                       .skip_forward_seconds = *skip_forward,
                                       .close_behavior = *close_behavior,
                                       .restore_last_session = restore_last_session_value.toBool(),
                                       .start_minimized = start_minimized_value.toBool(),
                                       .queue_end_behavior = *queue_end_behavior,
                                       .window_placement = window_placement.placement}};
}

} // namespace music_player::m0
