#include "adapters/QtShellPlatform.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellPlatformBinding.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

[[nodiscard]] QIcon testIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::blue);
    return QIcon{pixmap};
}

[[nodiscard]] TrayMenuLabels labels() {
    return {QStringLiteral("Reproduzir"), QStringLiteral("Pausar"),       QStringLiteral("Anterior"),
            QStringLiteral("Próxima"),    QStringLiteral("Mostrar"),      QStringLiteral("Ocultar"),
            QStringLiteral("Sair"),       QStringLiteral("Nenhuma faixa")};
}

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

class BindingPreferencesWriter final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest request) override {
        starts.push_back(request);
        active = std::move(request);
        return PreferencesWriterCommand::Accepted;
    }

    [[nodiscard]] std::optional<PreferencesWriteCompletion> poll() override { return std::nullopt; }

    [[nodiscard]] std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        flushes.push_back(request);
        active.reset();
        return PreferencesWriteCompletion{.revision = request.revision, .status = PreferencesSaveStatus::Saved};
    }

    [[nodiscard]] bool shutdown() override { return true; }

    std::optional<PreferencesWriteRequest> active;
    std::vector<PreferencesWriteRequest> starts;
    std::vector<PreferencesWriteRequest> flushes;
};

class BindingPlaybackBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback callback) override { callback_ = std::move(callback); }
    PlaybackCommandResult load(const std::filesystem::path& path) override {
        loaded_paths.push_back(path);
        snapshot_ = {.status = PlaybackStatus::Loading, .generation = snapshot_.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override {
        ++play_calls;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult pause() override {
        ++pause_calls;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult seekTo(std::int64_t) override {
        ++seek_calls;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override {
        ++unload_calls;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override {
        ++shutdown_calls;
        return PlaybackCommandResult::Accepted;
    }
    void publish(PlaybackSnapshot snapshot) {
        snapshot_ = snapshot;
        if (callback_)
            callback_({PlaybackEventType::SnapshotChanged, snapshot_});
    }

    PlaybackSnapshot snapshot_{};
    PlaybackEventCallback callback_{};
    std::vector<std::filesystem::path> loaded_paths;
    int play_calls{0};
    int pause_calls{0};
    int seek_calls{0};
    int unload_calls{0};
    int shutdown_calls{0};
};

class BindingMetadataReader final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest) override { return MetadataCommandResult::Accepted; }
    MetadataCommandResult cancelRead(MetadataGeneration) override { return MetadataCommandResult::Accepted; }
    [[nodiscard]] MetadataPollResult poll(MetadataGeneration) override {
        return {.status = MetadataPollStatus::Pending};
    }
    [[nodiscard]] std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
};

[[nodiscard]] QVariantMap playbackAndPreferences(const PlayerPresenter& presenter) {
    QVariantMap result;
    for (const char* name :
         {"stateId",         "playbackState",       "currentOccurrenceId", "currentQueueIndex", "contextOccurrenceId",
          "positionMs",      "confirmedPositionMs", "seekPreviewActive",   "seekPending",       "durationMs",
          "seekable",        "currentTitle",        "currentArtist",       "currentAlbum",      "volume",
          "muted",           "skipBackwardSeconds", "skipForwardSeconds",  "themePreference",   "motionPreference",
          "closePreference", "queueEndPreference",  "restoreLastSession",  "startMinimized"}) {
        result.insert(QString::fromLatin1(name), presenter.property(name));
    }
    return result;
}

[[nodiscard]] QStringList occurrenceIds(const QueueListModel& model) {
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row)
        ids.append(model.data(model.index(row, 0), QueueListModel::OccurrenceIdRole).toString());
    return ids;
}

} // namespace

class ShellPlatformBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void aLateHideFallbackAlwaysPublishesTheExplanation_data() {
        QTest::addColumn<bool>("cached_available");
        QTest::addColumn<bool>("confirm_first_notice");
        QTest::newRow("cached-available") << true << false;
        QTest::newRow("cached-unavailable") << false << false;
        QTest::newRow("confirm-first-notice") << true << true;
    }

    void aLateHideFallbackAlwaysPublishesTheExplanation() {
        QFETCH(bool, cached_available);
        QFETCH(bool, confirm_first_notice);
        bool armed = false;
        int action_reads = 0;
        int quit_requests = 0;
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player M0"), labels(),
            [&] { return armed ? ++action_reads <= 2 : cached_available; }, [&] { ++quit_requests; });
        PlayerPresenter presenter(frozenBundle(), platform);
        QWindow window;
        window.resize(960, 640);
        window.showMaximized();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.loadFrozenQueue());
        QVERIFY(presenter.setVolume(0.35));
        QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.setSkipBackwardSeconds(7));
        QVERIFY(presenter.setSkipForwardSeconds(20));
        QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
        QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
        QVERIFY(presenter.setRestoreLastSession(false));
        QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
        if (confirm_first_notice) {
            QVERIFY(presenter.requestClose());
            QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
        } else {
            QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
        }
        QVERIFY(presenter.beginSeekPreview(124'000));
        QVERIFY(presenter.commitSeekPreview());
        QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
        QVERIFY(timer != nullptr && timer->isActive());
        QueueListModel* const model = presenter.queueModel();
        const QPersistentModelIndex current(model->index(16, 0));
        const QStringList ids = occurrenceIds(*model);
        QCOMPARE(ids.size(), 1000);
        const QVariantMap before = playbackAndPreferences(presenter);
        for (const auto& value : before)
            QVERIFY(value.isValid());
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
        QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
        QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
        QSignalSpy availability(&platform, &QtShellPlatform::availabilityChanged);
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);
        QList<PlayerPresenter::WindowNotice> notices;
        connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter,
                [&] { notices.append(presenter.windowNotice()); });
        connect(&platform, &QtShellPlatform::hideFellBackToMinimize, &presenter, [&] {
            QVERIFY(window.isVisible());
            QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
            QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
            QVERIFY(!presenter.trayAvailable());
        });

        // Only this action sees the race: presenter/core read true, then the adapter reads false.
        // All following reads remain false, including the binding's availability reconciliation.
        armed = true;
        if (confirm_first_notice)
            QVERIFY(presenter.confirmWindowNotice());
        else
            QVERIFY(presenter.requestHide());

        QVERIFY(action_reads >= 3);
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(playbackAndPreferences(presenter), before);
        QCOMPARE(presenter.queueModel(), model);
        QCOMPARE(occurrenceIds(*model), ids);
        QVERIFY(current.isValid());
        QCOMPARE(current.data(QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0017"));
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 0);
        QCOMPARE(playback.size(), 0);
        QCOMPARE(preferences.size(), 0);
        QCOMPARE(quit_requests, 0);
        QVERIFY(timer->isActive());
        QCOMPARE(availability.size(), cached_available ? 1 : 0);
        QCOMPARE(fallbacks.size(), 1);
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
        QVERIFY(!presenter.trayAvailable());
        QCOMPARE(notices, QList<PlayerPresenter::WindowNotice>{PlayerPresenter::WindowNotice::TrayUnavailableNotice});

        QVERIFY(presenter.requestRestore());
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        QVERIFY(!window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
        QCOMPARE(playbackAndPreferences(presenter), before);
        QVERIFY(presenter.confirmWindowNotice());
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(presenter.advanceOneTick());
        QVERIFY(!presenter.seekPending());
        QVERIFY(!presenter.seekPreviewActive());
        QCOMPARE(presenter.confirmedPositionMs(), 124'000);
        QCOMPARE(presenter.queueModel(), model);
        QCOMPARE(quit_requests, 0);
    }

    void exitingDuringCapabilityRefreshPreventsTheFallback() {
        bool available = true;
        int quit_requests = 0;
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player M0"), labels(), [&] { return available; }, [&] { ++quit_requests; });
        PlayerPresenter presenter(frozenBundle(), platform);
        QWindow window;
        window.show();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.loadFrozenQueue());
        const QVariantMap before = playbackAndPreferences(presenter);
        QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
        QVERIFY(timer != nullptr && timer->isActive());
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);
        connect(&platform, &QtShellPlatform::availabilityChanged, &presenter,
                [&] { QVERIFY(presenter.requestExit()); });

        available = false;
        platform.hideWindow();

        QCOMPARE(quit_requests, 1);
        QVERIFY(presenter.exiting());
        QVERIFY(!timer->isActive());
        QVERIFY(window.isVisible());
        QVERIFY(!window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QCOMPARE(playbackAndPreferences(presenter), before);
        QCOMPARE(fallbacks.size(), 0);
        platform.hideWindow();
        platform.refreshAvailability();
        QVERIFY(!presenter.requestHide());
        QVERIFY(!presenter.requestExit());
        QCOMPARE(quit_requests, 1);
        QCOMPARE(fallbacks.size(), 0);
    }

    void trayHostRestartRecoversTheHiddenWindowWithoutChangingLocalPlayback_data() {
        QTest::addColumn<bool>("playing");
        QTest::newRow("playing") << true;
        QTest::newRow("paused") << false;
    }

    void trayHostRestartRecoversTheHiddenWindowWithoutChangingLocalPlayback() {
        QFETCH(bool, playing);
        int quit_requests = 0;
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [&] { ++quit_requests; });
        BindingPlaybackBackend backend;
        BindingMetadataReader metadata;
        const std::vector<std::filesystem::path> paths{L"C:/m0/faixa Unicode.wav", L"C:/m0/outra.wav",
                                                       L"C:/m0/faixa Unicode.wav"};
        PlayerPresenter presenter(platform, backend, metadata, [&] { return paths; });
        QWindow window;
        window.resize(960, 640);
        window.showMaximized();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.openAudio());
        backend.publish({.status = playing ? PlaybackStatus::Playing : PlaybackStatus::Paused,
                         .position_ms = 84'000,
                         .duration_ms = 180'000,
                         .seekable = true,
                         .generation = backend.snapshot().generation});
        QTRY_COMPARE(presenter.playbackState(),
                     playing ? PlayerPresenter::PlaybackState::Playing : PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(presenter.confirmedPositionMs(), 84'000);
        QVERIFY(presenter.setStartMinimized(true));
        QVERIFY(presenter.requestHide());
        QVERIFY(!window.isVisible());

        const PlaybackSnapshot before = backend.snapshot();
        const QVariantMap presentation_before = playbackAndPreferences(presenter);
        auto* const model = presenter.queueModelObject();
        QCOMPARE(model->rowCount(), 3);
        QStringList ids;
        for (int row = 0; row < model->rowCount(); ++row)
            ids.append(model->data(model->index(row, 0), QueueListModel::OccurrenceIdRole).toString());
        const QPersistentModelIndex current(model->index(0, 0));
        const auto loads_before = backend.loaded_paths;
        const int plays_before = backend.play_calls;
        const int pauses_before = backend.pause_calls;
        const int seeks_before = backend.seek_calls;
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
        QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
        QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);

        platform.handleTrayHostRestart();

        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(backend.snapshot(), before);
        QCOMPARE(playbackAndPreferences(presenter), presentation_before);
        QCOMPARE(presenter.queueModelObject(), model);
        QVERIFY(current.isValid());
        QCOMPARE(model->rowCount(), 3);
        for (int row = 0; row < model->rowCount(); ++row)
            QCOMPARE(model->data(model->index(row, 0), QueueListModel::OccurrenceIdRole).toString(), ids.at(row));
        QCOMPARE(backend.loaded_paths, loads_before);
        QCOMPARE(backend.play_calls, plays_before);
        QCOMPARE(backend.pause_calls, pauses_before);
        QCOMPARE(backend.seek_calls, seeks_before);
        QCOMPARE(backend.unload_calls, 0);
        QCOMPARE(backend.shutdown_calls, 0);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 0);
        QCOMPARE(playback.size(), 0);
        QCOMPARE(preferences.size(), 0);
        QCOMPARE(quit_requests, 0);

        // Returning shell support does not reapply the stored startup preference.
        platform.refreshAvailability();
        platform.handleTrayHostRestart();
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        QVERIFY(presenter.requestRestore());
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        QVERIFY(!window.windowStates().testFlag(Qt::WindowMinimized));
        platform.handleTrayHostRestart();
        QVERIFY(window.isVisible());
        QVERIFY(!window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(playbackAndPreferences(presenter), presentation_before);
        QCOMPARE(backend.snapshot(), before);
        QCOMPARE(backend.play_calls, plays_before);
        QCOMPARE(backend.pause_calls, pauses_before);
    }

    void trayHostRestartDoesNotReplayStartupVisibility_data() {
        QTest::addColumn<int>("initial_visibility");
        QTest::newRow("visible") << 0;
        QTest::newRow("minimized") << 1;
        QTest::newRow("hidden-bootstrap") << 2;
    }

    void trayHostRestartDoesNotReplayStartupVisibility() {
        QFETCH(int, initial_visibility);
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [] {});
        PlayerPresenter presenter(frozenBundle(), platform);
        QWindow window;
        if (initial_visibility == 0)
            window.show();
        else if (initial_visibility == 1)
            window.showMinimized();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.setStartMinimized(true));
        const bool visible_before = window.isVisible();
        const Qt::WindowStates states_before = window.windowStates();
        const QVariantMap before = playbackAndPreferences(presenter);
        QSignalSpy visibility(&window, &QWindow::visibleChanged);
        QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);

        platform.handleTrayHostRestart();
        platform.refreshAvailability();

        QCOMPARE(window.isVisible(), visible_before);
        QCOMPARE(window.windowStates(), states_before);
        QCOMPARE(playbackAndPreferences(presenter), before);
        QCOMPARE(visibility.size(), 0);
        QCOMPARE(lifecycle.size(), 0);
    }

    void trayHostRestartRespectsPendingExitAndRemainsReachableAfterCancellation() {
        int quit_requests = 0;
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [&] { ++quit_requests; });
        BindingPreferencesWriter writer;
        const Preferences initial{};
        PreferencesAutosave autosave{writer, initial};
        auto now = PlayerPresenterPreferences::TimePoint{};
        PlayerPresenter presenter(frozenBundle(), platform,
                                  PlayerPresenterPreferences{.initial = initial,
                                                             .load_status = PreferencesLoadStatus::Missing,
                                                             .autosave = &autosave,
                                                             .now = [&] { return now; }});
        QWindow window;
        window.showMaximized();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.loadFrozenQueue());
        QVERIFY(presenter.setVolume(0.35));
        QVERIFY(presenter.requestHide());
        QVERIFY(!window.isVisible());
        const QVariantMap before = playbackAndPreferences(presenter);
        QVERIFY(presenter.requestExit());
        QVERIFY(presenter.exiting());
        QVERIFY(!presenter.exitCommitted());
        QVERIFY(writer.active.has_value());
        QSignalSpy visibility(&window, &QWindow::visibleChanged);

        platform.handleTrayHostRestart();
        platform.refreshAvailability();

        QVERIFY(!window.isVisible());
        QCOMPARE(visibility.size(), 0);
        QCOMPARE(quit_requests, 0);
        QCOMPARE(playbackAndPreferences(presenter), before);

        now += std::chrono::seconds{2};
        PlayerPresenterTestAccess::pollPersistence(presenter);

        QVERIFY(!presenter.exiting());
        QVERIFY(!presenter.exitCommitted());
        QVERIFY(window.isVisible());
        QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);
        QCOMPARE(quit_requests, 0);
        QCOMPARE(playbackAndPreferences(presenter), before);
        platform.handleTrayHostRestart();
        QVERIFY(window.isVisible());
    }

    // S10.1: while a guest follows a listening room, the tray's Play/Pause, Previous and Next never drive the
    // local player; they go to the room, and only what the room allows is enabled.
    void aRoomGuestsTrayGoesToTheRoomAndNeverToTheLocalPlayer() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [] {});
        PlayerPresenter presenter(frozenBundle(), platform);
        QWindow window;
        window.resize(640, 480);
        window.show();
        bool following = true;
        std::vector<SystemMediaCommandKind> allowed;
        std::vector<SystemMediaCommand> sent;
        const auto refresh_tray =
            bindShellPlatform(presenter, platform, window,
                              SystemMediaRoomRoute{
                                  .following = [&] { return following; },
                                  .allows =
                                      [&](const SystemMediaCommandKind kind) {
                                          return std::find(allowed.begin(), allowed.end(), kind) != allowed.end();
                                      },
                                  .send = [&](const SystemMediaCommand& command) { sent.push_back(command); },
                              });
        QVERIFY(presenter.loadFrozenQueue());
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        const auto occurrence = presenter.currentOccurrenceId();
        const auto action = [&](const char* name) {
            return platform.menu()->findChild<QAction*>(QString::fromLatin1(name));
        };
        QAction* const play_pause = action("tray-play-pause");
        QAction* const previous = action("tray-previous");
        QAction* const next = action("tray-next");
        QVERIFY(play_pause && previous && next);

        // Nothing allowed: the three are off, and even a stray request never reaches the local player.
        QVERIFY(!play_pause->isEnabled() && !previous->isEnabled() && !next->isEnabled());
        emit platform.playPauseRequested();
        emit platform.previousRequested();
        emit platform.nextRequested();
        QVERIFY(sent.empty());
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QCOMPARE(presenter.currentOccurrenceId(), occurrence);

        // Skip only: Previous and Next turn on and become room commands; Play/Pause stays off.
        allowed = {SystemMediaCommandKind::Previous, SystemMediaCommandKind::Next};
        refresh_tray();
        QVERIFY(!play_pause->isEnabled());
        QVERIFY(previous->isEnabled() && next->isEnabled());
        next->trigger();
        previous->trigger();
        // Pause while the room plays: the tray says "Pausar" and asks the room.
        allowed.push_back(SystemMediaCommandKind::Pause);
        refresh_tray();
        QVERIFY(play_pause->isEnabled());
        QCOMPARE(play_pause->text(), QStringLiteral("Pausar"));
        play_pause->trigger();
        // Play when the room is paused (the room allows Play instead of Pause).
        allowed = {SystemMediaCommandKind::Play};
        refresh_tray();
        QVERIFY(play_pause->isEnabled());
        emit platform.playPauseRequested();
        QCOMPARE(sent, (std::vector<SystemMediaCommand>{{SystemMediaCommandKind::Next, 0},
                                                        {SystemMediaCommandKind::Previous, 0},
                                                        {SystemMediaCommandKind::Pause, 0},
                                                        {SystemMediaCommandKind::Play, 0}}));
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QCOMPARE(presenter.currentOccurrenceId(), occurrence);

        // Out of the room, the tray drives the local player again.
        following = false;
        refresh_tray();
        QVERIFY(play_pause->isEnabled());
        play_pause->trigger();
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(sent.size(), std::size_t{4});
    }

    void destroyingThePresenterDisconnectsFallbackNotifications() {
        bool available = true;
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [&] { return available; }, [] {});
        QWindow window;
        window.show();
        auto presenter = std::make_unique<PlayerPresenter>(frozenBundle(), platform);
        QPointer<PlayerPresenter> guarded_presenter(presenter.get());
        bindShellPlatform(*presenter, platform, window);
        QVERIFY(presenter->loadFrozenQueue());
        presenter.reset();
        QVERIFY(guarded_presenter.isNull());
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);

        available = false;
        platform.hideWindow();

        QCOMPARE(fallbacks.size(), 1);
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
    }

    void destroyingTheWindowDisarmsTheFallback() {
        bool available = true;
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [&] { return available; }, [] {});
        PlayerPresenter presenter(frozenBundle(), platform);
        auto window = std::make_unique<QWindow>();
        window->show();
        bindShellPlatform(presenter, platform, *window);
        window.reset();
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);
        QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);

        available = false;
        platform.refreshAvailability();
        platform.hideWindow();

        QCOMPARE(fallbacks.size(), 0);
        QCOMPARE(lifecycle.size(), 0);
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(!presenter.trayAvailable());
    }

    void normalPlacementSchedulesPersistenceWithoutPublishingUiPreferences() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [] {});
        BindingPreferencesWriter writer;
        const Preferences initial{};
        PreferencesAutosave autosave{writer, initial};
        PlayerPresenter presenter(frozenBundle(), platform,
                                  PlayerPresenterPreferences{.initial = initial,
                                                             .load_status = PreferencesLoadStatus::Missing,
                                                             .autosave = &autosave});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        bindShellPlatform(presenter, platform, window);
        QSignalSpy preferences_changed(&presenter, &PlayerPresenter::preferencesChanged);
        constexpr WindowPlacement expected{31, 37, 760, 560};

        window.setGeometry(expected.x, expected.y, expected.width, expected.height);

        QTRY_COMPARE(autosave.latestRevision(), PreferencesRevision{1});
        QVERIFY(presenter.preferences().window_placement.has_value());
        QCOMPARE(*presenter.preferences().window_placement, expected);
        QVERIFY(autosave.dirty());
        QVERIFY(writer.starts.empty());
        QCOMPARE(preferences_changed.size(), 0);
    }

    void exitFlushesTheQueuedPlacementBeforeStartingPreferencePersistence() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [] {});
        BindingPreferencesWriter writer;
        const Preferences initial{};
        PreferencesAutosave autosave{writer, initial};
        PlayerPresenter presenter(frozenBundle(), platform,
                                  PlayerPresenterPreferences{.initial = initial,
                                                             .load_status = PreferencesLoadStatus::Missing,
                                                             .autosave = &autosave});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        bindShellPlatform(presenter, platform, window);
        QSignalSpy preferences_changed(&presenter, &PlayerPresenter::preferencesChanged);
        QSignalSpy flush_requested(&presenter, &PlayerPresenter::windowPlacementFlushRequested);
        constexpr WindowPlacement pending{41, 43, 750, 550};

        window.setGeometry(pending.x, pending.y, pending.width, pending.height);
        QVERIFY(presenter.requestExit());

        QCOMPARE(flush_requested.size(), 1);
        QCOMPARE(preferences_changed.size(), 0);
        QCOMPARE(writer.starts.size(), std::size_t{1});
        QVERIFY(writer.starts.back().preferences.window_placement.has_value());
        QCOMPARE(*writer.starts.back().preferences.window_placement, pending);
        QVERIFY(presenter.exiting());
        QVERIFY(!presenter.exitCommitted());
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::ShellPlatformBindingTests)
#include "ShellPlatformBindingTests.moc"
