#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"

#include <QList>
#include <QTest>

#include <algorithm>
#include <filesystem>

namespace music_player::m0::test {

class QtMultimediaPlaybackBackendTests final : public QObject {
    Q_OBJECT

  private slots:
    void loadsAuthenticatedWavAndPublishesReadyWithinTwoSeconds() {
        QtMultimediaPlaybackBackend backend;
        QList<PlaybackStatus> observed_statuses;
        connect(&backend, &QtMultimediaPlaybackBackend::snapshotChanged, this,
                [&] { observed_statuses.append(backend.snapshot().status); });

        backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE});

        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.snapshot().duration_ms, 1'000);
        QVERIFY(backend.snapshot().seekable);

        const auto loading = std::find(observed_statuses.cbegin(), observed_statuses.cend(), PlaybackStatus::Loading);
        const auto ready = std::find(observed_statuses.cbegin(), observed_statuses.cend(), PlaybackStatus::Ready);
        QVERIFY(loading != observed_statuses.cend());
        QVERIFY(ready != observed_statuses.cend());
        QVERIFY(loading < ready);
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::QtMultimediaPlaybackBackendTests)

#include "QtMultimediaPlaybackBackendTests.moc"
