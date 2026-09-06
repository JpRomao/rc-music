#pragma once

#include <functional>

namespace music_player::m0 {

enum class DefaultAppsSettingsLaunchResult {
    Dispatched,
    Unavailable,
    Failed,
};

using DefaultAppsSettingsCompletion = std::function<void(DefaultAppsSettingsLaunchResult)>;

class PlatformIntegration {
  public:
    virtual ~PlatformIntegration() = default;

    [[nodiscard]] virtual bool trayAvailable() const noexcept = 0;
    [[nodiscard]] virtual bool defaultAppsSettingsSupported() const noexcept { return false; }
    [[nodiscard]] virtual bool defaultAppsSettingsAvailable() const noexcept { return false; }
    // Completion is delivered exactly once on the thread that initiated the operation. Dispatched
    // proves only that the registered URI handler accepted the request, never that a default changed.
    virtual void openDefaultAppsSettings(DefaultAppsSettingsCompletion completion) {
        if (completion)
            completion(DefaultAppsSettingsLaunchResult::Unavailable);
    }
    virtual void hideWindow() = 0;
    virtual void minimizeWindow() = 0;
    virtual void showAndActivateWindow() = 0;
    virtual void requestExit() = 0;
};

} // namespace music_player::m0
