#include "adapters/QtShellPlatform.hpp"

#ifdef Q_OS_WIN
#include "adapters/WindowsTaskbarEvents.hpp"
#endif

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>

#include <utility>

namespace music_player::m0 {
namespace {

[[nodiscard]] QString menuTitle(const QString& source, const QFont& font) {
    // QAction text is plain text, but ampersands introduce mnemonics and tabs add a shortcut column.
    QString title = QFontMetricsF(font).elidedText(source.simplified(), Qt::ElideRight, 280);
    title.replace(QStringLiteral("&"), QStringLiteral("&&"));
    return title;
}

[[nodiscard]] bool isNonNormalWindowState(const Qt::WindowStates states) noexcept {
    return states.testFlag(Qt::WindowMinimized) || states.testFlag(Qt::WindowMaximized) ||
           states.testFlag(Qt::WindowFullScreen);
}

[[nodiscard]] DefaultAppsSettingsOperations defaultDefaultAppsSettingsOperations() {
#ifdef Q_OS_WIN
    return {
        .availability_reader = [] { return true; },
        .uri_opener = [](const QUrl& uri) { return QDesktopServices::openUrl(uri); },
    };
#else
    return {
        .availability_reader = [] { return false; },
        .uri_opener = {},
    };
#endif
}

} // namespace

QtShellPlatform::QtShellPlatform(QIcon icon, QString app_name, TrayMenuLabels labels,
                                 std::function<bool()> availability_reader, std::function<void()> quit_request,
                                 QObject* parent)
    : QtShellPlatform(std::move(icon), std::move(app_name), std::move(labels), std::move(availability_reader),
                      std::move(quit_request), defaultDefaultAppsSettingsOperations(), parent) {}

QtShellPlatform::QtShellPlatform(QIcon icon, QString app_name, TrayMenuLabels labels,
                                 std::function<bool()> availability_reader, std::function<void()> quit_request,
                                 DefaultAppsSettingsOperations default_apps_settings, QObject* parent)
    : QObject(parent), labels_(std::move(labels)), availability_reader_(std::move(availability_reader)),
      quit_request_(std::move(quit_request)), default_apps_settings_(std::move(default_apps_settings)),
      menu_(std::make_unique<QMenu>()), tray_icon_(new QSystemTrayIcon(std::move(icon), this)),
      availability_timer_(new QTimer(this)), window_placement_timer_(std::make_unique<QTimer>()) {
    if (!availability_reader_) {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            taskbar_events_ = std::make_unique<WindowsTaskbarEvents>();
            connect(taskbar_events_.get(), &WindowsTaskbarEvents::taskbarCreated, this,
                    &QtShellPlatform::handleTrayHostRestart);
            taskbar_monitor_ready_ = taskbar_events_->start();
            availability_reader_ = [this] { return taskbar_monitor_ready_ && WindowsTaskbarEvents::shellAvailable(); };
        } else
#endif
            availability_reader_ = [] { return QSystemTrayIcon::isSystemTrayAvailable(); };
    }

    const auto add_action = [this](const char* name, const QString& text) {
        auto* const added = menu_->addAction(text);
        added->setObjectName(QString::fromLatin1(name));
        return added;
    };
    current_track_ = add_action("tray-current-track", labels_.no_track);
    play_pause_ = add_action("tray-play-pause", labels_.play);
    previous_ = add_action("tray-previous", labels_.previous);
    next_ = add_action("tray-next", labels_.next);
    show_hide_ = add_action("tray-show-hide", labels_.show);
    exit_ = add_action("tray-exit", labels_.exit);

    connect(play_pause_, &QAction::triggered, this, [this] {
        refreshAvailability();
        if (play_pause_->isEnabled())
            emit playPauseRequested();
    });
    connect(previous_, &QAction::triggered, this, [this] {
        refreshAvailability();
        if (previous_->isEnabled())
            emit previousRequested();
    });
    connect(next_, &QAction::triggered, this, [this] {
        refreshAvailability();
        if (next_->isEnabled())
            emit nextRequested();
    });
    connect(current_track_, &QAction::triggered, this, [this] {
        if (window_ && !exit_requested_)
            emit restoreRequested();
    });
    connect(show_hide_, &QAction::triggered, this, [this] {
        if (!window_ || exit_requested_)
            return;
        if (window_->isVisible() && !window_->windowStates().testFlag(Qt::WindowMinimized))
            emit hideRequested();
        else
            emit restoreRequested();
    });
    connect(exit_, &QAction::triggered, this, [this] {
        if (!exit_requested_)
            emit exitRequested();
    });
    connect(tray_icon_, &QSystemTrayIcon::activated, this, [this](const QSystemTrayIcon::ActivationReason reason) {
        if (window_ && !exit_requested_ &&
            (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)) {
            emit restoreRequested();
        }
    });

    availability_timer_->setInterval(1000);
    availability_timer_->setTimerType(Qt::CoarseTimer);
    connect(availability_timer_, &QTimer::timeout, this, &QtShellPlatform::refreshAvailability);
    window_placement_timer_->setInterval(0);
    window_placement_timer_->setSingleShot(true);
    connect(window_placement_timer_.get(), &QTimer::timeout, this, &QtShellPlatform::publishWindowPlacement);
    connect(menu_.get(), &QMenu::aboutToShow, this, &QtShellPlatform::refreshAvailability);
    tray_icon_->setContextMenu(menu_.get());
    tray_icon_->setToolTip(app_name);
    // Keep the visibility intent even if the tray is absent: Qt can register it when support appears.
    // An empty icon cannot create a usable entry and must retain the normal-window fallback.
    tray_icon_->setVisible(!tray_icon_->icon().isNull());
    available_ = trayAvailable();
    updateMenu();
}

QtShellPlatform::~QtShellPlatform() {
#ifdef Q_OS_WIN
    if (taskbar_events_)
        taskbar_events_->shutdown();
#endif
    hidden_to_tray_ = false;
    availability_timer_->stop();
    tray_icon_->hide();
    // QSystemTrayIcon does not own QMenu; detach before the unique_ptr releases its widget.
    tray_icon_->setContextMenu(nullptr);
}

void QtShellPlatform::bindWindow(QWindow& window) {
    ++window_binding_generation_;
    hidden_to_tray_ = false;
    tray_recovery_pending_ = false;
    window_placement_timer_->stop();
    pending_window_placement_.reset();
    if (window_)
        disconnect(window_, nullptr, this, nullptr);
    window_ = &window;
    restore_states_ = Qt::WindowNoState;
    rememberWindowState();
    connect(&window, &QWindow::windowStateChanged, this, [this] {
        if (isNonNormalWindowState(window_->windowStates())) {
            // Windows can report expanded geometry before publishing the new state. Until the coalesced
            // candidate survives a normal-state event-loop turn, the last persisted normal placement is safer.
            window_placement_timer_->stop();
            pending_window_placement_.reset();
        } else {
            const std::uint64_t binding_generation = window_binding_generation_;
            QTimer::singleShot(0, this, [this, binding_generation] {
                if (binding_generation == window_binding_generation_)
                    scheduleWindowPlacementCapture();
            });
        }
        rememberWindowState();
        updateMenu();
        updatePolling();
    });
    connect(&window, &QWindow::visibleChanged, this, [this] {
        if (window_ && window_->isVisible())
            hidden_to_tray_ = false;
        scheduleWindowPlacementCapture();
        updateMenu();
        updatePolling();
    });
    connect(&window, &QWindow::xChanged, this, [this] { scheduleWindowPlacementCapture(); });
    connect(&window, &QWindow::yChanged, this, [this] { scheduleWindowPlacementCapture(); });
    connect(&window, &QWindow::widthChanged, this, [this] { scheduleWindowPlacementCapture(); });
    connect(&window, &QWindow::heightChanged, this, [this] { scheduleWindowPlacementCapture(); });
    connect(&window, &QWindow::screenChanged, this, [this] { scheduleWindowPlacementCapture(); });
    connect(&window, &QObject::destroyed, this, [this] {
        ++window_binding_generation_;
        window_placement_timer_->stop();
        pending_window_placement_.reset();
        hidden_to_tray_ = false;
        window_.clear();
        updateMenu();
        updatePolling();
    });
    updateMenu();
    updatePolling();
}

void QtShellPlatform::flushPendingWindowPlacement() {
    if (!window_ || exit_requested_ || window_placement_flush_in_progress_)
        return;
    const QPointer<QtShellPlatform> guard{this};
    window_placement_flush_in_progress_ = true;
    scheduleWindowPlacementCapture();
    publishWindowPlacement();
    if (guard)
        window_placement_flush_in_progress_ = false;
}

void QtShellPlatform::updateTrayState(const TrayMenuState& state) {
    // Playback snapshots arrive at 5 Hz, but position changes do not change the tray presentation.
    if (state.title == state_.title && state.transport_enabled == state_.transport_enabled &&
        state.playing == state_.playing && state.can_go_previous == state_.can_go_previous &&
        state.can_go_next == state_.can_go_next && state.interaction_enabled == state_.interaction_enabled) {
        return;
    }
    state_ = state;
    updateMenu();
    if (tray_recovery_pending_ && state_.interaction_enabled)
        handleTrayHostRestart();
}

void QtShellPlatform::refreshAvailability() {
    if (exit_requested_)
        return;
#ifdef Q_OS_WIN
    if (taskbar_events_ && !taskbar_monitor_ready_)
        taskbar_monitor_ready_ = taskbar_events_->start();
#endif
    const bool previous = available_;
    available_ = trayAvailable();
    const QPointer<QtShellPlatform> guard{this};
    if (!available_ && hidden_to_tray_ && window_ && !window_->isVisible()) {
        if (state_.interaction_enabled)
            minimizeWindow();
        else
            tray_recovery_pending_ = true;
    }
    if (!guard || exit_requested_)
        return;
    updateMenu();
    updatePolling();
    if (available_ != previous)
        emit availabilityChanged();
}

void QtShellPlatform::handleTrayHostRestart() {
    if (exit_requested_)
        return;
    if (!state_.interaction_enabled) {
        tray_recovery_pending_ = hidden_to_tray_;
        return;
    }
    tray_recovery_pending_ = false;
    const QPointer<QtShellPlatform> guard{this};
    // Qt re-registers the icon itself. Keep a taskbar route to our window even if that add fails.
    // TaskbarCreated can also follow a DPI change, so do not recreate the icon or steal focus.
    if (hidden_to_tray_ && window_ && !window_->isVisible())
        minimizeWindow();
    if (guard)
        refreshAvailability();
}

QMenu* QtShellPlatform::menu() const noexcept { return menu_.get(); }
QSystemTrayIcon* QtShellPlatform::trayIcon() const noexcept { return tray_icon_; }

bool QtShellPlatform::trayAvailable() const noexcept {
    if (exit_requested_ || !tray_icon_->isVisible() || tray_icon_->icon().isNull())
        return false;
    try {
        return availability_reader_();
    } catch (...) {
        // A failed capability read cannot justify making the only window unreachable.
        return false;
    }
}

bool QtShellPlatform::defaultAppsSettingsSupported() const noexcept {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool QtShellPlatform::defaultAppsSettingsAvailable() const noexcept {
    if (!defaultAppsSettingsSupported() || exit_requested_ || !default_apps_settings_.availability_reader)
        return false;
    try {
        return default_apps_settings_.availability_reader();
    } catch (...) {
        return false;
    }
}

void QtShellPlatform::openDefaultAppsSettings(DefaultAppsSettingsCompletion completion) {
    if (!completion)
        return;
    DefaultAppsSettingsLaunchResult result = DefaultAppsSettingsLaunchResult::Unavailable;
    if (defaultAppsSettingsAvailable() && !exit_requested_) {
        if (!default_apps_settings_.uri_opener) {
            result = DefaultAppsSettingsLaunchResult::Failed;
        } else {
            try {
                const QUrl uri{QStringLiteral("ms-settings:defaultapps")};
                result = default_apps_settings_.uri_opener(uri) ? DefaultAppsSettingsLaunchResult::Dispatched
                                                                : DefaultAppsSettingsLaunchResult::Failed;
            } catch (...) {
                result = DefaultAppsSettingsLaunchResult::Failed;
            }
        }
    }
    try {
        completion(result);
    } catch (...) {
        // Completion is an application callback and must not unwind through the platform boundary.
    }
}

void QtShellPlatform::hideWindow() {
    if (!window_ || exit_requested_)
        return;
    const std::uint64_t binding_generation = window_binding_generation_;
    flushPendingWindowPlacement();
    if (!window_ || exit_requested_ || binding_generation != window_binding_generation_)
        return;
    refreshAvailability();
    if (!window_ || exit_requested_ || binding_generation != window_binding_generation_)
        return;
    if (!available_) {
        minimizeWindow();
        if (window_ && !exit_requested_ && window_->isVisible() &&
            window_->windowStates().testFlag(Qt::WindowMinimized)) {
            emit hideFellBackToMinimize();
        }
        return;
    }
    rememberWindowState();
    hidden_to_tray_ = true;
    window_->hide();
    updatePolling();
}

void QtShellPlatform::minimizeWindow() {
    if (!window_ || exit_requested_)
        return;
    const QPointer<QtShellPlatform> guard{this};
    hidden_to_tray_ = false;
    updatePolling();
    const std::uint64_t binding_generation = window_binding_generation_;
    flushPendingWindowPlacement();
    if (!guard || !window_ || exit_requested_ || binding_generation != window_binding_generation_)
        return;
    rememberWindowState();
    window_->setWindowStates(restore_states_ | Qt::WindowMinimized);
    if (!guard || !window_ || exit_requested_ || binding_generation != window_binding_generation_)
        return;
    window_->setVisible(true);
}

void QtShellPlatform::showAndActivateWindow() {
    if (!window_ || exit_requested_)
        return;
    hidden_to_tray_ = false;
    updatePolling();
    rememberWindowState();
    window_->setWindowStates(restore_states_);
    window_->setVisible(true);
    window_->requestActivate();
}

void QtShellPlatform::requestExit() {
    if (exit_requested_)
        return;
    hidden_to_tray_ = false;
    updatePolling();
    flushPendingWindowPlacement();
    if (exit_requested_)
        return;
    exit_requested_ = true;
#ifdef Q_OS_WIN
    if (taskbar_events_)
        taskbar_events_->shutdown();
#endif
    tray_recovery_pending_ = false;
    availability_timer_->stop();
    tray_icon_->hide();
    available_ = false;
    updateMenu();
    if (quit_request_)
        quit_request_();
    else
        QTimer::singleShot(0, this, [this] {
            // QApplication's quit path closes QWidget windows, not our standalone QML QWindow.
            // The presenter has already latched exiting, so Main accepts this queued close.
            if (window_)
                window_->close();
            QCoreApplication::quit();
        });
}

void QtShellPlatform::updateMenu() {
    current_track_->setText(
        menuTitle(state_.title.isEmpty() ? labels_.no_track : state_.title, current_track_->font()));
    const bool interaction_enabled = state_.interaction_enabled && !exit_requested_;
    current_track_->setEnabled(window_ && interaction_enabled);
    play_pause_->setText(state_.playing ? labels_.pause : labels_.play);
    const bool transport_enabled = available_ && state_.transport_enabled && interaction_enabled;
    play_pause_->setEnabled(transport_enabled);
    previous_->setEnabled(transport_enabled && state_.can_go_previous);
    next_->setEnabled(transport_enabled && state_.can_go_next);
    const bool shown = window_ && window_->isVisible() && !window_->windowStates().testFlag(Qt::WindowMinimized);
    show_hide_->setText(shown ? labels_.hide : labels_.show);
    show_hide_->setEnabled(window_ && interaction_enabled);
    exit_->setEnabled(interaction_enabled);
}

void QtShellPlatform::updatePolling() {
    const bool should_poll = hidden_to_tray_ && window_ && !window_->isVisible() && !exit_requested_;
    if (should_poll && !availability_timer_->isActive())
        availability_timer_->start();
    else if (!should_poll)
        availability_timer_->stop();
}

void QtShellPlatform::rememberWindowState() {
    if (!window_)
        return;
    const auto states = window_->windowStates() & ~Qt::WindowStates{Qt::WindowActive};
    if (!states.testFlag(Qt::WindowMinimized) || states.testFlag(Qt::WindowMaximized) ||
        states.testFlag(Qt::WindowFullScreen)) {
        restore_states_ = states & ~Qt::WindowStates{Qt::WindowMinimized};
    }
}

void QtShellPlatform::scheduleWindowPlacementCapture() {
    if (!window_ || exit_requested_)
        return;
    const Qt::WindowStates states = window_->windowStates();
    if (!window_->isVisible() || isNonNormalWindowState(states)) {
        return;
    }
    const QRect geometry = window_->geometry();
    if (geometry.width() <= 0 || geometry.height() <= 0)
        return;
    pending_window_placement_ = {
        .x = geometry.x(), .y = geometry.y(), .width = geometry.width(), .height = geometry.height()};
    scheduled_window_binding_generation_ = window_binding_generation_;
    window_placement_timer_->start();
}

void QtShellPlatform::publishWindowPlacement() {
    window_placement_timer_->stop();
    if (!pending_window_placement_)
        return;
    if (!window_ || exit_requested_ || scheduled_window_binding_generation_ != window_binding_generation_ ||
        !window_->isVisible() || isNonNormalWindowState(window_->windowStates())) {
        pending_window_placement_.reset();
        return;
    }
    const WindowPlacement placement = *pending_window_placement_;
    pending_window_placement_.reset();
    emit windowPlacementChanged(placement);
}

} // namespace music_player::m0
