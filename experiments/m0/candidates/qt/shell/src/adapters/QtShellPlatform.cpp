#include "adapters/QtShellPlatform.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QFontMetricsF>
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

} // namespace

QtShellPlatform::QtShellPlatform(QIcon icon, QString app_name, TrayMenuLabels labels,
                                 std::function<bool()> availability_reader, std::function<void()> quit_request,
                                 QObject* parent)
    : QObject(parent), labels_(std::move(labels)), availability_reader_(std::move(availability_reader)),
      quit_request_(std::move(quit_request)), menu_(std::make_unique<QMenu>()),
      tray_icon_(new QSystemTrayIcon(std::move(icon), this)), availability_timer_(new QTimer(this)) {
    if (!availability_reader_)
        availability_reader_ = [] { return QSystemTrayIcon::isSystemTrayAvailable(); };

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
    availability_timer_->stop();
    tray_icon_->hide();
    // QSystemTrayIcon does not own QMenu; detach before the unique_ptr releases its widget.
    tray_icon_->setContextMenu(nullptr);
}

void QtShellPlatform::bindWindow(QWindow& window) {
    if (window_)
        disconnect(window_, nullptr, this, nullptr);
    window_ = &window;
    restore_states_ = Qt::WindowNoState;
    rememberWindowState();
    connect(&window, &QWindow::windowStateChanged, this, [this] {
        rememberWindowState();
        updateMenu();
        updatePolling();
    });
    connect(&window, &QWindow::visibleChanged, this, [this] {
        updateMenu();
        updatePolling();
    });
    connect(&window, &QObject::destroyed, this, [this] {
        window_.clear();
        updateMenu();
        updatePolling();
    });
    updateMenu();
    updatePolling();
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
}

void QtShellPlatform::refreshAvailability() {
    if (exit_requested_)
        return;
    const bool previous = available_;
    available_ = trayAvailable();
    if (!available_ && window_ && !window_->isVisible())
        minimizeWindow();
    updateMenu();
    updatePolling();
    if (available_ != previous)
        emit availabilityChanged();
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

void QtShellPlatform::hideWindow() {
    if (!window_ || exit_requested_)
        return;
    refreshAvailability();
    if (!window_ || exit_requested_)
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
    window_->hide();
}

void QtShellPlatform::minimizeWindow() {
    if (!window_ || exit_requested_)
        return;
    rememberWindowState();
    window_->setWindowStates(restore_states_ | Qt::WindowMinimized);
    window_->setVisible(true);
}

void QtShellPlatform::showAndActivateWindow() {
    if (!window_ || exit_requested_)
        return;
    rememberWindowState();
    window_->setWindowStates(restore_states_);
    window_->setVisible(true);
    window_->requestActivate();
}

void QtShellPlatform::requestExit() {
    if (exit_requested_)
        return;
    exit_requested_ = true;
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
    const bool should_poll = window_ && !window_->isVisible() && !exit_requested_;
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

} // namespace music_player::m0
