#pragma once

#include "adapters/QtShellPlatform.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QMetaObject>
#include <QWindow>

namespace music_player::m0::experiment {

// Built only with the executable's test hooks. No real tray, input injection, audio or network is used.
[[nodiscard]] inline bool runShellLifecycleSmoke(QWindow& window, PlayerPresenter& presenter,
                                                 PlatformIntegration& integration, bool& tray_available) {
    const auto fail = [](const char* detail) {
        qCritical() << "Tray lifecycle smoke:" << detail;
        return false;
    };
    auto* const platform = dynamic_cast<QtShellPlatform*>(&integration);
    if (platform == nullptr || QApplication::quitOnLastWindowClosed() || !window.isVisible())
        return fail("the real platform adapter or explicit-quit policy is missing");
    auto* const menu = platform->menu();
    auto* const play_pause = menu->findChild<QAction*>(QStringLiteral("tray-play-pause"));
    auto* const previous = menu->findChild<QAction*>(QStringLiteral("tray-previous"));
    auto* const next = menu->findChild<QAction*>(QStringLiteral("tray-next"));
    auto* const show_hide = menu->findChild<QAction*>(QStringLiteral("tray-show-hide"));
    auto* const current_track = menu->findChild<QAction*>(QStringLiteral("tray-current-track"));
    auto* const exit = menu->findChild<QAction*>(QStringLiteral("tray-exit"));
    if (play_pause == nullptr || previous == nullptr || next == nullptr || show_hide == nullptr ||
        current_track == nullptr || exit == nullptr || play_pause->isEnabled() || previous->isEnabled() ||
        next->isEnabled() || !presenter.loadFrozenQueue()) {
        return fail("the initial menu or frozen queue could not be loaded");
    }
    play_pause->trigger();
    auto* const model = presenter.queueModelObject();
    if (presenter.playbackState() != PlayerPresenter::PlaybackState::Paused || model == nullptr ||
        model->rowCount() != 1000 || !previous->isEnabled() || !next->isEnabled()) {
        return fail("the tray pause action was not routed to the loaded presenter");
    }
    const auto position = presenter.positionMs();
    const auto occurrence = presenter.currentOccurrenceId();
    static_cast<void>(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
    window.setWindowStates(Qt::WindowMaximized);
    if (window.close() || !window.isVisible() ||
        presenter.windowNotice() != PlayerPresenter::WindowNotice::TrayCloseNotice) {
        return fail("close did not keep the visible window behind its first-close notice");
    }
    QCoreApplication::processEvents();
    auto* const notice = window.findChild<QObject*>(QStringLiteral("window-notice"));
    auto* const confirm = window.findChild<QObject*>(QStringLiteral("window-notice-confirm"));
    if (notice == nullptr || !notice->property("opened").toBool() || confirm == nullptr ||
        !QMetaObject::invokeMethod(confirm, "clicked", Qt::DirectConnection)) {
        return fail("the first-close notice could not be confirmed through QML");
    }
    QCoreApplication::processEvents();
    if (window.isVisible() || presenter.windowNotice() != PlayerPresenter::WindowNotice::NoNotice ||
        presenter.queueModelObject() != model || presenter.positionMs() != position || presenter.exiting()) {
        return fail("confirm did not hide while preserving the paused session");
    }
    show_hide->trigger();
    QCoreApplication::processEvents();
    if (!window.isVisible() || !window.windowStates().testFlag(Qt::WindowMaximized) ||
        window.windowStates().testFlag(Qt::WindowMinimized)) {
        return fail("the menu did not restore the maximized window");
    }
    next->trigger();
    if (presenter.currentOccurrenceId() == occurrence ||
        presenter.playbackState() != PlayerPresenter::PlaybackState::Paused) {
        return fail("next did not navigate while preserving pause");
    }
    previous->trigger();
    if (presenter.currentOccurrenceId() != occurrence || presenter.queueModelObject() != model)
        return fail("previous did not restore the occurrence without replacing the model");

    static_cast<void>(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    show_hide->trigger();
    if (window.isVisible() || presenter.exiting())
        return fail("explicit hide did not ignore the close-to-exit preference");
    // A capability loss while hidden must make the window reachable without waiting for a balloon.
    tray_available = false;
    platform->refreshAvailability();
    QCoreApplication::processEvents();
    if (!window.isVisible() || !window.windowStates().testFlag(Qt::WindowMinimized) ||
        presenter.windowNotice() != PlayerPresenter::WindowNotice::TrayUnavailableNotice) {
        return fail("observed tray loss did not minimize and expose the fallback notice");
    }
    tray_available = true;
    platform->refreshAvailability();
    current_track->trigger();
    QCoreApplication::processEvents();
    if (!window.isVisible() || !window.windowStates().testFlag(Qt::WindowMaximized) ||
        !QMetaObject::invokeMethod(confirm, "clicked", Qt::DirectConnection) ||
        presenter.windowNotice() != PlayerPresenter::WindowNotice::NoNotice) {
        return fail("the track item did not restore the window or dismiss the fallback notice");
    }
    static_cast<void>(presenter.setClosePreference(PlayerPresenter::ClosePreference::MinimizeToTray));
    exit->trigger();
    // Success does not call exit(): the queued, production quit path must close the real QML window.
    const bool exiting = presenter.exiting() && !presenter.requestExit() &&
                         presenter.windowNotice() == PlayerPresenter::WindowNotice::NoNotice &&
                         presenter.queueModelObject() == model &&
                         presenter.playbackState() == PlayerPresenter::PlaybackState::Paused && !exit->isEnabled();
    return exiting || fail("exit was not routed once with a preserved session and disabled menu");
}

} // namespace music_player::m0::experiment
