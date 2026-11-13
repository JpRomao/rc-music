#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QString;

namespace music_player::m0 {

// An optional local Python runtime selected from the application config root.
// Invoke the returned callable on a worker thread.
[[nodiscard]] std::function<std::optional<std::vector<std::string>>(const std::vector<std::string>&)>
makeQtLyricsRomanizer(const QString& config_root);

} // namespace music_player::m0
