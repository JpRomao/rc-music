#pragma once

#include <QString>

#include <cstddef>
#include <string>
#include <vector>

namespace music_player::m0 {
// L7: links pasted on the Search page. Each YouTube link is one audio by video
// ID. A Letras page on the same line as a YouTube link, or on the line right
// after it, becomes that audio's lyrics page. Nothing here opens a page.
inline constexpr std::size_t pasted_youtube_links_max = 20; // One audio request.
inline constexpr qsizetype pasted_youtube_links_max_chars = 64 * 1024;

struct PastedYoutubeLink final {
    std::string video_id{}, lyrics_page{};
    bool operator==(const PastedYoutubeLink&) const = default;
};
enum class PastedLinkIssueKind { NotALink, LetrasWithoutYoutube, Duplicate, TooMany, TextTooLong };
struct PastedLinkIssue final {
    int line{}; // 1-based; 0 for the whole text.
    PastedLinkIssueKind kind{PastedLinkIssueKind::NotALink};
    bool operator==(const PastedLinkIssue&) const = default;
};
struct PastedYoutubeLinks final {
    std::vector<PastedYoutubeLink> links{};
    std::vector<PastedLinkIssue> issues{};
};

[[nodiscard]] PastedYoutubeLinks parsePastedYoutubeLinks(const QString& text);
} // namespace music_player::m0
