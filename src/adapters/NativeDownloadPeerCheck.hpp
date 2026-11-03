#pragma once

#include <QStringList>

#include <functional>
#include <memory>

namespace music_player::m0 {
enum class NativeDownloadPeerSide { Client, Server };
enum class NativeDownloadPeerState { Idle, Pending, Eligible, Rejected };

// Prompt lexical validation only; never resolves or opens an image path.
[[nodiscard]] bool validNativePeerImages(const QStringList& expected_images);

// Expected images come exclusively from trusted build/application composition.
// Eligibility identifies image path/current user, not binary integrity, browser
// origin, publisher, loaded modules or absence of same-user process injection.
// Methods are prompt observations on one caller thread. No pipe payload is
// consumed or emitted here. Eligible completes the inspection; shutdown revokes
// it, and stopped requires physical worker/handle retirement. The caller keeps
// this check alive through normal drain and cannot be destroyed by its callbacks.
class NativeDownloadPeerCheck {
  public:
    virtual ~NativeDownloadPeerCheck() = default;
    [[nodiscard]] virtual bool begin(qintptr borrowed_pipe, NativeDownloadPeerSide side,
                                     const QStringList& expected_images) = 0;
    [[nodiscard]] virtual NativeDownloadPeerState poll() = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual bool stopped() = 0;
};

using NativeDownloadPeerCheckFactory = std::function<std::unique_ptr<NativeDownloadPeerCheck>()>;
} // namespace music_player::m0
