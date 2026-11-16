#pragma once

#include "YoutubeResolverSession.hpp"
#include "m0/ports/OnlineSearch.hpp"

#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace music_player::m0 {
// ADR-0019 search mode of the packaged resolver (`--search <query>`).
inline constexpr std::size_t youtube_search_max_results = 12;
inline constexpr qsizetype youtube_search_max_query_bytes = 400;
inline constexpr qsizetype youtube_search_max_text_bytes = 512;
inline constexpr int youtube_search_max_duration_seconds = 7 * 24 * 3600;

struct YoutubeSearchResult final {
    QString video_id{}, title{}, channel{};
    std::optional<int> duration_seconds{};
    bool operator==(const YoutubeSearchResult&) const = default;
};

// Collapses whitespace. Empty when the text cannot be searched: blank, with
// control characters or over the UTF-8 byte limit. The resolver accepts only
// this exact form, so the app always sends it normalized.
[[nodiscard]] QString normalizedYoutubeSearchQuery(const QString& text);
// Strict decode of the resolver's `{"entries": [...]}` data. Metadata only: an
// unknown field, duplicate or invalid ID, or too many entries fails the list.
[[nodiscard]] std::optional<std::vector<YoutubeSearchResult>> decodeYoutubeSearchResults(const QByteArray& json);

enum class YoutubeSearchState { Idle, Searching, Stopping, Ready, Failed, Cancelled };
struct YoutubeSearchLimits final {
    quint64 max_stdout_bytes{256 * 1024};
    quint64 max_stderr_bytes{128 * 1024};
    qint64 total_timeout_ms{30 * 1000};
};
struct YoutubeSearchSnapshot final {
    YoutubeSearchState state{YoutubeSearchState::Idle};
    DownloadFailure error{DownloadFailure::None};
    std::vector<YoutubeSearchResult> results{};
};

// One-shot worker session over a search-mode resolver process. Results count
// only after exit 0 with every pipe acknowledged. The owner drains to terminal
// before destroying this object or the process.
class YoutubeSearchSession final {
  public:
    explicit YoutubeSearchSession(YoutubeResolverProcess& process, YoutubeSearchLimits limits = {});
    ~YoutubeSearchSession();
    [[nodiscard]] bool start(const QString& query, qint64 now_ms);
    [[nodiscard]] YoutubeSearchSnapshot poll(qint64 now_ms);
    void cancel();
    [[nodiscard]] YoutubeSearchSnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Runs one search session at a time on a private thread, with an event loop
// for the native process. Destruction cancels and waits for the drain.
class ThreadedYoutubeSearch final : public OnlineSearch {
  public:
    using ProcessFactory = std::function<std::unique_ptr<YoutubeResolverProcess>()>;
    explicit ThreadedYoutubeSearch(ProcessFactory factory, YoutubeSearchLimits limits = {});
    ~ThreadedYoutubeSearch() override;
    ThreadedYoutubeSearch(const ThreadedYoutubeSearch&) = delete;
    ThreadedYoutubeSearch& operator=(const ThreadedYoutubeSearch&) = delete;
    [[nodiscard]] std::optional<std::uint64_t> start(const std::string& query) override;
    [[nodiscard]] std::optional<OnlineSearchOutcome> poll() override;
    void cancel() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
