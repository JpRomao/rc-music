#pragma once

#include "DirectDownloadSession.hpp"
#include "DownloadWorkerProtocol.hpp"
#include "WindowsDownloadProcess.hpp"

#include <memory>

namespace music_player::m0 {
class WindowsDownloadNetworkLaunch;

struct WindowsProcessDownloadIoConfig final {
    WindowsDownloadProcessConfig process{};
    WindowsDownloadTlsRuntimeConfig runtime{};
    quint64 max_wire_bytes{514ULL * 1024 * 1024};
    // When supplied, owns the authenticated runtime through process destruction;
    // process/runtime fields above are ignored, never overrides for this lease.
    std::shared_ptr<const WindowsDownloadNetworkLaunch> prepared_launch{};
};

// Worker-thread controller proxy. Each DNS/stream operation is a new diskless
// child. Successful DNS/EOF and pollStop require native exit + all pipe drains.
// A redirect queues at most one successor until that acknowledgement. HTTP and
// all-answers/public-IP policy remain in DirectDownloadSession, not this seam.
// Authenticated composition supplies prepared_launch; raw process/runtime remain
// a legacy synthetic test seam, not pre-launch proof. Drain native ACK before
// destruction. Distribution/sandbox/application gates remain open; not in player.
class WindowsProcessDownloadIo final : public DirectDownloadIo {
  public:
    explicit WindowsProcessDownloadIo(WindowsProcessDownloadIoConfig config);
    ~WindowsProcessDownloadIo() override;
    bool resolve(const QString& host) override;
    DownloadResolution resolution() const override;
    bool open(const PinnedDownloadEndpoint& endpoint) override;
    PinnedDownloadStreamUpdate poll() const override;
    QByteArray read(qint64 maximum) override;
    void cancel() override;
    DownloadFailure failure() const override;
    DirectDownloadIoStopUpdate pollStop() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
