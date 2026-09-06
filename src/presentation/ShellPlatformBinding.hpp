#pragma once

#include "adapters/QtShellPlatform.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QPointer>
#include <QWindow>

namespace music_player::m0 {

inline void bindShellPlatform(PlayerPresenter& presenter, QtShellPlatform& platform, QWindow& window) {
    platform.bindWindow(window);
    QObject::connect(&platform, &QtShellPlatform::windowPlacementChanged, &presenter,
                     &PlayerPresenter::recordWindowPlacement);
    QObject::connect(&presenter, &PlayerPresenter::windowPlacementFlushRequested, &platform,
                     &QtShellPlatform::flushPendingWindowPlacement, Qt::DirectConnection);
    QObject::connect(&platform, &QtShellPlatform::playPauseRequested, &presenter, &PlayerPresenter::togglePlayback);
    QObject::connect(&platform, &QtShellPlatform::previousRequested, &presenter, &PlayerPresenter::previousTrack);
    QObject::connect(&platform, &QtShellPlatform::nextRequested, &presenter, &PlayerPresenter::nextTrack);
    QObject::connect(&platform, &QtShellPlatform::hideRequested, &presenter, &PlayerPresenter::requestHide);
    QObject::connect(&platform, &QtShellPlatform::restoreRequested, &presenter, &PlayerPresenter::requestRestore);
    QObject::connect(&platform, &QtShellPlatform::exitRequested, &presenter, &PlayerPresenter::requestExit);
    QObject::connect(&platform, &QtShellPlatform::hideFellBackToMinimize, &presenter, [&presenter] {
        presenter.refreshPlatformAvailability();
        presenter.reportTrayUnavailable();
    });

    const auto refresh_tray = [&presenter, &platform] {
        const auto state = presenter.playbackState();
        const bool transport_enabled = state == PlayerPresenter::PlaybackState::Ready ||
                                       state == PlayerPresenter::PlaybackState::Paused ||
                                       state == PlayerPresenter::PlaybackState::Playing;
        const QString title = presenter.hasCurrentTrack()   ? presenter.currentTitle()
                              : presenter.hasContextTrack() ? presenter.contextTitle()
                                                            : QString{};
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
}

} // namespace music_player::m0
