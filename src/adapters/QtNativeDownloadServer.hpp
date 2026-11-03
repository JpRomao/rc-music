#pragma once

#include "adapters/NativeDownloadPeerCheck.hpp"
#include "adapters/NativeHostLaunch.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"

#include <QObject>

#include <functional>
#include <memory>

namespace music_player::m0 {
struct QtNativeDownloadServerOptions final {
    // All callbacks are prompt trusted application-thread observations. They
    // cannot wait, perform filesystem/store I/O, or tick/mutate the owner.
    std::function<bool()> available{};
    std::function<qint64()> now_ms{};
    bool automatic_polling{true};
    qsizetype max_active_connections{8};
    // Trusted prompt construction only; production defaults to the concrete
    // Windows image/user worker. Never supplied by the IPC peer or QML.
    NativeDownloadPeerCheckFactory peer_check_factory{};
};

// A primary app's dedicated optional ingress. Configuration is copied/trusted;
// submission remains borrowed through shutdown/drain. Callbacks must not destroy
// the ingress or borrowed owner synchronously. Shutdown closes observers only,
// never accepted jobs; no network transfer, store I/O or controller tick occurs.
// Both the cooperative marker and a native first-instance pipe reservation
// survive beginShutdown/stopped until destruction. Keep this object alive
// through the owner's worker teardown. Neither authenticates same-user peers.
class QtNativeDownloadServer final : public QObject {
    Q_OBJECT
  public:
    QtNativeDownloadServer(NativeHostConfiguration configuration, AsyncDownloadSubmission& submission,
                           QtNativeDownloadServerOptions options = {}, QObject* parent = nullptr);
    ~QtNativeDownloadServer() override;
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
