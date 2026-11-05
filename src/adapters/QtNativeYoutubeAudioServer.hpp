#pragma once

#include "adapters/NativeDownloadPeerCheck.hpp"
#include "adapters/NativeHostLaunch.hpp"
#include "m0/ports/AsyncYoutubeAudioSubmission.hpp"

#include <QObject>

#include <functional>
#include <memory>

namespace music_player::m0 {
struct QtNativeYoutubeAudioServerOptions final {
    // Prompt trusted application-thread observations only. Never wait, perform
    // filesystem/store I/O or tick/mutate the borrowed application owner here.
    std::function<bool()> available{};
    std::function<qint64()> now_ms{};
    bool automatic_polling{true};
    qsizetype max_active_connections{8};
    NativeDownloadPeerCheckFactory peer_check_factory{};
};

// Separate optional YouTube ingress with a fixed protocol and borrowed owner.
// Closing observers never cancels or retries accepted jobs. Keep this object
// and its cooperative/native pipe reservations alive through owner teardown;
// stopped() acknowledges ingress drain, not the owner's accepted work. Trusted
// callbacks cannot synchronously destroy this object or the borrowed owner.
// Same-user image eligibility is not browser origin/publisher attestation.
class QtNativeYoutubeAudioServer final : public QObject {
    Q_OBJECT
  public:
    QtNativeYoutubeAudioServer(NativeHostConfiguration configuration, AsyncYoutubeAudioSubmission& submission,
                               QtNativeYoutubeAudioServerOptions options = {}, QObject* parent = nullptr);
    ~QtNativeYoutubeAudioServer() override;
    [[nodiscard]] bool start();
    void poll();
    void beginShutdown();
    [[nodiscard]] bool stopped() const;
    [[nodiscard]] qsizetype activeConnectionCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
