#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"
#include "m0/adapters/SingleWorkerMetadataReader.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QFile>
#include <QList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

[[nodiscard]] std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path{path.toStdWString()};
#else
    const QByteArray utf8 = path.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

class TestPlatform final : public PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}
};

class RecordingPlaybackBackend final : public PlaybackBackend {
  public:
    explicit RecordingPlaybackBackend(QtMultimediaPlaybackBackend& backend) : backend_(backend) {}

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return backend_.snapshot(); }

    void setEventCallback(PlaybackEventCallback callback) override {
        callback_ = std::move(callback);
        if (!callback_) {
            backend_.setEventCallback({});
            return;
        }
        backend_.setEventCallback([this](const PlaybackEvent& event) {
            observed_statuses_.append(event.snapshot.status);
            callback_(event);
        });
    }

    PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loaded_paths_.push_back(source_path);
        volume_at_load_ = last_volume_;
        muted_at_load_ = last_muted_;
        return backend_.load(source_path);
    }

    PlaybackCommandResult play() override { return backend_.play(); }
    PlaybackCommandResult pause() override { return backend_.pause(); }
    PlaybackCommandResult seekTo(const std::int64_t position_ms) override { return backend_.seekTo(position_ms); }

    PlaybackCommandResult setVolume(const double volume) override {
        const auto result = backend_.setVolume(volume);
        if (result == PlaybackCommandResult::Accepted)
            last_volume_ = volume;
        return result;
    }

    PlaybackCommandResult setMuted(const bool muted) override {
        const auto result = backend_.setMuted(muted);
        if (result == PlaybackCommandResult::Accepted)
            last_muted_ = muted;
        return result;
    }

    PlaybackCommandResult unload() override { return backend_.unload(); }
    PlaybackCommandResult shutdown() override { return backend_.shutdown(); }

    [[nodiscard]] const std::vector<std::filesystem::path>& loadedPaths() const noexcept { return loaded_paths_; }
    [[nodiscard]] double volumeAtLoad() const noexcept { return volume_at_load_; }
    [[nodiscard]] bool mutedAtLoad() const noexcept { return muted_at_load_; }

    [[nodiscard]] bool observed(const PlaybackStatus status) const {
        return std::find(observed_statuses_.cbegin(), observed_statuses_.cend(), status) != observed_statuses_.cend();
    }

  private:
    QtMultimediaPlaybackBackend& backend_;
    PlaybackEventCallback callback_{};
    QList<PlaybackStatus> observed_statuses_{};
    std::vector<std::filesystem::path> loaded_paths_{};
    double last_volume_{-1.0};
    double volume_at_load_{-1.0};
    bool last_muted_{false};
    bool muted_at_load_{false};
};

class RecordingMetadataReader final : public MetadataReader {
  public:
    explicit RecordingMetadataReader(SingleWorkerMetadataReader& reader) : reader_(reader) {}

    MetadataCommandResult startRead(MetadataReadRequest request) override {
        requested_paths_.push_back(request.source_path);
        return reader_.startRead(std::move(request));
    }

    MetadataCommandResult cancelRead(const MetadataGeneration generation) override {
        return reader_.cancelRead(generation);
    }

    [[nodiscard]] MetadataPollResult poll(const MetadataGeneration generation) override {
        auto result = reader_.poll(generation);
        last_poll_status_ = result.status;
        if (result.completion.has_value()) {
            completion_source_path_ = result.completion->source_path;
            if (const auto* track = std::get_if<TrackMetadata>(&result.completion->result); track != nullptr) {
                metadata_source_path_ = track->source_path;
                metadata_duration_ms_ = track->audio.duration_ms;
                metadata_error_.reset();
            } else if (const auto* error = std::get_if<MetadataReadError>(&result.completion->result);
                       error != nullptr) {
                metadata_error_ = error->code;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<MetadataReadCompletion> tryTakeCompletion() override {
        return reader_.tryTakeCompletion();
    }

    MetadataCommandResult shutdown() override { return reader_.shutdown(); }

    [[nodiscard]] const std::vector<std::filesystem::path>& requestedPaths() const noexcept { return requested_paths_; }
    [[nodiscard]] MetadataPollStatus lastPollStatus() const noexcept { return last_poll_status_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& completionSourcePath() const noexcept {
        return completion_source_path_;
    }
    [[nodiscard]] const std::optional<std::filesystem::path>& metadataSourcePath() const noexcept {
        return metadata_source_path_;
    }
    [[nodiscard]] const std::optional<std::int64_t>& metadataDurationMs() const noexcept {
        return metadata_duration_ms_;
    }
    [[nodiscard]] const std::optional<MetadataReadErrorCode>& metadataError() const noexcept { return metadata_error_; }

  private:
    SingleWorkerMetadataReader& reader_;
    std::vector<std::filesystem::path> requested_paths_{};
    MetadataPollStatus last_poll_status_{MetadataPollStatus::Idle};
    std::optional<std::filesystem::path> completion_source_path_{};
    std::optional<std::filesystem::path> metadata_source_path_{};
    std::optional<std::int64_t> metadata_duration_ms_{};
    std::optional<MetadataReadErrorCode> metadata_error_{};
};

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

} // namespace

class LocalAudioIntegrationTests final : public QObject {
    Q_OBJECT

  private slots:
    void opensUnicodeLocalAudioAndDrivesTheRealJourney_data() {
        QTest::addColumn<QString>("source_file");
        QTest::addColumn<QString>("extension");
        QTest::addColumn<qint64>("expected_duration_ms");
        QTest::addColumn<qint64>("duration_tolerance_ms");
        QTest::addColumn<QString>("expected_final_title");

        QTest::newRow("wav-pcm") << QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE) << QStringLiteral(".wav") << qint64{1'000}
                                 << qint64{0} << QStringLiteral("ação 日本 ☕");
        QTest::newRow("mp3-cbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE) << QStringLiteral(".mp3") << qint64{2'000}
                                 << qint64{50} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("mp3-vbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_VBR_FILE) << QStringLiteral(".mp3")
                                 << qint64{2'000} << qint64{50} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("mp3-unicode") << QString::fromUtf8(M0_TEST_AUDIO_MP3_UNICODE_FILE) << QStringLiteral(".mp3")
                                     << qint64{2'000} << qint64{50}
                                     << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("m4a-aac") << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE) << QStringLiteral(".m4a")
                                 << qint64{2'000} << qint64{25} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("m4a-aac-mp4-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE) << QStringLiteral(".mp4") << qint64{2'000} << qint64{25}
            << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("flac") << QString::fromUtf8(M0_TEST_AUDIO_FLAC_FILE) << QStringLiteral(".flac") << qint64{2'000}
                              << qint64{20} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("ogg-vorbis") << QString::fromUtf8(M0_TEST_AUDIO_OGG_VORBIS_FILE) << QStringLiteral(".ogg")
                                    << qint64{2'000} << qint64{20} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("ogg-vorbis-oga-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_OGG_VORBIS_FILE) << QStringLiteral(".oga") << qint64{2'000} << qint64{20}
            << QStringLiteral("Canção determinística 日本語 🎵");
    }

    void opensUnicodeLocalAudioAndDrivesTheRealJourney() {
        QFETCH(QString, source_file);
        QFETCH(QString, extension);
        QFETCH(qint64, expected_duration_ms);
        QFETCH(qint64, duration_tolerance_ms);
        QFETCH(QString, expected_final_title);

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());

        const QString unicode_file = temporary.filePath(QStringLiteral("ação 日本 ☕") + extension);
        QVERIFY(QFile::copy(source_file, unicode_file));
        const auto selected_path = nativePath(unicode_file);

        QtMultimediaPlaybackBackend qt_backend;
        RecordingPlaybackBackend playback{qt_backend};
        SingleWorkerMetadataReader worker{std::make_unique<TagLibMetadataExtractor>()};
        RecordingMetadataReader metadata{worker};
        TestPlatform platform;
        int picker_calls = 0;

        PlayerPresenter presenter{frozenBundle(), platform, playback, metadata,
                                  [&]() -> std::optional<std::filesystem::path> {
                                      ++picker_calls;
                                      return selected_path;
                                  }};

        QVERIFY(presenter.setVolume(0.0));
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());

        QVERIFY(presenter.openAudio());
        QCOMPARE(picker_calls, 1);
        QCOMPARE(presenter.currentTitle(), QStringLiteral("ação 日本 ☕"));
        QVERIFY(presenter.hasCurrentTrack());
        QCOMPARE(playback.loadedPaths().size(), std::size_t{1});
        QVERIFY(playback.loadedPaths().front() == selected_path);
        QCOMPARE(metadata.requestedPaths().size(), std::size_t{1});
        QVERIFY(metadata.requestedPaths().front() == selected_path);
        QCOMPARE(playback.volumeAtLoad(), 0.0);
        QVERIFY(playback.mutedAtLoad());

        bool event_loop_responsive = false;
        QTimer::singleShot(0, &presenter, [&event_loop_responsive] { event_loop_responsive = true; });
        QTRY_VERIFY_WITH_TIMEOUT(event_loop_responsive, 1'000);
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing, 5'000);
        QVERIFY(playback.observed(PlaybackStatus::Loading));
        QVERIFY(playback.observed(PlaybackStatus::Ready));
        QVERIFY(playback.observed(PlaybackStatus::Playing));
        QVERIFY(presenter.durationMs() > 0);
        QVERIFY(presenter.seekable());

        const qint64 midpoint_ms = presenter.durationMs() / 2;
        QVERIFY(presenter.beginSeekPreview(midpoint_ms));
        QVERIFY(presenter.commitSeekPreview());
        QTRY_VERIFY_WITH_TIMEOUT(!presenter.seekPending(), 2'000);
        QVERIFY(presenter.togglePlayback());
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused, 2'000);
        QVERIFY(presenter.confirmedPositionMs() >= midpoint_ms);
        QVERIFY(presenter.confirmedPositionMs() < presenter.durationMs());

        QTRY_COMPARE_WITH_TIMEOUT(metadata.lastPollStatus(), MetadataPollStatus::Completed, 5'000);
        QVERIFY(metadata.completionSourcePath().has_value());
        QVERIFY(*metadata.completionSourcePath() == selected_path);
        QVERIFY(metadata.metadataSourcePath().has_value());
        QVERIFY(*metadata.metadataSourcePath() == selected_path);
        QVERIFY(metadata.metadataDurationMs().has_value());
        QVERIFY(*metadata.metadataDurationMs() >= expected_duration_ms - duration_tolerance_ms);
        QVERIFY(*metadata.metadataDurationMs() <= expected_duration_ms + duration_tolerance_ms);
        QVERIFY(!metadata.metadataError().has_value());
        QCOMPARE(presenter.currentTitle(), expected_final_title);
    }

    void recoversFromTruncatedContainerThroughTheRealJourney_data() {
        QTest::addColumn<QString>("source_file");
        QTest::addColumn<QString>("extension");

        QTest::newRow("mp3-cbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE) << QStringLiteral(".mp3");
        QTest::newRow("m4a-aac") << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE) << QStringLiteral(".m4a");
        QTest::newRow("flac") << QString::fromUtf8(M0_TEST_AUDIO_FLAC_FILE) << QStringLiteral(".flac");
        QTest::newRow("ogg-vorbis") << QString::fromUtf8(M0_TEST_AUDIO_OGG_VORBIS_FILE) << QStringLiteral(".ogg");
    }

    void recoversFromTruncatedContainerThroughTheRealJourney() {
        QFETCH(QString, source_file);
        QFETCH(QString, extension);

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString hostile_file = temporary.filePath(QStringLiteral("falha 日本語") + extension);
        const QString valid_file = temporary.filePath(QStringLiteral("recuperada 日本語") + extension);

        QFile source{source_file};
        QVERIFY(source.open(QIODevice::ReadOnly));
        constexpr qint64 hostile_prefix_size = 64;
        const QByteArray hostile_prefix = source.read(hostile_prefix_size);
        QCOMPARE(hostile_prefix.size(), hostile_prefix_size);
        source.close();

        QFile hostile{hostile_file};
        QVERIFY(hostile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(hostile.write(hostile_prefix), hostile_prefix.size());
        hostile.close();
        QVERIFY(QFile::copy(source_file, valid_file));

        const std::vector<std::filesystem::path> selections{nativePath(hostile_file), nativePath(valid_file)};
        std::size_t selection_index = 0;
        QtMultimediaPlaybackBackend qt_backend;
        RecordingPlaybackBackend playback{qt_backend};
        SingleWorkerMetadataReader worker{std::make_unique<TagLibMetadataExtractor>()};
        RecordingMetadataReader metadata{worker};
        TestPlatform platform;
        PlayerPresenter presenter{frozenBundle(), platform, playback, metadata,
                                  [&]() -> std::optional<std::filesystem::path> {
                                      if (selection_index >= selections.size())
                                          return std::nullopt;
                                      return selections[selection_index++];
                                  }};

        QVERIFY(presenter.setVolume(0.0));
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());

        QVERIFY(presenter.openAudio());
        QCOMPARE(presenter.currentTitle(), QStringLiteral("falha 日本語"));
        bool event_loop_responsive = false;
        QTimer::singleShot(0, &presenter, [&event_loop_responsive] { event_loop_responsive = true; });
        QTRY_VERIFY_WITH_TIMEOUT(event_loop_responsive, 1'000);
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Error, 5'000);
        QVERIFY(presenter.hasError());
        QVERIFY(presenter.hasContextTrack());
        QCOMPARE(presenter.contextTitle(), QStringLiteral("falha 日本語"));
        QCOMPARE(presenter.primaryActionId(), QStringLiteral("open-audio"));
        QTRY_COMPARE_WITH_TIMEOUT(metadata.lastPollStatus(), MetadataPollStatus::Completed, 5'000);
        QVERIFY(metadata.metadataError().has_value());
        QCOMPARE(*metadata.metadataError(), MetadataReadErrorCode::InvalidData);

        QVERIFY(presenter.openAudio());
        QCOMPARE(presenter.currentTitle(), QStringLiteral("recuperada 日本語"));
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing, 5'000);
        QVERIFY(playback.loadedPaths() == selections);
        QVERIFY(metadata.requestedPaths() == selections);
        QVERIFY(!presenter.hasError());
        QVERIFY(!presenter.hasContextTrack());
        QCOMPARE(presenter.primaryActionId(), QStringLiteral("play-pause"));
        QTRY_VERIFY_WITH_TIMEOUT(
            metadata.metadataSourcePath().has_value() && *metadata.metadataSourcePath() == selections.back(), 5'000);
        QVERIFY(!metadata.metadataError().has_value());
        QCOMPARE(presenter.currentTitle(), QStringLiteral("Canção determinística 日本語 🎵"));
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::LocalAudioIntegrationTests)

#include "LocalAudioIntegrationTests.moc"
