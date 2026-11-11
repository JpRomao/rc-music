#include "../audio/Id3PaddedMp3Fixture.hpp"
#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"
#include "m0/adapters/SingleWorkerMetadataReader.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"
#include "m0/presentation/LocalQueueListModel.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QAbstractItemModel>
#include <QFile>
#include <QList>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <id3v2tag.h>
#include <mpegfile.h>
#include <tpropertymap.h>

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

class ScriptedAudioOutputDevices final : public QtAudioOutputDevices {
  public:
    [[nodiscard]] QtAudioOutputDevice defaultDevice() const override { return {id, {}}; }
    void applyDevice(QAudioOutput&, const QtAudioOutputDevice&) override {}
    void setChangeHandler(std::function<void()> callback) override { changed = std::move(callback); }
    void changeTo(QByteArray next) {
        id = std::move(next);
        const auto callback = changed;
        if (callback)
            callback();
    }
    QByteArray id{"test-output-a"};
    std::function<void()> changed;
};

class TestPlatform final : public PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}
};

class SelectedFileLocator final : public AudioFileLocator {
  public:
    std::filesystem::path selected;
    bool locate(const std::filesystem::path&, AudioFileLocationCompletion completion) override {
        completion({AudioFileLocationStatus::Selected, selected});
        return true;
    }
    void cancel() noexcept override {}
};

class MemoryPreferencesWriter final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest request) override {
        if (pending.has_value())
            return PreferencesWriterCommand::Busy;
        pending = PreferencesWriteCompletion{.revision = request.revision, .status = PreferencesSaveStatus::Saved};
        return PreferencesWriterCommand::Accepted;
    }
    std::optional<PreferencesWriteCompletion> poll() override { return std::exchange(pending, std::nullopt); }
    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        pending.reset();
        return PreferencesWriteCompletion{.revision = request.revision, .status = PreferencesSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
    std::optional<PreferencesWriteCompletion> pending;
};

class MemorySessionWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest request) override {
        if (pending.has_value())
            return SessionWriterCommand::Busy;
        pending = SessionWriteCompletion{.revision = request.revision, .status = SessionSaveStatus::Saved};
        return SessionWriterCommand::Accepted;
    }
    std::optional<SessionWriteCompletion> poll() override { return std::exchange(pending, std::nullopt); }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override {
        pending.reset();
        return SessionWriteCompletion{.revision = request.revision, .status = SessionSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
    std::optional<SessionWriteCompletion> pending;
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
            observed_events_.push_back(event);
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

    [[nodiscard]] bool observed(const PlaybackEventType type, const std::uint64_t generation) const {
        return std::ranges::any_of(observed_events_, [type, generation](const PlaybackEvent& event) {
            return event.type == type && event.snapshot.generation == generation;
        });
    }

    [[nodiscard]] bool observed(const PlaybackStatus status, const PlaybackErrorCode error,
                                const std::uint64_t generation) const {
        return std::ranges::any_of(observed_events_, [status, error, generation](const PlaybackEvent& event) {
            return event.snapshot.status == status && event.snapshot.error == error &&
                   event.snapshot.generation == generation;
        });
    }

    [[nodiscard]] std::size_t eventCount(const PlaybackEventType type) const {
        return static_cast<std::size_t>(
            std::ranges::count_if(observed_events_, [type](const PlaybackEvent& event) { return event.type == type; }));
    }

  private:
    QtMultimediaPlaybackBackend& backend_;
    PlaybackEventCallback callback_{};
    QList<PlaybackStatus> observed_statuses_{};
    std::vector<PlaybackEvent> observed_events_{};
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

} // namespace

class LocalAudioIntegrationTests final : public QObject {
    Q_OBJECT

  private slots:
    void metadataReadBudgetKeepsTheRealPlaybackAndFilenameFallback() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto filename = directory.filePath(QStringLiteral("Faixa 日本語.mp3"));
        const auto source = nativePath(filename);
        QVERIFY(writeMp3WithPaddedId3(nativePath(QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE)), source,
                                      32U * 1024U * 1024U + 1U));

        TestPlatform platform;
        QtMultimediaPlaybackBackend engine;
        RecordingPlaybackBackend playback{engine};
        SingleWorkerMetadataReader worker{std::make_unique<TagLibMetadataExtractor>()};
        RecordingMetadataReader metadata{worker};
        PlayerPresenter presenter{platform, playback, metadata, [source] { return std::vector{source}; }};
        QVERIFY(presenter.setVolume(0.0));
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.openAudio());
        QTRY_VERIFY_WITH_TIMEOUT(playback.observed(PlaybackStatus::Playing), 5'000);
        QTRY_COMPARE_WITH_TIMEOUT(metadata.lastPollStatus(), MetadataPollStatus::Completed, 5'000);
        QVERIFY(metadata.metadataError().has_value());
        QCOMPARE(*metadata.metadataError(), MetadataReadErrorCode::InvalidData);
        QCOMPARE(presenter.currentTitle(), QStringLiteral("Faixa 日本語"));
        QTRY_COMPARE_WITH_TIMEOUT(playback.eventCount(PlaybackEventType::Ended), std::size_t{1}, 5'000);
        QVERIFY(!playback.observed(PlaybackStatus::Error));
        QVERIFY(!presenter.hasError());
    }

    void invalidTagsAreSanitizedWithoutInterruptingRealDecodedPlayback() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filename = directory.filePath(QStringLiteral("Metadados 日本語.mp3"));
        QVERIFY(QFile::copy(QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE), filename));
        const auto source = nativePath(filename);
        {
            TagLib::MPEG::File file{source.c_str()};
            QVERIFY(file.isValid());
            auto* tag = file.ID3v2Tag(true);
            QVERIFY(tag != nullptr);
            TagLib::PropertyMap properties;
            properties["TITLE"].append(TagLib::String("  Canção\t 日本語 \xe2\x80\xae🎵  ", TagLib::String::UTF8));
            properties["ARTIST"].append(TagLib::String(" \t\r\n ", TagLib::String::UTF8));
            properties["ALBUM"].append(TagLib::String("Álbum preservado", TagLib::String::UTF8));
            properties["TRACKNUMBER"].append(TagLib::String("-1"));
            properties["DATE"].append(TagLib::String("-1"));
            QVERIFY(tag->setProperties(properties).isEmpty());
            QVERIFY(file.save(TagLib::MPEG::File::ID3v2, TagLib::MPEG::File::StripOthers, TagLib::ID3v2::v4,
                              TagLib::MPEG::File::DoNotDuplicate));
        }
        TestPlatform platform;
        QtMultimediaPlaybackBackend engine;
        RecordingPlaybackBackend backend(engine);
        SingleWorkerMetadataReader reader(std::make_unique<TagLibMetadataExtractor>());
        PlayerPresenter presenter(platform, backend, reader, [source] { return std::vector{source}; });
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.openAudio());
        QTRY_VERIFY_WITH_TIMEOUT(backend.observed(PlaybackStatus::Playing), 5'000);
        QCOMPARE(engine.pause(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QTRY_COMPARE_WITH_TIMEOUT(presenter.detailsAlbum(), QStringLiteral("Álbum preservado"), 5'000);
        QCOMPARE(presenter.currentTitle(), QStringLiteral("Canção 日本語 🎵"));
        QVERIFY(presenter.detailsArtist().isEmpty());
        QVERIFY(!presenter.detailsTrackNumber().isValid());
        QVERIFY(!presenter.detailsYear().isValid());
        QVERIFY(!presenter.hasError());
        QCOMPARE(backend.loadedPaths(), std::vector{source});
        const auto diagnostics = presenter.detailsDiagnostics();
        for (const auto& expected : {
                 QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_text_sanitized")},
                             {QStringLiteral("field"), QStringLiteral("title")}},
                 QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_invalid_value")},
                             {QStringLiteral("field"), QStringLiteral("track_number")}},
                 QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_invalid_value")},
                             {QStringLiteral("field"), QStringLiteral("year")}},
             }) {
            QVERIFY(diagnostics.contains(expected));
        }
        QCOMPARE(engine.shutdown(), PlaybackCommandResult::Accepted);
        QVERIFY(QFile::exists(filename));
    }

    void renamedSourceRestoresThePersistedOccurrencePausedThroughTheRealDecoder() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString original = directory.filePath(QStringLiteral("Original 日本語.wav"));
        const QString renamed = directory.filePath(QStringLiteral("Renomeada 日本語.wav"));
        QVERIFY(QFile::copy(QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE), original));
        QVERIFY(QFile::rename(original, renamed));
        const auto old_path = nativePath(original);
        const auto new_path = nativePath(renamed);
        TestPlatform platform;
        QtMultimediaPlaybackBackend engine;
        RecordingPlaybackBackend backend(engine);
        SingleWorkerMetadataReader reader(std::make_unique<TagLibMetadataExtractor>());
        SelectedFileLocator locator;
        locator.selected = new_path;
        Preferences preferences;
        preferences.restore_last_session = true;
        preferences.muted = true;
        const SessionSnapshot restored{
            .queue = {.items = {{LocalQueueOccurrenceId{7}, old_path}, {LocalQueueOccurrenceId{8}, old_path}},
                      .current_occurrence_id = LocalQueueOccurrenceId{7}},
            .position_ms = 350};
        MemoryPreferencesWriter preferences_writer;
        MemorySessionWriter session_writer;
        const auto now = [] { return PreferencesAutosave::TimePoint{}; };
        PreferencesAutosave preferences_autosave{preferences_writer, preferences, {.now = now}};
        SessionAutosave session_autosave{session_writer, restored, {.now = now}};
        PlayerPresenter presenter(
            platform, backend, reader, [] { return std::vector<std::filesystem::path>{}; },
            PlayerPresenterPreferences{.initial = preferences,
                                       .autosave = &preferences_autosave,
                                       .now = now,
                                       .session = {.initial = restored,
                                                   .load_status = SessionLoadStatus::Loaded,
                                                   .autosave = &session_autosave,
                                                   .now = now}});
        presenter.setAudioFileLocator(&locator);
        QTRY_VERIFY_WITH_TIMEOUT(presenter.hasError(), 5'000);
        QCOMPARE(presenter.errorCode(), QStringLiteral("source_unavailable"));
        QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-7"));
        auto* const model = presenter.queueModelObject();
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QVERIFY(presenter.locateQueueOccurrence(QStringLiteral("local-7")));
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused, 5'000);
        QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{350});
        QCOMPARE(backend.loadedPaths(), (std::vector{old_path, new_path}));
        QVERIFY(!backend.observed(PlaybackStatus::Playing));
        QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-7"));
        QCOMPARE(model->rowCount(), 2);
        QVERIFY(model->data(model->index(0, 0), LocalQueueListModel::AvailableRole).toBool());
        QCOMPARE(model->data(model->index(0, 0), LocalQueueListModel::FileNameRole).toString(),
                 QStringLiteral("Renomeada 日本語.wav"));
        QCOMPARE(model->data(model->index(1, 0), LocalQueueListModel::FileNameRole).toString(),
                 QStringLiteral("Original 日本語.wav"));
        QCOMPARE(resets.count(), 0);
        QVERIFY(!QFile::exists(original));
        QVERIFY(QFile::exists(renamed));
    }

    void outputReconnectRestoresRealDecodedAudioPausedAtTheSamePosition() {
        auto devices = std::make_unique<ScriptedAudioOutputDevices>();
        auto* const topology = devices.get();
        QtMultimediaPlaybackBackend engine{std::move(devices)};
        RecordingPlaybackBackend backend(engine);
        TestPlatform platform;
        SingleWorkerMetadataReader reader(std::make_unique<TagLibMetadataExtractor>());
        const auto source = nativePath(QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE));
        PlayerPresenter presenter(platform, backend, reader, [source] { return std::vector{source, source}; });
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.openAudio());
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing, 5'000);
        QCOMPARE(engine.pause(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(engine.seekTo(350), PlaybackCommandResult::Accepted);
        QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{350});
        const QString occurrence = presenter.currentOccurrenceId();
        auto* const model = presenter.queueModelObject();
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);

        topology->changeTo({});
        QTRY_COMPARE(presenter.errorCode(), QStringLiteral("audio_output_unavailable"));
        QCOMPARE(presenter.confirmedPositionMs(), qint64{350});
        QVERIFY(presenter.executeErrorAction());
        QTRY_COMPARE(presenter.errorCode(), QStringLiteral("audio_output_unavailable"));
        topology->changeTo("test-output-b");
        QCoreApplication::processEvents();
        QVERIFY(presenter.hasError());
        QVERIFY(presenter.executeErrorAction());
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused, 5'000);
        QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{350});
        QCOMPARE(engine.snapshot().generation, std::uint64_t{3});
        QCOMPARE(presenter.currentOccurrenceId(), occurrence);
        QCOMPARE(backend.loadedPaths(), (std::vector{source, source, source}));
        QVERIFY(!backend.observed(PlaybackStatus::Playing, PlaybackErrorCode::None, 2));
        QVERIFY(!backend.observed(PlaybackStatus::Playing, PlaybackErrorCode::None, 3));
        QCOMPARE(backend.eventCount(PlaybackEventType::Ended), std::size_t{0});
        QCOMPARE(model->rowCount(), 2);
        QVERIFY(model->data(model->index(0, 0), LocalQueueListModel::AvailableRole).toBool());
        QCOMPARE(resets.size(), 0);
    }

    void restoredSourceRetriesTheSameOccurrenceThroughTheRealBackend() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString restored_file = directory.filePath(QStringLiteral("Restaurada 日本語.wav"));
        const auto source = nativePath(restored_file);
        TestPlatform platform;
        QtMultimediaPlaybackBackend engine;
        RecordingPlaybackBackend backend(engine);
        SingleWorkerMetadataReader reader(std::make_unique<TagLibMetadataExtractor>());
        PlayerPresenter presenter(platform, backend, reader, [source] { return std::vector{source}; });
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.openAudio());
        const QString occurrence = presenter.currentOccurrenceId();
        QTRY_VERIFY_WITH_TIMEOUT(presenter.hasError(), 5'000);
        QCOMPARE(presenter.errorCode(), QStringLiteral("source_unavailable"));
        QVERIFY(presenter.canRetryCurrentOccurrence());
        auto* const model = presenter.queueModelObject();
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QVERIFY(!model->data(model->index(0, 0), LocalQueueListModel::AvailableRole).toBool());

        QVERIFY(QFile::copy(QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE), restored_file));
        QVERIFY(presenter.retryQueueOccurrence(occurrence));
        QTRY_VERIFY_WITH_TIMEOUT(backend.observed(PlaybackStatus::Playing), 5'000);
        QTRY_VERIFY(presenter.retryingOccurrenceId().isEmpty());
        QCOMPARE(presenter.currentOccurrenceId(), occurrence);
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(backend.loadedPaths(), (std::vector{source, source}));
        QVERIFY(model->data(model->index(0, 0), LocalQueueListModel::AvailableRole).toBool());
        QVERIFY(!model->data(model->index(0, 0), LocalQueueListModel::ErrorCodeRole).isValid());
        QCOMPARE(resets.size(), 0);
        QVERIFY(!presenter.hasError());
    }

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

        // Explicit local selections also reach the decoder with an unknown
        // suffix. These are temporary copies, not additional corpus fixtures.
        QTest::newRow("wav-pcm-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE) << QStringLiteral(".audio") << qint64{1'000} << qint64{0}
            << QStringLiteral("ação 日本 ☕");
        QTest::newRow("mp3-cbr-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE) << QStringLiteral(".audio") << qint64{2'000} << qint64{50}
            << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("mp3-vbr-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_MP3_VBR_FILE) << QStringLiteral(".audio") << qint64{2'000} << qint64{50}
            << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("mp3-unicode-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_MP3_UNICODE_FILE) << QStringLiteral(".audio") << qint64{2'000}
            << qint64{50} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("m4a-aac-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE) << QStringLiteral(".audio") << qint64{2'000} << qint64{25}
            << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("flac-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_FLAC_FILE) << QStringLiteral(".audio") << qint64{2'000} << qint64{20}
            << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("id3-prefixed-flac-with-mp3-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_ID3_PREFIXED_FLAC_FILE) << QStringLiteral(".mp3") << qint64{2'000}
            << qint64{20} << QStringLiteral("Canção determinística 日本語 🎵");
        QTest::newRow("ogg-vorbis-neutral-extension")
            << QString::fromUtf8(M0_TEST_AUDIO_OGG_VORBIS_FILE) << QStringLiteral(".audio") << qint64{2'000}
            << qint64{20} << QStringLiteral("Canção determinística 日本語 🎵");
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

        PlayerPresenter presenter{platform, playback, metadata, [&]() -> std::vector<std::filesystem::path> {
                                      ++picker_calls;
                                      return {selected_path};
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

        QVERIFY(presenter.togglePlayback());
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused, 2'000);
        const auto playback_generation = qt_backend.snapshot().generation;
        const QString occurrence_id = presenter.currentOccurrenceId();
        const qint64 midpoint_ms = presenter.durationMs() / 2;
        QVERIFY(presenter.beginSeekPreview(midpoint_ms));
        QVERIFY(presenter.commitSeekPreview());
        QTRY_VERIFY_WITH_TIMEOUT(!presenter.seekPending(), 2'000);
        constexpr qint64 midpoint_tolerance_ms = 500;
        QVERIFY(presenter.confirmedPositionMs() >= midpoint_ms - midpoint_tolerance_ms);
        QVERIFY(presenter.confirmedPositionMs() <= midpoint_ms + midpoint_tolerance_ms);
        QVERIFY(qt_backend.snapshot().position_ms >= midpoint_ms - midpoint_tolerance_ms);
        QVERIFY(qt_backend.snapshot().position_ms <= midpoint_ms + midpoint_tolerance_ms);
        QVERIFY(presenter.confirmedPositionMs() < presenter.durationMs());
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(qt_backend.snapshot().generation, playback_generation);

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

        // Decode the entire short fixture after the midpoint check. Seeking to
        // the final packet would only characterize the terminal transition.
        QCOMPARE(playback.eventCount(PlaybackEventType::Ended), std::size_t{0});
        QVERIFY(presenter.beginSeekPreview(0));
        QVERIFY(presenter.commitSeekPreview());
        QTRY_VERIFY_WITH_TIMEOUT(!presenter.seekPending(), 2'000);
        QCOMPARE(presenter.confirmedPositionMs(), qint64{0});
        QCOMPARE(qt_backend.snapshot().position_ms, std::int64_t{0});
        QVERIFY(presenter.togglePlayback());
        QTRY_COMPARE_WITH_TIMEOUT(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing, 2'000);
        QTRY_VERIFY_WITH_TIMEOUT(playback.observed(PlaybackEventType::Ended, playback_generation), 5'000);

        bool terminal_events_drained = false;
        QVERIFY(QMetaObject::invokeMethod(
            &presenter, [&terminal_events_drained] { terminal_events_drained = true; }, Qt::QueuedConnection));
        QTRY_VERIFY_WITH_TIMEOUT(terminal_events_drained, 2'000);
        QCOMPARE(playback.eventCount(PlaybackEventType::Ended), std::size_t{1});
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(presenter.confirmedPositionMs(), presenter.durationMs());
        QCOMPARE(presenter.currentOccurrenceId(), occurrence_id);
        QCOMPARE(qt_backend.snapshot().generation, playback_generation);
        QCOMPARE(qt_backend.snapshot().error, PlaybackErrorCode::None);
        QVERIFY(!playback.observed(PlaybackStatus::Error));
        QCOMPARE(playback.loadedPaths().size(), std::size_t{1});
    }

    void opensTwoSelectedFilesAndNavigatesWithTheRealBackend() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source_file = QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE);
        const QString first_file = temporary.filePath(QStringLiteral("primeira 日本語.mp3"));
        const QString second_file = temporary.filePath(QStringLiteral("segunda ação.mp3"));
        QVERIFY(QFile::copy(source_file, first_file));
        QVERIFY(QFile::copy(source_file, second_file));
        const std::vector<std::filesystem::path> selected_paths{nativePath(first_file), nativePath(second_file)};

        QtMultimediaPlaybackBackend qt_backend;
        RecordingPlaybackBackend playback{qt_backend};
        SingleWorkerMetadataReader worker{std::make_unique<TagLibMetadataExtractor>()};
        RecordingMetadataReader metadata{worker};
        TestPlatform platform;
        int picker_calls = 0;
        PlayerPresenter presenter{platform, playback, metadata, [&]() -> std::vector<std::filesystem::path> {
                                      ++picker_calls;
                                      return selected_paths;
                                  }};

        QVERIFY(presenter.setVolume(0.0));
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.openAudio());
        QAbstractItemModel* const model = presenter.queueModelObject();
        QCOMPARE(picker_calls, 1);
        QVERIFY(presenter.queueLoaded());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(presenter.currentQueueIndex(), 0);
        const QString first_occurrence_id = presenter.currentOccurrenceId();
        QVERIFY(!first_occurrence_id.isEmpty());
        QVERIFY(playback.loadedPaths() == std::vector<std::filesystem::path>{selected_paths.front()});
        QVERIFY(metadata.requestedPaths() == std::vector<std::filesystem::path>{selected_paths.front()});

        QTRY_VERIFY_WITH_TIMEOUT(qt_backend.snapshot().generation == 1 &&
                                     presenter.playbackState() == PlayerPresenter::PlaybackState::Playing,
                                 5'000);
        QVERIFY(presenter.nextTrack());
        QCOMPARE(presenter.currentQueueIndex(), 1);
        QVERIFY(presenter.currentOccurrenceId() != first_occurrence_id);
        QVERIFY(playback.loadedPaths() == selected_paths);
        QVERIFY(metadata.requestedPaths() == selected_paths);
        QVERIFY(!presenter.canGoNext());

        QTRY_VERIFY_WITH_TIMEOUT(qt_backend.snapshot().generation == 2 &&
                                     presenter.playbackState() == PlayerPresenter::PlaybackState::Playing,
                                 5'000);
        QVERIFY(presenter.canGoPrevious());
    }

    void realEndedSkipsInvalidBetweenValidTracksAndStopsAtTail() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());

        const QString first_file = temporary.filePath(QStringLiteral("válida A 日本.wav"));
        const QString invalid_file = temporary.filePath(QStringLiteral("inválida 中間.wav"));
        const QString second_file = temporary.filePath(QStringLiteral("válida B ☕.wav"));
        QVERIFY(QFile::copy(QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE), first_file));
        QVERIFY(QFile::copy(QString::fromUtf8(M0_TEST_AUDIO_TRUNCATED_WAV_FILE), invalid_file));
        QVERIFY(QFile::copy(QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE), second_file));

        const std::vector<std::filesystem::path> selected_paths{
            nativePath(first_file),
            nativePath(invalid_file),
            nativePath(second_file),
        };
        QtMultimediaPlaybackBackend qt_backend;
        RecordingPlaybackBackend playback{qt_backend};
        SingleWorkerMetadataReader worker{std::make_unique<TagLibMetadataExtractor>()};
        RecordingMetadataReader metadata{worker};
        TestPlatform platform;
        int picker_calls = 0;
        PlayerPresenter presenter{platform, playback, metadata, [&]() -> std::vector<std::filesystem::path> {
                                      ++picker_calls;
                                      return selected_paths;
                                  }};

        QVERIFY(presenter.setVolume(0.0));
        if (!presenter.muted())
            QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.openAudio());

        QAbstractItemModel* const model = presenter.queueModelObject();
        QSignalSpy model_resets(model, &QAbstractItemModel::modelReset);
        QCOMPARE(picker_calls, 1);
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(presenter.currentQueueIndex(), 0);
        QCOMPARE(playback.loadedPaths(), std::vector<std::filesystem::path>{selected_paths.front()});
        QTRY_VERIFY_WITH_TIMEOUT(qt_backend.snapshot().generation == 1 &&
                                     presenter.playbackState() == PlayerPresenter::PlaybackState::Playing,
                                 5'000);

        const qint64 first_near_end_ms = std::max<qint64>(0, presenter.durationMs() - 100);
        QVERIFY(presenter.beginSeekPreview(first_near_end_ms));
        QVERIFY(presenter.commitSeekPreview());
        QTRY_VERIFY_WITH_TIMEOUT(playback.observed(PlaybackEventType::Ended, 1), 2'000);
        QTRY_VERIFY_WITH_TIMEOUT(qt_backend.snapshot().generation == 3 &&
                                     presenter.playbackState() == PlayerPresenter::PlaybackState::Playing,
                                 5'000);

        QCOMPARE(playback.loadedPaths(), selected_paths);
        QCOMPARE(metadata.requestedPaths(), selected_paths);
        QVERIFY(playback.observed(PlaybackStatus::Error, PlaybackErrorCode::InvalidMedia, 2));
        QCOMPARE(presenter.currentQueueIndex(), 2);
        QVERIFY(model->data(model->index(0, 0), LocalQueueListModel::AvailableRole).toBool());
        QVERIFY(!model->data(model->index(1, 0), LocalQueueListModel::AvailableRole).toBool());
        QCOMPARE(model->data(model->index(1, 0), LocalQueueListModel::ErrorCodeRole).toString(),
                 QStringLiteral("invalid_media"));
        QVERIFY(model->data(model->index(2, 0), LocalQueueListModel::AvailableRole).toBool());
        QCOMPARE(model_resets.size(), 0);

        const QString second_occurrence_id = presenter.currentOccurrenceId();
        const qint64 second_near_end_ms = std::max<qint64>(0, presenter.durationMs() - 100);
        QVERIFY(presenter.beginSeekPreview(second_near_end_ms));
        QVERIFY(presenter.commitSeekPreview());
        QTRY_VERIFY_WITH_TIMEOUT(playback.observed(PlaybackEventType::Ended, 3), 2'000);
        bool terminal_events_drained = false;
        QVERIFY(QMetaObject::invokeMethod(
            &presenter, [&terminal_events_drained] { terminal_events_drained = true; }, Qt::QueuedConnection));
        QTRY_VERIFY_WITH_TIMEOUT(terminal_events_drained, 2'000);

        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QCOMPARE(playback.eventCount(PlaybackEventType::Ended), std::size_t{2});
        QCOMPARE(playback.loadedPaths(), selected_paths);
        QCOMPARE(presenter.currentQueueIndex(), 2);
        QCOMPARE(presenter.currentOccurrenceId(), second_occurrence_id);
        QCOMPARE(qt_backend.snapshot().generation, std::uint64_t{3});
        QCOMPARE(presenter.confirmedPositionMs(), presenter.durationMs());
        QVERIFY(!presenter.canGoNext());
        QCOMPARE(model_resets.size(), 0);
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
        PlayerPresenter presenter{platform, playback, metadata, [&]() -> std::vector<std::filesystem::path> {
                                      if (selection_index >= selections.size())
                                          return {};
                                      return {selections[selection_index++]};
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
