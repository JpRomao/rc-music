#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"

#include <QList>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace music_player::m0::test {

class QtMultimediaPlaybackBackendTests final : public QObject {
    Q_OBJECT

  private slots:
    void loadsAuthenticatedWavAndPublishesReadyWithinTwoSeconds() {
        QtMultimediaPlaybackBackend backend;
        QList<PlaybackStatus> observed_statuses;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::SnapshotChanged) {
                observed_statuses.append(event.snapshot.status);
            }
        });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);

        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.snapshot().duration_ms, 1'000);
        QVERIFY(backend.snapshot().seekable);

        const auto loading = std::find(observed_statuses.cbegin(), observed_statuses.cend(), PlaybackStatus::Loading);
        const auto ready = std::find(observed_statuses.cbegin(), observed_statuses.cend(), PlaybackStatus::Ready);
        QVERIFY(loading != observed_statuses.cend());
        QVERIFY(ready != observed_statuses.cend());
        QVERIFY(loading < ready);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
        QCOMPARE(backend.snapshot().generation, std::uint64_t{1});
    }

    void playPauseAndSeekAreIdempotentForAuthenticatedWav() {
        QtMultimediaPlaybackBackend backend;
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);

        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(500), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{500}, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Playing, 2'000);
        const auto before_duplicate_play = backend.snapshot();
        const auto event_count_before_duplicate_play = events.size();
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        QTest::qWait(50);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Playing);
        QCOMPARE(backend.snapshot().generation, before_duplicate_play.generation);
        QCOMPARE(backend.snapshot().duration_ms, before_duplicate_play.duration_ms);
        QVERIFY(backend.snapshot().position_ms >= before_duplicate_play.position_ms);
        for (auto index = event_count_before_duplicate_play; index < events.size(); ++index) {
            QCOMPARE(events[index].type, PlaybackEventType::SnapshotChanged);
            QCOMPARE(events[index].snapshot.status, PlaybackStatus::Playing);
            QCOMPARE(events[index].snapshot.generation, before_duplicate_play.generation);
        }

        QCOMPARE(backend.pause(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Paused, 2'000);
        const auto after_first_pause = backend.snapshot();
        const auto event_count_after_first_pause = events.size();
        QCOMPARE(backend.pause(), PlaybackCommandResult::Accepted);
        QTest::qWait(50);
        QVERIFY(backend.snapshot() == after_first_pause);
        QCOMPARE(events.size(), event_count_after_first_pause);
    }

    void seekNearEndPublishesEndedExactlyOnceInDocumentedOrder() {
        QtMultimediaPlaybackBackend backend;
        int ended_count = 0;
        std::uint64_t ended_generation = 0;
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            events.push_back(event);
            if (event.type == PlaybackEventType::Ended) {
                ++ended_count;
                ended_generation = event.snapshot.generation;
            }
        });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{900}, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        const auto events_before_end = events.size();

        QTRY_COMPARE_WITH_TIMEOUT(ended_count, 1, 2'000);
        QCOMPARE(ended_generation, backend.snapshot().generation);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Paused);
        QCOMPARE(backend.snapshot().position_ms, backend.snapshot().duration_ms);
        const auto ended = std::find_if(events.cbegin(), events.cend(), [](const PlaybackEvent& event) {
            return event.type == PlaybackEventType::Ended;
        });
        QVERIFY(ended != events.cend());
        QVERIFY(ended != events.cbegin());
        const auto& final_snapshot = *std::prev(ended);
        QCOMPARE(final_snapshot.type, PlaybackEventType::SnapshotChanged);
        QCOMPARE(final_snapshot.snapshot.status, PlaybackStatus::Paused);
        QCOMPARE(final_snapshot.snapshot.position_ms, final_snapshot.snapshot.duration_ms);
        QCOMPARE(final_snapshot.snapshot.generation, ended->snapshot.generation);
        const auto ended_index = static_cast<std::size_t>(std::distance(events.cbegin(), ended));
        for (auto index = events_before_end; index < ended_index; ++index) {
            if (events[index].type == PlaybackEventType::SnapshotChanged) {
                QVERIFY(events[index].snapshot.status != PlaybackStatus::Ready);
            }
        }
        QCOMPARE(backend.pause(), PlaybackCommandResult::Accepted);
        QTest::qWait(100);
        QCOMPARE(ended_count, 1);
    }

    void seekingBackAfterEndDoesNotPublishUntilPlaybackEndsAgain() {
        QtMultimediaPlaybackBackend backend;
        int ended_count = 0;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::Ended) {
                ++ended_count;
            }
        });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{900}, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(ended_count, 1, 2'000);

        QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{900}, 2'000);
        QTest::qWait(100);
        QCOMPARE(ended_count, 1);

        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(ended_count, 2, 2'000);
        QTest::qWait(100);
        QCOMPARE(ended_count, 2);
    }

    void hostileInputFailsSafelyAndDoesNotContaminateNextLoad() {
        const std::filesystem::path hostile_sources[]{
            std::filesystem::path{M0_TEST_AUDIO_EMPTY_FILE},
            std::filesystem::path{M0_TEST_AUDIO_TRUNCATED_WAV_FILE},
        };
        for (const auto& hostile_source : hostile_sources) {
            QtMultimediaPlaybackBackend backend;

            QCOMPARE(backend.load(hostile_source), PlaybackCommandResult::Accepted);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Error, 2'000);
            QCOMPARE(backend.snapshot().error, PlaybackErrorCode::InvalidMedia);
            const auto failed_generation = backend.snapshot().generation;

            QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
            QVERIFY(backend.snapshot().generation > failed_generation);
            QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
        }
    }

    void newerLoadDiscardsOlderGenerationCallbacks() {
        QtMultimediaPlaybackBackend backend;
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_TRUNCATED_WAV_FILE}),
                 PlaybackCommandResult::Accepted);
        const auto old_generation = backend.snapshot().generation;
        const auto events_before_new_load = events.size();
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        const auto new_generation = backend.snapshot().generation;
        QVERIFY(new_generation > old_generation);

        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QTest::qWait(100);
        QCOMPARE(backend.snapshot().generation, new_generation);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Ready);
        QVERIFY(events.size() > events_before_new_load);
        for (auto index = events_before_new_load; index < events.size(); ++index) {
            QCOMPARE(events[index].snapshot.generation, new_generation);
            QVERIFY(events[index].snapshot.status != PlaybackStatus::Error);
        }
    }

    void reentrantUnloadDuringLoadingSupersedesTheOuterLoadSafely() {
        QtMultimediaPlaybackBackend backend;
        bool unloaded = false;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (!unloaded && event.type == PlaybackEventType::SnapshotChanged &&
                event.snapshot.status == PlaybackStatus::Loading) {
                unloaded = true;
                QCOMPARE(backend.unload(), PlaybackCommandResult::Accepted);
            }
        });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QVERIFY(unloaded);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Empty);
        QTest::qWait(100);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Empty);
    }

    void reentrantReloadAtEndDoesNotAttributeOldEndedToTheNewGeneration() {
        QtMultimediaPlaybackBackend backend;
        bool reloaded = false;
        QList<std::uint64_t> ended_generations;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::Ended) {
                ended_generations.append(event.snapshot.generation);
            }
            if (!reloaded && event.type == PlaybackEventType::SnapshotChanged &&
                event.snapshot.status == PlaybackStatus::Paused && event.snapshot.duration_ms > 0 &&
                event.snapshot.position_ms == event.snapshot.duration_ms) {
                reloaded = true;
                QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
            }
        });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{900}, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);

        QTRY_VERIFY_WITH_TIMEOUT(reloaded, 2'000);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(ended_generations.size(), 0);
    }

    void reentrantSeekFromFinalSnapshotSupersedesThePendingEndedEvent() {
        QtMultimediaPlaybackBackend backend;
        bool sought_back = false;
        int ended_count = 0;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::Ended) {
                ++ended_count;
            }
            if (!sought_back && event.type == PlaybackEventType::SnapshotChanged &&
                event.snapshot.status == PlaybackStatus::Paused && event.snapshot.duration_ms > 0 &&
                event.snapshot.position_ms == event.snapshot.duration_ms) {
                sought_back = true;
                QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
            }
        });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{900}, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);

        QTRY_VERIFY_WITH_TIMEOUT(sought_back, 2'000);
        QTest::qWait(100);
        QCOMPARE(ended_count, 0);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Paused);
        QCOMPARE(backend.snapshot().position_ms, std::int64_t{900});
    }

    void detectsPlayableWavContentBehindMp3Extension() {
        QtMultimediaPlaybackBackend backend;

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WRONG_EXTENSION_FILE}),
                 PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.snapshot().duration_ms, std::int64_t{1'000});
        QVERIFY(backend.snapshot().seekable);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
    }

    void unloadAndShutdownAreIdempotentWithTypedPostShutdownFailures() {
        QtMultimediaPlaybackBackend backend;
        int snapshot_count = 0;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::SnapshotChanged) {
                ++snapshot_count;
            }
        });

        const auto empty_snapshot = backend.snapshot();
        QCOMPARE(backend.seekTo(500), PlaybackCommandResult::NotReady);
        QVERIFY(backend.snapshot() == empty_snapshot);

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.unload(), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Empty);
        const auto after_first_unload = snapshot_count;
        QCOMPARE(backend.unload(), PlaybackCommandResult::Accepted);
        QCOMPARE(snapshot_count, after_first_unload);

        QCOMPARE(backend.shutdown(), PlaybackCommandResult::Accepted);
        const auto after_first_shutdown = backend.snapshot();
        const auto after_first_shutdown_count = snapshot_count;
        QCOMPARE(backend.shutdown(), PlaybackCommandResult::Accepted);
        QVERIFY(backend.snapshot() == after_first_shutdown);
        QCOMPARE(snapshot_count, after_first_shutdown_count);
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Shutdown);
        QCOMPARE(backend.play(), PlaybackCommandResult::Shutdown);
        QCOMPARE(backend.pause(), PlaybackCommandResult::Shutdown);
        QCOMPARE(backend.seekTo(500), PlaybackCommandResult::Shutdown);
        QCOMPARE(backend.setVolume(0.5), PlaybackCommandResult::Shutdown);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Shutdown);
        QCOMPARE(backend.unload(), PlaybackCommandResult::Shutdown);
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::QtMultimediaPlaybackBackendTests)

#include "QtMultimediaPlaybackBackendTests.moc"
