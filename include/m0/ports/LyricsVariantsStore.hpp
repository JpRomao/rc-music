#pragma once

#include "m0/domain/LyricsVariants.hpp"

#include <filesystem>

namespace music_player::m0 {
enum class LyricsVariantsLoadStatus { Missing, Loaded, RecoveredFromBackup, Invalid, FutureVersion, Unavailable };
enum class LyricsVariantsSaveStatus { Saved, Failed, BlockedByFutureVersion };
struct LyricsVariantsLoadResult final {
    LyricsVariants variants{};
    LyricsVariantsLoadStatus status{LyricsVariantsLoadStatus::Missing};
};
// The caller runs this synchronous port off the presentation thread. A path is
// an exact local track identity, never authority to read or modify its audio.
class LyricsVariantsStore {
  public:
    virtual ~LyricsVariantsStore() = default;
    [[nodiscard]] virtual LyricsVariantsLoadResult load(const std::filesystem::path& track) = 0;
    [[nodiscard]] virtual LyricsVariantsSaveStatus save(const std::filesystem::path& track,
                                                        const LyricsVariants& variants) = 0;
};
} // namespace music_player::m0
