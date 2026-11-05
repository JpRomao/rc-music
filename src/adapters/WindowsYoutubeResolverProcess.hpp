#pragma once

#include "WindowsDownloadProcess.hpp"
#include "YoutubeResolverSession.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
struct WindowsYoutubeResolverPreparedProcess final {
    WindowsDownloadProcessConfig config{};
    // Trusted preparation retains the authenticated private runtime. A nonnull
    // lease is required and remains alive until native exit and pipe EOF ACK.
    std::shared_ptr<const void> lifetime{};
};

// Worker-only, one-shot adapter. Construction is passive; trusted preparation
// and native allocation happen only in start(). No executable, environment or
// arguments come from a browser/page. The supplied environment must be explicit.
// WindowsDownloadProcess keeps its NoChild restriction: this does not qualify
// a yt-dlp runtime that needs a Node descendant or authorize its deployment.
// The owner must cancel/poll to a terminal ACK before destroying this adapter.
// Misuse teardown tries cancellation/drain for at most one worker second; if
// ACK is still uncertain, it deliberately retains native state and its lease
// in heap memory. Destruction is never permission to remove the job workspace.
class WindowsYoutubeResolverProcess final : public YoutubeResolverProcess {
  public:
    using Prepare = std::function<std::optional<WindowsYoutubeResolverPreparedProcess>(const QString& video_id)>;
    explicit WindowsYoutubeResolverProcess(Prepare prepare);
    ~WindowsYoutubeResolverProcess() override;
    [[nodiscard]] bool start(const QString& video_id, qint64 now_ms) override;
    [[nodiscard]] YoutubeResolverProcessUpdate poll(qint64 now_ms) override;
    void cancel() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
