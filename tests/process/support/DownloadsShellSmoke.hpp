#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace music_player::m0 {
class PlayerPresenter;
class PlaybackBackend;
class DownloadApplicationBinding;
struct SessionLoadResult;

// Instrumented shell only. Real UI/service/import/session/checkpoint workers;
// trusted authenticated MP3 stands in for publication. No network/conversion
// claims or production fallback executor. A separate capability can compose
// the real native ingress with a synthetic test-only trusted identity.
class DownloadsShellSmoke final {
  public:
    DownloadsShellSmoke(PlayerPresenter& presenter, PlaybackBackend& backend, const SessionLoadResult& session,
                        const QString& config_root, bool native_ingress = false);
    ~DownloadsShellSmoke();
    [[nodiscard]] DownloadApplicationBinding* binding() const;
    void bindWindow(QObject& root);
    [[nodiscard]] bool completed() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
