#pragma once

namespace music_player::m0 {

class PlatformIntegration {
  public:
    virtual ~PlatformIntegration() = default;

    [[nodiscard]] virtual bool trayAvailable() const noexcept = 0;
    virtual void hideWindow() = 0;
    virtual void minimizeWindow() = 0;
    virtual void showAndActivateWindow() = 0;
    virtual void requestExit() = 0;
};

} // namespace music_player::m0
