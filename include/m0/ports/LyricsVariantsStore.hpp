#pragma once

#include "m0/domain/LyricsVariants.hpp"

#include <filesystem>

namespace music_player::m0 {
enum class LyricsVariantsLoadStatus { Missing, Loaded, RecoveredFromBackup, Invalid, FutureVersion, Unavailable };
enum class LyricsVariantsSaveStatus { Saved, Failed, BlockedByFutureVersion };
enum class LyricsSelectionKind { Automatic, Source, Hidden, Variant };
struct LyricsSelection final {
    LyricsSelectionKind kind{LyricsSelectionKind::Automatic};
    std::uint64_t variant_id{};
    bool operator==(const LyricsSelection&) const = default;
};
struct LyricsVariantsLoadResult final {
    LyricsVariants variants{};
    LyricsVariantsLoadStatus status{LyricsVariantsLoadStatus::Missing};
    LyricsSelection selection{};
};
// The caller runs this synchronous port off the presentation thread. A path is
// an exact local track identity, never authority to read or modify its audio.
class LyricsVariantsStore {
  public:
    virtual ~LyricsVariantsStore() = default;
    [[nodiscard]] virtual LyricsVariantsLoadResult load(const std::filesystem::path& track) = 0;
    [[nodiscard]] virtual LyricsVariantsSaveStatus save(const std::filesystem::path& track,
                                                        const LyricsVariants& variants) = 0;
    // Updates the remembered choice without changing saved versions.
    [[nodiscard]] virtual LyricsVariantsSaveStatus saveSelection(const std::filesystem::path&, LyricsSelection) {
        return LyricsVariantsSaveStatus::Failed;
    }
};
} // namespace music_player::m0
