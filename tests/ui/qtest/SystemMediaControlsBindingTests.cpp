#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/ports/PlatformIntegration.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/SystemMediaControlsBinding.hpp"

#include <QTest>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

class TestPlatform final : public PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exit_requests; }

    int exit_requests{0};
};

class FakeSystemMediaControls final : public SystemMediaControls {
  public:
    [[nodiscard]] bool available() const noexcept override { return true; }
    void setCommandHandler(SystemMediaCommandHandler handler) override { handler_ = std::move(handler); }
    [[nodiscard]] bool publish(const SystemMediaSnapshot& snapshot) noexcept override {
        snapshots.push_back(snapshot);
        return publish_succeeds;
    }
    void shutdown() noexcept override {
        ++shutdown_requests;
        handler_ = {};
    }

    void send(SystemMediaCommand command) const {
        if (handler_)
            handler_(command);
    }

    SystemMediaCommandHandler handler_;
    std::vector<SystemMediaSnapshot> snapshots;
    bool publish_succeeds{true};
    int shutdown_requests{0};
};

[[nodiscard]] SystemMediaSnapshot playingSnapshot(const PlayerPresenter& presenter) {
    return {presenter.currentTitle().toStdU16String(),
            presenter.currentArtist().toStdU16String(),
            presenter.currentAlbum().toStdU16String(),
            SystemMediaPlaybackStatus::Playing,
            false,
            true,
            presenter.canGoPrevious(),
            presenter.canGoNext(),
            presenter.seekable(),
            presenter.confirmedPositionMs(),
            presenter.durationMs()};
}

} // namespace

class SystemMediaControlsBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void projectsStateMetadataCapabilitiesAndClearsStaleContent() {
        TestPlatform platform;
        PlayerPresenter presenter(frozenBundle(), platform);
        FakeSystemMediaControls controls;
        SystemMediaControlsBinding binding(presenter, controls);

        QCOMPARE(controls.snapshots, std::vector<SystemMediaSnapshot>{SystemMediaSnapshot{}});
        binding.refresh();
        QCOMPARE(controls.snapshots.size(), std::size_t{1});

        QVERIFY(presenter.loadFrozenQueue());
        QCOMPARE(controls.snapshots.size(), std::size_t{2});
        QCOMPARE(controls.snapshots.back(), playingSnapshot(presenter));
        binding.refresh();
        QCOMPARE(controls.snapshots.size(), std::size_t{2});

        QVERIFY(experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter));
        QCOMPARE(controls.snapshots.back(), SystemMediaSnapshot{});
        QCOMPARE(controls.snapshots.size(), std::size_t{3});
    }

    void routesDistinctCommandsThroughCommonIdempotentUseCases() {
        TestPlatform platform;
        PlayerPresenter presenter(frozenBundle(), platform);
        FakeSystemMediaControls controls;
        SystemMediaControlsBinding binding(presenter, controls);
        QVERIFY(presenter.loadFrozenQueue());
        const auto initial_occurrence = presenter.currentOccurrenceId();

        controls.send({SystemMediaCommandKind::Play, 0});
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        controls.send({SystemMediaCommandKind::Pause, 0});
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        const auto after_pause_count = controls.snapshots.size();
        controls.send({SystemMediaCommandKind::Pause, 0});
        QCOMPARE(controls.snapshots.size(), after_pause_count);
        controls.send({SystemMediaCommandKind::Play, 0});
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

        controls.send({SystemMediaCommandKind::Next, 0});
        QVERIFY(presenter.currentOccurrenceId() != initial_occurrence);
        controls.send({SystemMediaCommandKind::Previous, 0});
        QCOMPARE(presenter.currentOccurrenceId(), initial_occurrence);

        const auto before_seek_count = controls.snapshots.size();
        controls.send({SystemMediaCommandKind::Seek, 73'125});
        QVERIFY(presenter.seekPending());
        QCOMPARE(presenter.positionMs(), qint64{73'125});
        QCOMPARE(controls.snapshots.size(), before_seek_count + 1);
        QVERIFY(!controls.snapshots.back().seek_enabled);
        QVERIFY(presenter.advanceOneTick());
        QCOMPARE(presenter.confirmedPositionMs(), qint64{73'125});
        QVERIFY(controls.snapshots.back().seek_enabled);
    }

    // S10.1: while a guest follows a listening room, media keys and the Windows flyout never drive the
    // local player; they go to the room, and only what the room allows is enabled.
    void aRoomGuestsSystemControlsGoToTheRoomAndNeverToTheLocalPlayer() {
        TestPlatform platform;
        PlayerPresenter presenter(frozenBundle(), platform);
        FakeSystemMediaControls controls;
        SystemMediaControlsBinding binding(presenter, controls);
        QVERIFY(presenter.loadFrozenQueue());
        const auto occurrence = presenter.currentOccurrenceId();
        bool following = true;
        std::vector<SystemMediaCommandKind> allowed;
        std::vector<SystemMediaCommand> sent;
        binding.setRoomRoute({
            .following = [&] { return following; },
            .allows =
                [&](const SystemMediaCommandKind kind) {
                    return std::find(allowed.begin(), allowed.end(), kind) != allowed.end();
                },
            .send = [&](const SystemMediaCommand& command) { sent.push_back(command); },
        });

        // Nothing allowed: every transport control is off, the title stays.
        auto snapshot = controls.snapshots.back();
        QCOMPARE(snapshot.title, presenter.currentTitle().toStdU16String());
        QCOMPARE(snapshot.status, SystemMediaPlaybackStatus::Playing);
        QVERIFY(!snapshot.play_enabled && !snapshot.pause_enabled && !snapshot.previous_enabled &&
                !snapshot.next_enabled && !snapshot.seek_enabled);
        for (const auto kind : {SystemMediaCommandKind::Pause, SystemMediaCommandKind::Play,
                                SystemMediaCommandKind::Next, SystemMediaCommandKind::Previous})
            controls.send({kind, 0});
        controls.send({SystemMediaCommandKind::Seek, 73'125});
        QVERIFY(sent.empty());
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QCOMPARE(presenter.currentOccurrenceId(), occurrence);
        QVERIFY(!presenter.seekPreviewActive() && !presenter.seekPending());

        // The room allows pausing, skipping and seeking: those turn on and become room commands.
        allowed = {SystemMediaCommandKind::Pause, SystemMediaCommandKind::Next, SystemMediaCommandKind::Previous,
                   SystemMediaCommandKind::Seek};
        binding.refresh();
        snapshot = controls.snapshots.back();
        QVERIFY(snapshot.pause_enabled && snapshot.next_enabled && snapshot.previous_enabled && snapshot.seek_enabled);
        QVERIFY(!snapshot.play_enabled);
        controls.send({SystemMediaCommandKind::Pause, 0});
        controls.send({SystemMediaCommandKind::Next, 0});
        controls.send({SystemMediaCommandKind::Seek, 73'125});
        controls.send({SystemMediaCommandKind::Play, 0});
        QCOMPARE(sent, (std::vector<SystemMediaCommand>{{SystemMediaCommandKind::Pause, 0},
                                                        {SystemMediaCommandKind::Next, 0},
                                                        {SystemMediaCommandKind::Seek, 73'125}}));
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QCOMPARE(presenter.currentOccurrenceId(), occurrence);
        QVERIFY(!presenter.seekPreviewActive() && !presenter.seekPending());

        // Out of the room, the controls drive the local player again.
        following = false;
        binding.refresh();
        QCOMPARE(controls.snapshots.back(), playingSnapshot(presenter));
        controls.send({SystemMediaCommandKind::Pause, 0});
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(sent.size(), std::size_t{3});
    }

    void exitPublishesClosedAndMakesLateCommandsInert() {
        TestPlatform platform;
        PlayerPresenter presenter(frozenBundle(), platform);
        FakeSystemMediaControls controls;
        {
            SystemMediaControlsBinding binding(presenter, controls);
            QVERIFY(presenter.loadFrozenQueue());
            QVERIFY(presenter.requestExit());
            QCOMPARE(platform.exit_requests, 1);
            QCOMPARE(controls.snapshots.back(), SystemMediaSnapshot{});
            const auto snapshots_after_exit = controls.snapshots.size();

            for (const auto command : {SystemMediaCommand{SystemMediaCommandKind::Play, 0},
                                       SystemMediaCommand{SystemMediaCommandKind::Pause, 0},
                                       SystemMediaCommand{SystemMediaCommandKind::Previous, 0},
                                       SystemMediaCommand{SystemMediaCommandKind::Next, 0},
                                       SystemMediaCommand{SystemMediaCommandKind::Seek, 50'000}}) {
                controls.send(command);
            }
            QCOMPARE(controls.snapshots.size(), snapshots_after_exit);
            QCOMPARE(platform.exit_requests, 1);
        }

        QCOMPARE(controls.shutdown_requests, 1);
        QVERIFY(!controls.handler_);
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::SystemMediaControlsBindingTests)

#include "SystemMediaControlsBindingTests.moc"
