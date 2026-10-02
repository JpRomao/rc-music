#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace music_player::m0 {

// Trusted composition/build profile only. Neither field comes from caller
// arguments, the environment, a page, or a Native Messaging/IPC message.
struct NativeHostConfiguration final {
    QString allowed_origin{};
    QString endpoint_name{};
};

enum class NativeHostLaunchError { None, Disabled, InvalidConfiguration, InvalidArguments, UnauthorizedOrigin };

struct NativeHostLaunchResult final {
    NativeHostLaunchError error{NativeHostLaunchError::InvalidArguments};
    QString caller_origin{};
    std::optional<quint64> parent_window{};
};

[[nodiscard]] bool isValidNativeHostConfiguration(const NativeHostConfiguration& configuration);
// arguments includes executable. parent_window is inert transport metadata,
// not ownership, authorization, a native window call, or a configuration source.
[[nodiscard]] NativeHostLaunchResult parseNativeHostLaunch(const QStringList& arguments,
                                                           const NativeHostConfiguration& configuration);

} // namespace music_player::m0
