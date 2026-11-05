#pragma once

#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"
#include "presentation/DownloadApplicationBinding.hpp"

#include <memory>
#include <string>

class QNetworkAccessManager;

namespace music_player::m0 {
class AsyncDownloadSubmission;
class AsyncYoutubeAudioSubmission;
class PlayerPresenter;
class WindowsDownloadExecution;
class LyricsBinding;
struct SessionLoadResult;

struct WindowsPlayerDownloadsOptions final {
    // Empty selects a fresh UUID namespace. Trusted composition only.
    std::string job_namespace{};
    DownloadApplicationBindingOptions binding{};
    // Optional shared writer owned by the player. Creates one private lookup
    // service/view only when present. Borrowed dependencies outlive this owner.
    LyricsBinding* lyrics_writer{nullptr};
    QNetworkAccessManager* lyrics_network{nullptr};
};

// Optional Windows application-thread composition, never a QML-created object.
// Owns execution, durable admission and Session-mode import as one lifecycle;
// creates no directory, ingress or installation profile. The store factory and
// all checkpoint I/O/destruction run on the checkpoint worker. Execution keeps
// its existing private Qt worker. All configuration is supplied by trusted C++.
// Presenter/session autosave and any borrowed binding recovery ports outlive us.
// Normal shutdown is presenter.requestExit(), through the existing binding exit
// gate; keep this owner alive until stopped() and the player's exit decision.
// Destruction only provides the workers' existing blocking fallback, not a
// prompt UI shutdown protocol. Use the same UI ABI as the borrowed presenter.
class WindowsPlayerDownloads final {
  public:
    WindowsPlayerDownloads(PlayerPresenter& presenter, const SessionLoadResult& session,
                           std::unique_ptr<WindowsDownloadExecution> execution,
                           SingleWorkerDownloadCheckpointIo::Factory checkpoint_store,
                           WindowsPlayerDownloadsOptions options = {});
    ~WindowsPlayerDownloads();
    WindowsPlayerDownloads(const WindowsPlayerDownloads&) = delete;
    WindowsPlayerDownloads& operator=(const WindowsPlayerDownloads&) = delete;

    [[nodiscard]] bool start();
    [[nodiscard]] DownloadApplicationBinding* binding() const;
    // Both independent ingress ports use this same durable controller/queue.
    // Never expose submit/restore on the service. YouTube still requires a
    // trusted resolver-capable execution configuration and authenticated host.
    [[nodiscard]] AsyncDownloadSubmission& submission();
    [[nodiscard]] AsyncYoutubeAudioSubmission& youtubeSubmission();
    void poll();
    // Physical service/checkpoint/execution drain, not proof of Saved. Failed
    // final persistence still requires the binding's explicit retry/decision.
    [[nodiscard]] bool stopped() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
