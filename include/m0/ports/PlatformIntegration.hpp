#pragma once

#include <functional>
#include <string>

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
    // A native, non-blocking system notification (UTF-8 text). false when none can be shown.
    virtual bool showNotification(const std::string& title, const std::string& message) {
        static_cast<void>(title);
        static_cast<void>(message);
        return false;
    }
};

} // namespace music_player::m0
