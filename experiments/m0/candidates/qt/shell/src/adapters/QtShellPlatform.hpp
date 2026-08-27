#pragma once

#include "m0/ports/PlatformIntegration.hpp"

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;
class QWindow;

namespace music_player::m0 {

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
};

class QtShellPlatform final : public QObject, public PlatformIntegration {
    Q_OBJECT

  public:
    // Construct after QApplication. Supplied callbacks keep adapter tests offscreen and deterministic.
    QtShellPlatform(QIcon icon, QString app_name, TrayMenuLabels labels, std::function<bool()> availability_reader = {},
                    std::function<void()> quit_request = {}, QObject* parent = nullptr);
    ~QtShellPlatform() override;

    void bindWindow(QWindow& window);
    void updateTrayState(const TrayMenuState& state);
    void refreshAvailability();
    [[nodiscard]] QMenu* menu() const noexcept;
    [[nodiscard]] QSystemTrayIcon* trayIcon() const noexcept;

    // Availability combines the reader with this adapter's icon intent. Qt's Windows reader
    // always returns true; neither it nor isVisible() proves native registration or Explorer recovery.
    [[nodiscard]] bool trayAvailable() const noexcept override;
    void hideWindow() override;
    void minimizeWindow() override;
    void showAndActivateWindow() override;
    void requestExit() override;

  signals:
    void playPauseRequested();
    void previousRequested();
    void nextRequested();
    void hideRequested();
    void restoreRequested();
    void exitRequested();
    void availabilityChanged();
    // An explicit hide was converted to a normal minimization after a fresh capability read.
    void hideFellBackToMinimize();

  private:
    void updateMenu();
    void updatePolling();
    void rememberWindowState();

    TrayMenuLabels labels_;
    TrayMenuState state_;
    std::function<bool()> availability_reader_;
    std::function<void()> quit_request_;
    std::unique_ptr<QMenu> menu_;
    QSystemTrayIcon* tray_icon_;
    QTimer* availability_timer_;
    QAction* current_track_{nullptr};
    QAction* play_pause_{nullptr};
    QAction* previous_{nullptr};
    QAction* next_{nullptr};
    QAction* show_hide_{nullptr};
    QAction* exit_{nullptr};
    QPointer<QWindow> window_;
    Qt::WindowStates restore_states_{Qt::WindowNoState};
    bool available_{false};
    bool exit_requested_{false};
};

} // namespace music_player::m0
