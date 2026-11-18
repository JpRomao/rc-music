#pragma once

#include "m0/domain/WindowPlacement.hpp"
#include "m0/ports/PlatformIntegration.hpp"

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;
class QWindow;

namespace music_player::m0 {

#ifdef Q_OS_WIN
class WindowsTaskbarEvents;
#endif

struct TrayMenuLabels final {
    QString play;
    QString pause;
    QString previous;
    QString next;
    QString show;
    QString hide;
    QString exit;
    QString no_track;
};

struct TrayMenuState final {
    QString title;
    bool transport_enabled{false};
    bool playing{false};
    bool can_go_previous{false};
    bool can_go_next{false};
    bool interaction_enabled{true};
};

struct DefaultAppsSettingsOperations final {
    std::function<bool()> availability_reader;
    std::function<bool(const QUrl&)> uri_opener;
};

class QtShellPlatform final : public QObject, public PlatformIntegration {
    Q_OBJECT

  public:
    // Construct after QApplication. Supplied callbacks keep adapter tests offscreen and deterministic.
    QtShellPlatform(QIcon icon, QString app_name, TrayMenuLabels labels, std::function<bool()> availability_reader = {},
                    std::function<void()> quit_request = {}, QObject* parent = nullptr);
    QtShellPlatform(QIcon icon, QString app_name, TrayMenuLabels labels, std::function<bool()> availability_reader,
                    std::function<void()> quit_request, DefaultAppsSettingsOperations default_apps_settings,
                    QObject* parent = nullptr);
    ~QtShellPlatform() override;

    void bindWindow(QWindow& window);
    void flushPendingWindowPlacement();
    void updateTrayState(const TrayMenuState& state);
    void refreshAvailability();
    void handleTrayHostRestart();
    [[nodiscard]] QMenu* menu() const noexcept;
    [[nodiscard]] QSystemTrayIcon* trayIcon() const noexcept;

    // Combines shell presence with icon intent, not an acknowledgement of native icon registration.
    [[nodiscard]] bool trayAvailable() const noexcept override;
    [[nodiscard]] bool defaultAppsSettingsSupported() const noexcept override;
    [[nodiscard]] bool defaultAppsSettingsAvailable() const noexcept override;
    void openDefaultAppsSettings(DefaultAppsSettingsCompletion completion) override;
    void hideWindow() override;
    void minimizeWindow() override;
    void showAndActivateWindow() override;
    void requestExit() override;
    // Shown as a tray message, only while the tray is available.
    bool showNotification(const std::string& title, const std::string& message) override;

  signals:
    void playPauseRequested();
    void previousRequested();
    void nextRequested();
    void hideRequested();
    void restoreRequested();
    void exitRequested();
    void availabilityChanged();
    void windowPlacementChanged(WindowPlacement placement);
    // An explicit hide was converted to a normal minimization after a fresh capability read.
    void hideFellBackToMinimize();

  private:
    void updateMenu();
    void updatePolling();
    void rememberWindowState();
    void scheduleWindowPlacementCapture();
    void publishWindowPlacement();

    TrayMenuLabels labels_;
    TrayMenuState state_;
    std::function<bool()> availability_reader_;
    std::function<void()> quit_request_;
    DefaultAppsSettingsOperations default_apps_settings_;
    std::unique_ptr<QMenu> menu_;
    QSystemTrayIcon* tray_icon_;
    QTimer* availability_timer_;
    std::unique_ptr<QTimer> window_placement_timer_;
#ifdef Q_OS_WIN
    std::unique_ptr<WindowsTaskbarEvents> taskbar_events_;
    bool taskbar_monitor_ready_{false};
#endif
    QAction* current_track_{nullptr};
    QAction* play_pause_{nullptr};
    QAction* previous_{nullptr};
    QAction* next_{nullptr};
    QAction* show_hide_{nullptr};
    QAction* exit_{nullptr};
    QPointer<QWindow> window_;
    std::uint64_t window_binding_generation_{0};
    std::uint64_t scheduled_window_binding_generation_{0};
    std::optional<WindowPlacement> pending_window_placement_;
    Qt::WindowStates restore_states_{Qt::WindowNoState};
    bool available_{false};
    bool hidden_to_tray_{false};
    bool tray_recovery_pending_{false};
    bool exit_requested_{false};
    bool window_placement_flush_in_progress_{false};
};

} // namespace music_player::m0
