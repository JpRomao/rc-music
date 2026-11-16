#pragma once

#include "WindowsYoutubeRuntime.hpp"
#include "YoutubeResolverSession.hpp"

#include <QProcessEnvironment>

#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
// Resolve: one video ID, extractor plus Node for JavaScript challenges.
// Search (ADR-0019): one normalized query sent as `--search <query>`. The
// extractor lists metadata only, so a challenge frame fails and Node never runs.
// Lookup (L7): 1 to 20 video IDs joined by commas, sent as `--lookup <ids>`,
// with the same metadata-only rules as the search.
enum class WindowsYoutubeBrokerMode { Resolve, Search, Lookup };
struct WindowsYoutubeBrokerPreparedRuntime final {
    std::shared_ptr<const WindowsYoutubeRuntime> files{};
    // Trusted explicit environment; never inherited from the page or host.
    QProcessEnvironment environment{};
};

// Worker-only resolver for the custom pinned Python/provider entry point.
// Extractor and Node are siblings, each supervised by its own NoChild Job=1.
// No executable, argv, source path or program is accepted from the browser.
// All challenge frames stay internal; only an acknowledged final projection
// reaches YoutubeResolverSession. This is not production sandbox qualification.
// start prepares/validates only. First uncancelled poll launches the extractor,
// so the executor observes cancel/shutdown received during preparation first.
class WindowsYoutubeBrokerProcess final : public YoutubeResolverProcess {
  public:
    using Prepare = std::function<std::optional<WindowsYoutubeBrokerPreparedRuntime>()>;
    explicit WindowsYoutubeBrokerProcess(Prepare prepare,
                                         WindowsYoutubeBrokerMode mode = WindowsYoutubeBrokerMode::Resolve);
    ~WindowsYoutubeBrokerProcess() override;
    [[nodiscard]] bool start(const QString& video_id, qint64 now_ms) override;
    [[nodiscard]] YoutubeResolverProcessUpdate poll(qint64 now_ms) override;
    void cancel() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
