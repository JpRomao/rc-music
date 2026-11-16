#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {

// ADR-0019: online search returns metadata only. A result never carries a
// media URL, cookie or header, and choosing one is an ordinary acquisition
// request by video ID through the existing download admission.
struct OnlineSearchResult final {
    std::string video_id{}, title{}, channel{};
    std::optional<int> duration_seconds{};
    bool operator==(const OnlineSearchResult&) const = default;
};

enum class OnlineSearchError { None, Unavailable, TimedOut, Failed };

struct OnlineSearchOutcome final {
    std::uint64_t ticket{};
    OnlineSearchError error{OnlineSearchError::None};
    bool cancelled{false};
    std::vector<OnlineSearchResult> results{};
};

// UI-thread port. One search at a time; work happens off the UI thread.
class OnlineSearch {
  public:
    virtual ~OnlineSearch() = default;
    // A ticket, or nothing when a search is still running or the query is not
    // searchable (blank, control characters or too long).
    [[nodiscard]] virtual std::optional<std::uint64_t> start(const std::string& query) = 0;
    // Nonblocking. Returns each finished search exactly once.
    [[nodiscard]] virtual std::optional<OnlineSearchOutcome> poll() = 0;
    // Asks the running search to stop; its outcome still arrives through poll.
    virtual void cancel() = 0;
};

} // namespace music_player::m0
