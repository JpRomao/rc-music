#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace music_player::m0 {
// Reads a user-chosen PNG or JPEG (detected by content, at most
// maximum_custom_avatar_input_bytes), center-crops it to a square and scales it to
// avatar_pixels x avatar_pixels. Returns the PNG bytes, or nothing for any other input.
[[nodiscard]] std::optional<QByteArray> normalizeAvatarImage(const QString& path);

// Writes <config_directory>/avatars/custom.png with a confirmed write and returns the
// lowercase hexadecimal SHA-256 of the bytes written, or an empty string on failure.
[[nodiscard]] QString saveCustomAvatar(const QString& config_directory, const QByteArray& png);
} // namespace music_player::m0
