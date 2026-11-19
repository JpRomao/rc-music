#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// S13.1: `--profile-root <dir>` runs a second, isolated RC Music beside the first on one computer
// (listening room test). Only builds configured with RC_MUSIC_TEST_PROFILE_ROOT=ON compile this into
// m0_qt_shell; the product never sees the option and treats it as any other argument.
namespace music_player::m0::experiment {

struct TestProfileRootArguments final {
    // Cleaned absolute directory; nothing exists on disk until prepareTestProfileRoot.
    std::optional<QString> profile_root{};
    // The launch arguments without the option and its value; the program name stays first.
    QStringList remaining_arguments{};
};

// Takes `--profile-root <dir>` out of the launch arguments (never after `--`). Throws
// std::invalid_argument with the reason when the option is repeated, has no value, uses the
// `--profile-root=<dir>` form or names a path that is not fully qualified.
[[nodiscard]] TestProfileRootArguments takeTestProfileRoot(const QStringList& arguments);

// Creates the directory (and its parents) when missing and returns its cleaned path. Throws
// std::invalid_argument when the path is not fully qualified, is not a directory or cannot be created.
[[nodiscard]] QString prepareTestProfileRoot(const QString& profile_root);

} // namespace music_player::m0::experiment
