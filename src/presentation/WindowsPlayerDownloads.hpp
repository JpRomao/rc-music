#pragma once

#include "adapters/NativeDownloadPeerCheck.hpp"
#include "adapters/NativeHostLaunch.hpp"
#include "adapters/QtLyricsService.hpp"
#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"
#include "presentation/DownloadApplicationBinding.hpp"

#include <memory>
#include <optional>
#include <string>

class QNetworkAccessManager;

namespace music_player::m0 {
class AsyncDownloadSubmission;
class AsyncYoutubeAudioSubmission;
class PlayerPresenter;
class WindowsDownloadExecution;
class LyricsBinding;
struct SessionLoadResult;
struct WindowsDownloadExecutionProfile;

struct WindowsPlayerNativeIngressOptions final {
    // Trusted optional profiles only, never values from a page or checkpoint.
    // Both origin and endpoint empty disable a channel; partial profiles fail.
    // Defaults open no listener.
    NativeHostConfiguration principal{}, youtube{};
    NativeDownloadPeerCheckFactory principal_peer_check_factory{}, youtube_peer_check_factory{};
};

struct WindowsPlayerDownloadsOptions final {
    // Empty selects a fresh UUID namespace. Trusted composition only.
    std::string job_namespace{};
    DownloadApplicationBindingOptions binding{};
    // Optional shared writer owned by the player. Creates one private lookup
    // service/view only when present. Borrowed dependencies outlive this owner.
    LyricsBinding* lyrics_writer{nullptr};
    QNetworkAccessManager* lyrics_network{nullptr};
    std::optional<WindowsPlayerNativeIngressOptions> native_ingress{};
    // Trusted opt-in for a qualified embedded media composition.
    bool embedded_browser_ingress{false};
    bool configurable_download_directory{false};
    LyricFindOptions lyricfind{};
};

// Optional Windows application-thread composition, never a QML-created object.
// Owns execution, durable admission and Session-mode import as one lifecycle;
// creates no directory or installation profile. Native ingress is inactive by
// default; trusted optional profiles open only at start(). The store factory and
// all checkpoint I/O/destruction run on the checkpoint worker. Execution keeps
// its existing private Qt worker. All configuration is supplied by trusted C++.
// Presenter/session autosave and any borrowed binding recovery ports outlive us.
// Normal shutdown is presenter.requestExit(), through the existing binding exit
// gate; keep this owner alive until stopped() and the player's exit decision.
// Destruction only provides the workers' existing blocking fallback, not a
// prompt UI shutdown protocol. Use the same UI ABI as the borrowed presenter.
class WindowsPlayerDownloads final {
  public:
    // The configured owner uses the same trusted journal and workspace for
    // execution and explicit recovery. Borrowed ports remain a legacy override.
    [[nodiscard]] static std::unique_ptr<WindowsPlayerDownloads> createConfigured(
        PlayerPresenter& presenter, const SessionLoadResult& session, WindowsDownloadExecutionProfile profile,
        SingleWorkerDownloadCheckpointIo::Factory checkpoint_store, WindowsPlayerDownloadsOptions options = {});
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
    // Borrowed UI-thread port. Availability is rechecked at every admission.
    [[nodiscard]] AsyncDownloadSubmission* embeddedBrowserSubmission() const;
    void poll();
    // Physical service/checkpoint/execution drain, not proof of Saved. Failed
    // final persistence still requires the binding's explicit retry/decision.
    [[nodiscard]] bool stopped() const;

  private:
    WindowsPlayerDownloads(PlayerPresenter& presenter, const SessionLoadResult& session,
                           std::unique_ptr<WindowsDownloadExecution> execution,
                           SingleWorkerDownloadCheckpointIo::Factory checkpoint_store,
                           WindowsPlayerDownloadsOptions options, std::unique_ptr<DownloadRecoveryReview> review,
                           std::unique_ptr<DownloadRecoveryValidation> validation);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
