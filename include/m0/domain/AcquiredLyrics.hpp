#pragma once

#include "m0/domain/LyricsVariants.hpp"

namespace music_player::m0 {
enum class AcquiredLyricsMergeStatus { Added, Unchanged, Conflict, Invalid, LimitExceeded };

struct AcquiredLyricsMergeResult final {
    AcquiredLyricsMergeStatus status{AcquiredLyricsMergeStatus::Invalid};
    LyricsVariants variants{};
};

// Pure, atomic merge of acquired drafts into an existing valid catalogue.
// Draft IDs are ignored. Exact canonical LRC content is deduplicated within
// (kind, language, script), preserving every saved entry and its ID verbatim.
// Divergent content for an existing form conflicts unless the caller supplies
// the user's explicit confirmation through keep_distinct_versions. No script,
// language, timestamps, translation or transliteration is inferred here.
// Added/Unchanged return the complete catalogue; failures return no catalogue.
[[nodiscard]] AcquiredLyricsMergeResult mergeAcquiredLyrics(const LyricsVariants& saved, const LyricsVariants& drafts,
                                                            bool keep_distinct_versions = false);
} // namespace music_player::m0
