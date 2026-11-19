#pragma once

#include "adapters/QtShellPlatform.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/SystemMediaControlsBinding.hpp"

#include <QPointer>
#include <QWindow>

#include <functional>
#include <memory>
#include <utility>

namespace music_player::m0 {

inline std::function<void()> bindShellPlatform(PlayerPresenter& presenter, QtShellPlatform& platform, QWindow& window,
                                               SystemMediaRoomRoute room = {}) {
    // While a guest follows a listening room (S10.1), the tray's transport goes to the room, never to the
    // local player, and is enabled by what the room allows.
    const auto route = std::make_shared<const SystemMediaRoomRoute>(std::move(room));
    const auto following = [route] { return route->following && route->following(); };
    const auto allows = [route](const SystemMediaCommandKind kind) { return route->allows && route->allows(kind); };
    const auto to_room = [route, allows](const SystemMediaCommandKind kind) {
        if (route->send && allows(kind))
            route->send(SystemMediaCommand{kind, 0});
    };
    platform.bindWindow(window);
    QObject::connect(&platform, &QtShellPlatform::windowPlacementChanged, &presenter,
                     &PlayerPresenter::recordWindowPlacement);
    QObject::connect(&presenter, &PlayerPresenter::windowPlacementFlushRequested, &platform,
                     &QtShellPlatform::flushPendingWindowPlacement, Qt::DirectConnection);
    QObject::connect(&platform, &QtShellPlatform::playPauseRequested, &presenter,
                     [&presenter, following, allows, to_room] {
                         if (!following())
                             static_cast<void>(presenter.togglePlayback());
                         else
                             to_room(allows(SystemMediaCommandKind::Pause) ? SystemMediaCommandKind::Pause
                                                                           : SystemMediaCommandKind::Play);
                     });
    QObject::connect(&platform, &QtShellPlatform::previousRequested, &presenter, [&presenter, following, to_room] {
        if (!following())
            static_cast<void>(presenter.previousTrack());
        else
            to_room(SystemMediaCommandKind::Previous);
    });
    QObject::connect(&platform, &QtShellPlatform::nextRequested, &presenter, [&presenter, following, to_room] {
        if (!following())
            static_cast<void>(presenter.nextTrack());
        else
            to_room(SystemMediaCommandKind::Next);
    });
    QObject::connect(&platform, &QtShellPlatform::hideRequested, &presenter, &PlayerPresenter::requestHide);
    QObject::connect(&platform, &QtShellPlatform::restoreRequested, &presenter, &PlayerPresenter::requestRestore);
    QObject::connect(&platform, &QtShellPlatform::exitRequested, &presenter, &PlayerPresenter::requestExit);
    QObject::connect(&platform, &QtShellPlatform::hideFellBackToMinimize, &presenter, [&presenter] {
        presenter.refreshPlatformAvailability();
        presenter.reportTrayUnavailable();
    });

    const auto refresh_tray = [&presenter, &platform, following, allows] {
        const auto state = presenter.playbackState();
        const QString title = presenter.hasCurrentTrack()   ? presenter.currentTitle()
                              : presenter.hasContextTrack() ? presenter.contextTitle()
                                                            : QString{};
        if (following()) {
            const bool pause = allows(SystemMediaCommandKind::Pause);
            const bool play_pause = pause || allows(SystemMediaCommandKind::Play);
            const bool previous = allows(SystemMediaCommandKind::Previous);
            const bool next = allows(SystemMediaCommandKind::Next);
            // The label follows the room: "Pausar" while it plays, even when pausing is not allowed.
            const bool playing =
                pause || (!allows(SystemMediaCommandKind::Play) && state == PlayerPresenter::PlaybackState::Playing);
            platform.updateTrayState(
                {title, play_pause || previous || next, playing, previous, next, !presenter.exiting(), play_pause});
            return;
        }
        const bool transport_enabled = state == PlayerPresenter::PlaybackState::Ready ||
                                       state == PlayerPresenter::PlaybackState::Paused ||
                                       state == PlayerPresenter::PlaybackState::Playing;
        platform.updateTrayState({title, transport_enabled, state == PlayerPresenter::PlaybackState::Playing,
                                  presenter.canGoPrevious(), presenter.canGoNext(), !presenter.exiting()});
    };
    QObject::connect(&presenter, &PlayerPresenter::stateChanged, &platform, refresh_tray);
    QObject::connect(&presenter, &PlayerPresenter::playbackChanged, &platform, refresh_tray);
    QObject::connect(&presenter, &PlayerPresenter::lifecycleChanged, &platform, refresh_tray);
    QObject::connect(&platform, &QtShellPlatform::availabilityChanged, &presenter,
                     [&presenter, guarded_window = QPointer<QWindow>(&window)] {
                         presenter.refreshPlatformAvailability();
                         if (!presenter.trayAvailable() && guarded_window &&
                             guarded_window->windowStates().testFlag(Qt::WindowMinimized)) {
                             presenter.reportTrayUnavailable();
                         }
                     });
    platform.refreshAvailability();
    presenter.refreshPlatformAvailability();
    refresh_tray();
    return refresh_tray;
}

} // namespace music_player::m0
