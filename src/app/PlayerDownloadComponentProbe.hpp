#pragma once

#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
enum class PlayerDownloadComponentState { Enabled, Absent, Invalid, Failed };
using PlayerDownloadComponentReader = std::function<PlayerDownloadComponentState()>;

// Trusted worker-only marker inspection; no tool execution or directory writes.
[[nodiscard]] PlayerDownloadComponentState readPlayerDownloadComponentMarker(const QString& application_directory);
[[nodiscard]] PlayerDownloadComponentState readInstalledPlayerDownloadComponentMarker();

// Passive, one-shot worker. A result is observable once, only after reader,
// captures and the actual worker have stopped. Cancel irrevocably seals results.
// Owner operations are serialized on one thread. The reader must not destroy
// its own probe; destruction cancels and joins as a blocking fallback.
class PlayerDownloadComponentProbe final {
  public:
    explicit PlayerDownloadComponentProbe(
        PlayerDownloadComponentReader reader = readInstalledPlayerDownloadComponentMarker);
    ~PlayerDownloadComponentProbe();
    PlayerDownloadComponentProbe(const PlayerDownloadComponentProbe&) = delete;
    PlayerDownloadComponentProbe& operator=(const PlayerDownloadComponentProbe&) = delete;
    [[nodiscard]] bool start();
    [[nodiscard]] std::optional<PlayerDownloadComponentState> poll();
    void cancel();
    [[nodiscard]] bool stopped() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
