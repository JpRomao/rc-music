#pragma once

#include <QStringList>

namespace music_player::m0 {
// Trusted composition helper: resolves safe executable basenames beside the
// process image reported by Windows, without CWD, argv, environment or file I/O.
// The resulting paths identify peers; they do not attest code or browser origin.
[[nodiscard]] QStringList nativeDownloadSiblingImages(const QStringList& names);
} // namespace music_player::m0
