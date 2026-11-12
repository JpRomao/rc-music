#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"

#include <QByteArray>
#include <QEvent>
#include <QFile>
#include <QList>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace music_player::m0::test {
namespace {

class FakeAudioOutputDevices final : public QtAudioOutputDevices {
  public:
    QByteArray default_id{"output-a"};
    QList<QtAudioOutputDevice> available_devices;
    QList<QByteArray> applied_ids;
    std::function<void()> handler;

    [[nodiscard]] QtAudioOutputDevice defaultDevice() const override { return {default_id, {}}; }
    [[nodiscard]] QList<QtAudioOutputDevice> devices() const override { return available_devices; }
    void applyDevice(QAudioOutput&, const QtAudioOutputDevice& device) override { applied_ids.append(device.id); }
    void setChangeHandler(std::function<void()> next) override { handler = std::move(next); }
    void notify() {
        const auto callback = handler;
        if (callback) {
            callback();
        }
    }
};

[[nodiscard]] std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path{path.toStdWString()};
#else
    const QByteArray utf8 = path.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

void addBasicCodecRows() {
    QTest::newRow("wav-pcm") << QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE) << qint64{1'000} << qint64{0};
    QTest::newRow("mp3-cbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE) << qint64{2'000} << qint64{50};
    QTest::newRow("mp3-vbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_VBR_FILE) << qint64{2'000} << qint64{50};
    QTest::newRow("mp3-unicode") << QString::fromUtf8(M0_TEST_AUDIO_MP3_UNICODE_FILE) << qint64{2'000} << qint64{50};
    QTest::newRow("m4a-aac") << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE) << qint64{2'000} << qint64{25};
    QTest::newRow("flac") << QString::fromUtf8(M0_TEST_AUDIO_FLAC_FILE) << qint64{2'000} << qint64{20};
    QTest::newRow("ogg-vorbis") << QString::fromUtf8(M0_TEST_AUDIO_OGG_VORBIS_FILE) << qint64{2'000} << qint64{20};
}

void addTruncatedBasicCodecRows() {
    QTest::newRow("mp3-cbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_FILE) << QStringLiteral("truncated.mp3");
    QTest::newRow("m4a-aac") << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE) << QStringLiteral("truncated.m4a");
    QTest::newRow("flac") << QString::fromUtf8(M0_TEST_AUDIO_FLAC_FILE) << QStringLiteral("truncated.flac");
    QTest::newRow("ogg-vorbis") << QString::fromUtf8(M0_TEST_AUDIO_OGG_VORBIS_FILE) << QStringLiteral("truncated.ogg");
}

[[nodiscard]] QString createTruncatedPrefix(const QString& source_file, const QString& target_file,
                                            QTemporaryDir& temporary_directory) {
    constexpr qint64 prefix_size = 64;
    QFile source{source_file};
    if (!source.open(QIODevice::ReadOnly) || source.size() <= prefix_size) {
        return {};
    }
    const QByteArray prefix = source.read(prefix_size);
    if (prefix.size() != prefix_size) {
        return {};
    }

    const QString target_path = temporary_directory.filePath(target_file);
    QFile target{target_path};
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate) || target.write(prefix) != prefix.size()) {
        return {};
    }
    target.close();
    return target_path;
}

} // namespace

class QtMultimediaPlaybackBackendTests final : public QObject {
    Q_OBJECT

  private slots:
#ifdef Q_OS_WIN
    void sourceVolumeMatchingUsesOnlyAnAbsoluteDriveLetter_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("matches");
        QTest::newRow("upper-drive") << QStringLiteral("X:\\fixture\\song.wav") << true;
        QTest::newRow("lower-drive-forward-slash") << QStringLiteral("x:/fixture/song.wav") << true;
        QTest::newRow("extended-drive") << QStringLiteral("\\\\?\\x:\\fixture\\song.wav") << true;
        QTest::newRow("another-drive") << QStringLiteral("Y:\\fixture\\song.wav") << false;
        QTest::newRow("drive-relative") << QStringLiteral("X:fixture\\song.wav") << false;
        QTest::newRow("root-relative") << QStringLiteral("\\fixture\\song.wav") << false;
        QTest::newRow("unc") << QStringLiteral("\\\\example.invalid\\share\\song.wav") << false;
        QTest::newRow("extended-unc") << QStringLiteral("\\\\?\\UNC\\example.invalid\\share\\song.wav") << false;
    }

    void sourceVolumeMatchingUsesOnlyAnAbsoluteDriveLetter() {
        QFETCH(QString, path);
        QFETCH(bool, matches);
        QtMultimediaPlaybackBackend backend{std::make_unique<FakeAudioOutputDevices>()};
        bool saw_loading = false;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.snapshot.status != PlaybackStatus::Loading || saw_loading)
                return;
            saw_loading = true;
            const auto generation = event.snapshot.generation;
            const bool observed_before_notification = backend.sourceVolumeRemovalObserved(generation);
            backend.sourceVolumeRemoved(std::uint32_t{1} << ('X' - 'A'));
            const bool observed_while_pending = backend.sourceVolumeRemovalObserved(generation);
            const bool observed_for_another_generation = backend.sourceVolumeRemovalObserved(generation + 1);
            const auto initial_status = backend.snapshot().status;
            QCoreApplication::sendPostedEvents(&backend, QEvent::MetaCall);
            const auto error = backend.snapshot().error;
            const bool observed_after_publication = backend.sourceVolumeRemovalObserved(generation);
            // Stop the outer load before it gives any synthetic path to Qt.
            const auto unload = backend.unload();
            QCOMPARE(unload, PlaybackCommandResult::Accepted);
            QCOMPARE(initial_status, PlaybackStatus::Loading);
            QCOMPARE(error, matches ? PlaybackErrorCode::SourceVolumeRemoved : PlaybackErrorCode::None);
            QVERIFY(!observed_before_notification);
            QCOMPARE(observed_while_pending, matches);
            QVERIFY(!observed_for_another_generation);
            QCOMPARE(observed_after_publication, matches);
            QVERIFY(!backend.sourceVolumeRemovalObserved(generation));
        });
        QCOMPARE(backend.load(nativePath(path)), PlaybackCommandResult::Accepted);
        QVERIFY(saw_loading);
    }

    void sourceVolumeRemovalRetiresTheDecoderAndPreservesConfirmedTime() {
        const auto fixture = QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE);
        QVERIFY(fixture.size() > 3 && fixture[1] == u':');
        const auto drive = fixture[0].toUpper().toLatin1();
        QVERIFY(drive >= 'A' && drive <= 'Z');
        const auto mask = std::uint32_t{1} << (drive - 'A');
        QtMultimediaPlaybackBackend backend{std::make_unique<FakeAudioOutputDevices>()};
        QCOMPARE(backend.load(nativePath(fixture)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.seekTo(500), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{500}, 2'000);
        QCOMPARE(backend.pause(), PlaybackCommandResult::Accepted);
        const auto confirmed = backend.snapshot();
        const QPointer<QMediaPlayer> retired = backend.findChild<QMediaPlayer*>();
        QVERIFY(retired);
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            events.push_back(event);
            if (event.snapshot.error == PlaybackErrorCode::SourceVolumeRemoved) {
                QVERIFY(retired);
                QVERIFY(retired->audioOutput() == nullptr);
                QCOMPARE(retired->playbackState(), QMediaPlayer::StoppedState);
                QVERIFY(retired->source().isEmpty());
            }
        });

        backend.sourceVolumeRemoved(0);
        backend.sourceVolumeRemoved(std::uint32_t{1} << ((drive - 'A' + 1) % 26));
        QCoreApplication::sendPostedEvents(&backend, QEvent::MetaCall);
        QVERIFY(backend.snapshot() == confirmed);
        backend.sourceVolumeRemoved(mask);
        backend.sourceVolumeRemoved(mask);
        QVERIFY(backend.snapshot() == confirmed);
        QCOMPARE(backend.play(), PlaybackCommandResult::NotReady);
        QCOMPARE(backend.pause(), PlaybackCommandResult::NotReady);
        QCOMPARE(backend.seekTo(0), PlaybackCommandResult::NotReady);
        // The decoder can drop its media before the queued removal is handled.
        retired->setSource({});
        QVERIFY(backend.snapshot() == confirmed);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().error, PlaybackErrorCode::SourceVolumeRemoved, 2'000);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Error);
        QCOMPARE(backend.snapshot().position_ms, confirmed.position_ms);
        QCOMPARE(backend.snapshot().duration_ms, confirmed.duration_ms);
        QCOMPARE(backend.snapshot().generation, confirmed.generation);
        QVERIFY(!backend.snapshot().seekable);
        backend.sourceVolumeRemoved(mask);
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), std::size_t{1});
        QCOMPARE(events.front().type, PlaybackEventType::SnapshotChanged);
    }

    void queuedSourceVolumeRemovalCannotRetireANewerLoadOnTheSameDrive() {
        const auto fixture = QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE);
        QVERIFY(fixture.size() > 3 && fixture[1] == u':');
        const auto drive = fixture[0].toUpper().toLatin1();
        QVERIFY(drive >= 'A' && drive <= 'Z');
        const auto mask = std::uint32_t{1} << (drive - 'A');
        QtMultimediaPlaybackBackend backend{std::make_unique<FakeAudioOutputDevices>()};
        QCOMPARE(backend.load(nativePath(fixture)), PlaybackCommandResult::Accepted);
        const auto generation = backend.snapshot().generation;
        backend.sourceVolumeRemoved(mask);
        QCOMPARE(backend.load(nativePath(fixture)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QVERIFY(backend.snapshot().generation > generation);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
    }

    void sourceVolumeRemovalSuppressesAnEndedEventThatHasNotBeenPublished() {
        const auto fixture = QString::fromUtf8(M0_TEST_AUDIO_WAV_FILE);
        QVERIFY(fixture.size() > 3 && fixture[1] == u':');
        const auto drive = fixture[0].toUpper().toLatin1();
        QVERIFY(drive >= 'A' && drive <= 'Z');
        const auto mask = std::uint32_t{1} << (drive - 'A');
        QtMultimediaPlaybackBackend backend{std::make_unique<FakeAudioOutputDevices>()};
        bool removal_reported = false;
        int ended_count = 0;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::Ended)
                ++ended_count;
            if (!removal_reported && event.snapshot.status == PlaybackStatus::Paused &&
                event.snapshot.duration_ms > 0 && event.snapshot.position_ms == event.snapshot.duration_ms) {
                removal_reported = true;
                backend.sourceVolumeRemoved(mask);
            }
        });
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.load(nativePath(fixture)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.seekTo(900), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{900}, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().error, PlaybackErrorCode::SourceVolumeRemoved, 2'000);
        QVERIFY(removal_reported);
        QCOMPARE(ended_count, 0);
    }
#endif

    void missingDefaultOutputIsActionableAndReconnectRequiresAnExplicitLoad() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        topology->default_id.clear();
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Error);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::AudioOutputUnavailable);
        QCOMPARE(backend.snapshot().generation, std::uint64_t{1});
        QVERIFY(!backend.snapshot().seekable);
        QVERIFY(topology->applied_ids.isEmpty());
        QCOMPARE(backend.play(), PlaybackCommandResult::NotReady);
        const auto failed = backend.snapshot();
        const auto event_count = events.size();

        topology->default_id = "output-b";
        topology->notify();
        QCoreApplication::processEvents();
        QVERIFY(backend.snapshot() == failed);
        QCOMPARE(events.size(), event_count);
        QVERIFY(topology->applied_ids.isEmpty());

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.snapshot().generation, std::uint64_t{2});
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
        QCOMPARE(topology->applied_ids, QList<QByteArray>{"output-b"});
        QVERIFY(std::none_of(events.cbegin(), events.cend(), [](const auto& event) {
            return event.type == PlaybackEventType::Ended || event.snapshot.status == PlaybackStatus::Playing;
        }));
    }

    void changedDefaultRetiresThePlayerAndPreservesTheConfirmedPosition_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("replacement_available");
        for (const auto status :
             {PlaybackStatus::Loading, PlaybackStatus::Ready, PlaybackStatus::Paused, PlaybackStatus::Playing}) {
            for (const bool available : {false, true}) {
                const auto name = QByteArray::number(static_cast<int>(status)) + (available ? "-replaced" : "-lost");
                QTest::newRow(name.constData()) << static_cast<int>(status) << available;
            }
        }
    }

    void changedDefaultRetiresThePlayerAndPreservesTheConfirmedPosition() {
        QFETCH(int, status);
        QFETCH(bool, replacement_available);
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        if (static_cast<PlaybackStatus>(status) != PlaybackStatus::Loading) {
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
            QCOMPARE(backend.seekTo(500), PlaybackCommandResult::Accepted);
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{500}, 2'000);
            if (static_cast<PlaybackStatus>(status) == PlaybackStatus::Paused) {
                QCOMPARE(backend.pause(), PlaybackCommandResult::Accepted);
            } else if (static_cast<PlaybackStatus>(status) == PlaybackStatus::Playing) {
                QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
            }
            QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, static_cast<PlaybackStatus>(status), 2'000);
        }
        const auto confirmed = backend.snapshot();
        QPointer<QMediaPlayer> retired = backend.findChild<QMediaPlayer*>();
        QVERIFY(retired);
        const auto event_count = events.size();

        topology->default_id = replacement_available ? QByteArray{"output-b"} : QByteArray{};
        topology->notify();

        QCOMPARE(backend.snapshot().status, PlaybackStatus::Error);
        QCOMPARE(backend.snapshot().error, replacement_available ? PlaybackErrorCode::AudioOutputChanged
                                                                 : PlaybackErrorCode::AudioOutputUnavailable);
        QCOMPARE(backend.snapshot().position_ms, confirmed.position_ms);
        QCOMPARE(backend.snapshot().duration_ms, confirmed.duration_ms);
        QCOMPARE(backend.snapshot().generation, confirmed.generation);
        QVERIFY(!backend.snapshot().seekable);
        QCOMPARE(events.size(), event_count + 1);
        QCOMPARE(events.back().type, PlaybackEventType::SnapshotChanged);
        QVERIFY(retired->audioOutput() == nullptr);
        QCOMPARE(retired->playbackState(), QMediaPlayer::StoppedState);
        QVERIFY(retired->source().isEmpty());
        QCOMPARE(topology->applied_ids, QList<QByteArray>{"output-a"});
        const auto failed = backend.snapshot();

        QVERIFY(QMetaObject::invokeMethod(retired, "positionChanged", Q_ARG(qint64, 0)));
        topology->default_id = "output-c";
        topology->notify();
        topology->notify();
        QCoreApplication::processEvents();
        QVERIFY(backend.snapshot() == failed);
        QCOMPARE(events.size(), event_count + 1);
        QCOMPARE(backend.play(), PlaybackCommandResult::NotReady);
        QCOMPARE(backend.pause(), PlaybackCommandResult::NotReady);
        QCOMPARE(backend.seekTo(100), PlaybackCommandResult::NotReady);
    }

    void unrelatedOutputTopologyChangesDoNotInterruptTheCurrentDefault() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        const auto confirmed = backend.snapshot();
        topology->notify();
        QCoreApplication::processEvents();
        QVERIFY(backend.snapshot() == confirmed);
        QCOMPARE(topology->applied_ids, QList<QByteArray>{"output-a"});
    }

    void selectingTheEffectiveOutputDoesNotRestartPlaybackAndPinsThatDevice() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        topology->available_devices = {{"output-a", {}, "Speakers"}, {"output-b", {}, "Headphones"}};
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.play(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Playing, 2'000);
        const auto generation = backend.snapshot().generation;
        const auto outputs = backend.audioOutputs();
        QCOMPARE(outputs.size(), std::size_t{2});
        QCOMPARE(outputs.front().description, "Speakers");
        QVERIFY(!outputs.front().id.empty());
        QVERIFY(backend.selectedAudioOutputId().empty());

        QVERIFY(!backend.selectAudioOutput("unknown"));
        QVERIFY(backend.selectedAudioOutputId().empty());
        QVERIFY(backend.selectAudioOutput(outputs.front().id));
        QCOMPARE(backend.selectedAudioOutputId(), outputs.front().id);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Playing);
        QCOMPARE(backend.snapshot().generation, generation);

        topology->default_id = "output-b";
        topology->notify();
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Playing);
        QCOMPARE(backend.snapshot().generation, generation);
        QCOMPARE(topology->applied_ids, QList<QByteArray>{"output-a"});
    }

    void selectingAnotherOutputPreservesPositionUntilExplicitReload() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        topology->available_devices = {{"output-a", {}, "Speakers"}, {"output-b", {}, "Headphones"}};
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.seekTo(500), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{500}, 2'000);
        const auto before = backend.snapshot();

        const auto headphones = backend.audioOutputs().at(1).id;
        QVERIFY(backend.selectAudioOutput(headphones));
        QCOMPARE(backend.selectedAudioOutputId(), headphones);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Error);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::AudioOutputChanged);
        QCOMPARE(backend.snapshot().position_ms, before.position_ms);
        QCOMPARE(backend.snapshot().generation, before.generation);
        QCOMPARE(topology->applied_ids, QList<QByteArray>{"output-a"});
        QCOMPARE(backend.play(), PlaybackCommandResult::NotReady);

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(topology->applied_ids, (QList<QByteArray>{"output-a", "output-b"}));
    }

    void missingSelectedOutputDoesNotFallBackOrResumeOnReturn() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        const QByteArray native_id{"usb\0headphones", 14};
        const QtAudioOutputDevice headphones{native_id, {}, "Fones 日本"};
        topology->available_devices = {{"output-a", {}, "Speakers"}, headphones};
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        const auto available = backend.audioOutputs();
        QCOMPARE(available.at(1).description, "Fones 日本");
        const auto selected = available.at(1).id;
        QVERIFY(!selected.empty());
        QCOMPARE(selected.find('\0'), std::string::npos);
        QVERIFY(backend.selectAudioOutput(selected));
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        int topology_changes = 0;
        backend.setAudioOutputsChangedCallback([&] { ++topology_changes; });

        topology->available_devices.removeLast();
        topology->notify();
        QCOMPARE(topology_changes, 1);
        QCOMPARE(backend.audioOutputs().size(), std::size_t{1});
        QCOMPARE(backend.selectedAudioOutputId(), selected);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Error);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::AudioOutputUnavailable);
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::AudioOutputUnavailable);
        QCOMPARE(topology->applied_ids, QList<QByteArray>{native_id});
        const auto failed = backend.snapshot();

        topology->available_devices.append(headphones);
        topology->notify();
        QCOMPARE(topology_changes, 2);
        QCOMPARE(backend.audioOutputs().at(1).id, selected);
        QVERIFY(backend.snapshot() == failed);
        QCOMPARE(topology->applied_ids, QList<QByteArray>{native_id});

        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(topology->applied_ids, (QList<QByteArray>{native_id, native_id}));
    }

    void mediaSignalReconcilesTheCurrentDefaultBeforePublishingAFileFailure() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        auto* const player = backend.findChild<QMediaPlayer*>();
        QVERIFY(player != nullptr);
        topology->default_id.clear();
        QVERIFY(QMetaObject::invokeMethod(player, "errorOccurred",
                                          Q_ARG(QMediaPlayer::Error, QMediaPlayer::ResourceError),
                                          Q_ARG(QString, QStringLiteral("untrusted device diagnostic"))));
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::AudioOutputUnavailable);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Error);
    }

    void outputChangeDuringLoadingCanBeRecoveredReentrantlyWithoutRevivingTheOldPlayer() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        QtMultimediaPlaybackBackend backend{std::move(devices)};
        bool interrupted = false;
        bool recovered = false;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (!interrupted && event.snapshot.status == PlaybackStatus::Loading) {
                interrupted = true;
                topology->default_id.clear();
                topology->notify();
            } else if (!recovered && event.snapshot.error == PlaybackErrorCode::AudioOutputUnavailable) {
                recovered = true;
                topology->default_id = "output-b";
                QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
            }
        });
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_TRUNCATED_WAV_FILE}),
                 PlaybackCommandResult::Accepted);
        QVERIFY(interrupted);
        QVERIFY(recovered);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.snapshot().generation, std::uint64_t{2});
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
        QCOMPARE(topology->applied_ids, (QList<QByteArray>{"output-a", "output-b"}));
    }

    void outputCallbacksAreInertAfterShutdownAndDestruction() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        auto backend = std::make_unique<QtMultimediaPlaybackBackend>(std::move(devices));
        QVERIFY(static_cast<bool>(topology->handler));
        const auto late_callback = topology->handler;
        QCOMPARE(backend->load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QCOMPARE(backend->shutdown(), PlaybackCommandResult::Accepted);
        QVERIFY(!topology->handler);
        const auto stopped = backend->snapshot();
        topology->default_id.clear();
        late_callback();
        QVERIFY(backend->snapshot() == stopped);
        backend.reset();
        late_callback();
    }

    void outputFailureCallbackMayDestroyTheBackendDuringLoading() {
        auto devices = std::make_unique<FakeAudioOutputDevices>();
        auto* const topology = devices.get();
        auto backend = std::make_unique<QtMultimediaPlaybackBackend>(std::move(devices));
        backend->setEventCallback([&](const PlaybackEvent& event) {
            if (event.snapshot.status == PlaybackStatus::Loading) {
                topology->default_id.clear();
                topology->notify();
            } else if (event.snapshot.error == PlaybackErrorCode::AudioOutputUnavailable) {
                backend.reset();
            }
        });
        QCOMPARE(backend->load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QVERIFY(!backend);
    }

    void loadsAuthenticatedBasicCodecAndPublishesReadyWithinTwoSeconds_data() {
        QTest::addColumn<QString>("source_file");
        QTest::addColumn<qint64>("expected_duration_ms");
        QTest::addColumn<qint64>("duration_tolerance_ms");
        addBasicCodecRows();
    }

    void loadsAuthenticatedBasicCodecAndPublishesReadyWithinTwoSeconds() {
        QFETCH(QString, source_file);
        QFETCH(qint64, expected_duration_ms);
        QFETCH(qint64, duration_tolerance_ms);

        QtMultimediaPlaybackBackend backend;
        QList<PlaybackStatus> observed_statuses;
        backend.setEventCallback([&](const PlaybackEvent& event) {
            if (event.type == PlaybackEventType::SnapshotChanged) {
                observed_statuses.append(event.snapshot.status);
            }
        });

        QCOMPARE(backend.load(nativePath(source_file)), PlaybackCommandResult::Accepted);

        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QVERIFY(backend.snapshot().duration_ms >= expected_duration_ms - duration_tolerance_ms);
        QVERIFY(backend.snapshot().duration_ms <= expected_duration_ms + duration_tolerance_ms);
        QVERIFY(backend.snapshot().seekable);

        const auto loading = std::find(observed_statuses.cbegin(), observed_statuses.cend(), PlaybackStatus::Loading);
        const auto ready = std::find(observed_statuses.cbegin(), observed_statuses.cend(), PlaybackStatus::Ready);
        QVERIFY(loading != observed_statuses.cend());
        QVERIFY(ready != observed_statuses.cend());
        QVERIFY(loading < ready);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
        QCOMPARE(backend.snapshot().generation, std::uint64_t{1});
    }

    void canonicalSeekConfirmsItsDestination_data() {
        QTest::addColumn<QString>("source_file");
        QTest::newRow("mp3-vbr") << QString::fromUtf8(M0_TEST_AUDIO_MP3_VBR_FILE);
        QTest::newRow("m4a-aac") << QString::fromUtf8(M0_TEST_AUDIO_M4A_AAC_FILE);
    }

    void canonicalSeekConfirmsItsDestination() {
        QFETCH(QString, source_file);

        QtMultimediaPlaybackBackend backend;
        QCOMPARE(backend.load(nativePath(source_file)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QVERIFY(backend.snapshot().seekable);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(1'000), PlaybackCommandResult::Accepted);
        constexpr std::int64_t seek_tolerance_ms = 50;
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot().position_ms >= 1'000 - seek_tolerance_ms &&
                                     backend.snapshot().position_ms <= 1'000 + seek_tolerance_ms,
                                 2'000);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
    }

    void pausingAReadySourceNeverStartsPlaybackAndPreservesItsConfirmedPosition() {
        QtMultimediaPlaybackBackend backend;
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });
        QCOMPARE(backend.load(std::filesystem::path{M0_TEST_AUDIO_WAV_FILE}), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        QCOMPARE(backend.seekTo(500), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().position_ms, std::int64_t{500}, 2'000);

        const auto event_count_before_pause = events.size();
        QCOMPARE(backend.pause(), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Paused, 2'000);
        QCOMPARE(backend.snapshot().position_ms, std::int64_t{500});
        for (auto index = event_count_before_pause; index < events.size(); ++index) {
            QVERIFY(events[index].snapshot.status != PlaybackStatus::Playing);
        }
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

    void seekNearEndPublishesEndedExactlyOnceInDocumentedOrder_data() {
        QTest::addColumn<QString>("source_file");
        QTest::addColumn<qint64>("expected_duration_ms");
        QTest::addColumn<qint64>("duration_tolerance_ms");
        addBasicCodecRows();
    }

    void seekNearEndPublishesEndedExactlyOnceInDocumentedOrder() {
        QFETCH(QString, source_file);
        QFETCH(qint64, expected_duration_ms);
        QFETCH(qint64, duration_tolerance_ms);

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

        QCOMPARE(backend.load(nativePath(source_file)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QVERIFY(backend.snapshot().duration_ms >= expected_duration_ms - duration_tolerance_ms);
        QVERIFY(backend.snapshot().duration_ms <= expected_duration_ms + duration_tolerance_ms);
        QCOMPARE(backend.setMuted(true), PlaybackCommandResult::Accepted);
        const auto near_end_ms = std::max<std::int64_t>(0, backend.snapshot().duration_ms - 100);
        QCOMPARE(backend.seekTo(near_end_ms), PlaybackCommandResult::Accepted);
        constexpr std::int64_t seek_tolerance_ms = 50;
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot().position_ms >= near_end_ms - seek_tolerance_ms &&
                                     backend.snapshot().position_ms <= near_end_ms + seek_tolerance_ms,
                                 2'000);
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

    void truncatedBasicCodecFailsSafelyAndDoesNotContaminateNextLoad_data() {
        QTest::addColumn<QString>("source_file");
        QTest::addColumn<QString>("truncated_file_name");
        addTruncatedBasicCodecRows();
    }

    void truncatedBasicCodecFailsSafelyAndDoesNotContaminateNextLoad() {
        QFETCH(QString, source_file);
        QFETCH(QString, truncated_file_name);

        QTemporaryDir temporary_directory;
        QVERIFY(temporary_directory.isValid());
        const QString hostile_source = createTruncatedPrefix(source_file, truncated_file_name, temporary_directory);
        QVERIFY(!hostile_source.isEmpty());
        QCOMPARE(QFile{hostile_source}.size(), qint64{64});

        QtMultimediaPlaybackBackend backend;
        QCOMPARE(backend.load(nativePath(hostile_source)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Error, 2'000);
        QVERIFY(backend.snapshot().error != PlaybackErrorCode::None);
        const auto failed_generation = backend.snapshot().generation;

        QCOMPARE(backend.load(nativePath(source_file)), PlaybackCommandResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QVERIFY(backend.snapshot().generation > failed_generation);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
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

    void immediateValidLoadDiscardsCallbacksFromTruncatedBasicCodec_data() {
        QTest::addColumn<QString>("source_file");
        QTest::addColumn<QString>("truncated_file_name");
        addTruncatedBasicCodecRows();
    }

    void immediateValidLoadDiscardsCallbacksFromTruncatedBasicCodec() {
        QFETCH(QString, source_file);
        QFETCH(QString, truncated_file_name);

        QTemporaryDir temporary_directory;
        QVERIFY(temporary_directory.isValid());
        const QString hostile_source = createTruncatedPrefix(source_file, truncated_file_name, temporary_directory);
        QVERIFY(!hostile_source.isEmpty());
        QCOMPARE(QFile{hostile_source}.size(), qint64{64});

        QtMultimediaPlaybackBackend backend;
        std::vector<PlaybackEvent> events;
        backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });

        QCOMPARE(backend.load(nativePath(hostile_source)), PlaybackCommandResult::Accepted);
        const auto old_generation = backend.snapshot().generation;
        const auto events_before_new_load = events.size();
        QCOMPARE(backend.load(nativePath(source_file)), PlaybackCommandResult::Accepted);
        const auto new_generation = backend.snapshot().generation;
        QVERIFY(new_generation > old_generation);

        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Ready, 2'000);
        QTest::qWait(100);
        QCOMPARE(backend.snapshot().generation, new_generation);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Ready);
        QCOMPARE(backend.snapshot().error, PlaybackErrorCode::None);
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
