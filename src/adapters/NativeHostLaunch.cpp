#include "NativeHostLaunch.hpp"

#include <algorithm>
#include <limits>

namespace music_player::m0 {
namespace {

std::optional<quint64> canonicalWindow(const QString& argument) {
    const auto prefix = QStringLiteral("--parent-window=");
    if (!argument.startsWith(prefix) || argument.size() > prefix.size() + 20)
        return std::nullopt;
    const auto decimal = argument.mid(prefix.size());
    if (decimal.isEmpty() || decimal.size() > 20 || (decimal.size() > 1 && decimal.front() == QLatin1Char('0')))
        return std::nullopt;
    quint64 value = 0;
    for (const auto character : decimal) {
        const auto unit = character.unicode();
        if (unit < '0' || unit > '9')
            return std::nullopt;
        const auto digit = static_cast<quint64>(unit - '0');
        if (value > (std::numeric_limits<quint64>::max() - digit) / 10)
            return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

} // namespace

bool isValidNativeHostConfiguration(const NativeHostConfiguration& configuration) {
    const auto prefix = QStringLiteral("chrome-extension://");
    const auto& origin = configuration.allowed_origin;
    const auto& endpoint = configuration.endpoint_name;
    if (!origin.startsWith(prefix) || origin.size() != prefix.size() + 32 + 1 || !origin.endsWith(QLatin1Char('/')) ||
        endpoint.isEmpty() || endpoint.size() > 96)
        return false;
    if (!std::all_of(origin.cbegin() + prefix.size(), origin.cend() - 1, [](const QChar character) {
            return character >= QLatin1Char('a') && character <= QLatin1Char('p');
        }))
        return false;
    return std::all_of(endpoint.cbegin(), endpoint.cend(), [](const QChar character) {
        const auto unit = character.unicode();
        return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
               unit == '_' || unit == '-';
    });
}

NativeHostLaunchResult parseNativeHostLaunch(const QStringList& arguments,
                                             const NativeHostConfiguration& configuration) {
    if (configuration.allowed_origin.isEmpty() && configuration.endpoint_name.isEmpty())
        return {.error = NativeHostLaunchError::Disabled};
    if (!isValidNativeHostConfiguration(configuration))
        return {.error = NativeHostLaunchError::InvalidConfiguration};
    if ((arguments.size() != 2 && arguments.size() != 3) || arguments.front().isEmpty() ||
        arguments.front().contains(QChar{0}))
        return {.error = NativeHostLaunchError::InvalidArguments};
    if (arguments[1] != configuration.allowed_origin)
        return {.error = NativeHostLaunchError::UnauthorizedOrigin};
    std::optional<quint64> parent_window;
    if (arguments.size() == 3) {
        parent_window = canonicalWindow(arguments[2]);
        if (!parent_window)
            return {.error = NativeHostLaunchError::InvalidArguments};
    }
    return {.error = NativeHostLaunchError::None, .caller_origin = arguments[1], .parent_window = parent_window};
}

} // namespace music_player::m0
