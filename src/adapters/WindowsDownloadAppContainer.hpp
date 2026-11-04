#pragma once

#include <QByteArray>

#include <memory>

namespace music_player::m0 {
class WindowsDownloadNativeUseFence;

// Immutable identity supplied only by trusted composition for an AppContainer
// profile that was already provisioned. This validates/copies a binary profile
// SID; it does not discover, derive, create, authenticate or delete a profile.
// No network or other capabilities are granted by this identity.
class WindowsDownloadAppContainer final {
  public:
    [[nodiscard]] static std::shared_ptr<const WindowsDownloadAppContainer>
    fromTrustedSid(QByteArray binary_sid, std::shared_ptr<WindowsDownloadNativeUseFence> fence = {});
    WindowsDownloadAppContainer(const WindowsDownloadAppContainer&) = delete;
    WindowsDownloadAppContainer& operator=(const WindowsDownloadAppContainer&) = delete;
    [[nodiscard]] const QByteArray& sidBytes() const noexcept;
    [[nodiscard]] std::shared_ptr<WindowsDownloadNativeUseFence> nativeUseFence() const noexcept;

  private:
    WindowsDownloadAppContainer(QByteArray sid, std::shared_ptr<WindowsDownloadNativeUseFence> fence);
    const QByteArray sid_;
    const std::shared_ptr<WindowsDownloadNativeUseFence> native_use_fence_;
};

} // namespace music_player::m0
