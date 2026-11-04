#pragma once

#include <QByteArray>

#include <memory>

namespace music_player::m0 {

// Immutable identity supplied only by trusted composition for an AppContainer
// profile that was already provisioned. This validates/copies a binary profile
// SID; it does not discover, derive, create, authenticate or delete a profile.
// No network or other capabilities are granted by this identity.
class WindowsDownloadAppContainer final {
  public:
    [[nodiscard]] static std::shared_ptr<const WindowsDownloadAppContainer> fromTrustedSid(QByteArray binary_sid);
    WindowsDownloadAppContainer(const WindowsDownloadAppContainer&) = delete;
    WindowsDownloadAppContainer& operator=(const WindowsDownloadAppContainer&) = delete;
    [[nodiscard]] const QByteArray& sidBytes() const noexcept;

  private:
    explicit WindowsDownloadAppContainer(QByteArray sid);
    const QByteArray sid_;
};

} // namespace music_player::m0
