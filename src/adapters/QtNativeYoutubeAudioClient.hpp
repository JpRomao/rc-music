#pragma once

#include "adapters/NativeDownloadPeerCheck.hpp"
#include "adapters/NativeHostLaunch.hpp"
#include "m0/ports/AsyncYoutubeAudioSubmission.hpp"

#include <QObject>

#include <functional>
#include <memory>

namespace music_player::m0 {
struct QtNativeYoutubeAudioClientOptions final {
    // Prompt trusted caller-thread observation only; never a page setting.
    std::function<qint64()> now_ms{};
    NativeDownloadPeerCheckFactory peer_check_factory{};
};

// One dedicated YouTube IPC attempt. Pending never acknowledges admission.
// An exchange uncertain after possible write throws on poll; no reconnect,
// retransmission, owner cancellation or player launch is inferred from EOF.
// shutdown/stopped retain the existing physical peer-worker drain contract.
class QtNativeYoutubeAudioClient final : public QObject, public AsyncYoutubeAudioSubmission {
    Q_OBJECT
  public:
    explicit QtNativeYoutubeAudioClient(NativeHostConfiguration configuration,
                                        QtNativeYoutubeAudioClientOptions options = {}, QObject* parent = nullptr);
    ~QtNativeYoutubeAudioClient() override;
    [[nodiscard]] DownloadAdmissionResult begin(const YoutubeAudioRequest& request) override;
    [[nodiscard]] std::optional<DownloadSubmissionResult> poll(std::string_view request_id) override;
    void shutdown();
    [[nodiscard]] bool stopped() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
