#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/ports/DiagnosticLog.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlatformIntegration.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/ShellDiagnosticBinding.hpp"

#include <QTest>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

class TestPlatform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}
};

class RecordingSink final : public DiagnosticSink {
  public:
    bool submit(const DiagnosticEntry entry) noexcept override {
        entries.push_back(entry);
        if (on_submit)
            on_submit();
        return accept;
    }
    std::vector<DiagnosticEntry> entries;
    std::function<void()> on_submit;
    bool accept{true};
};

class TestBackend final : public PlaybackBackend {
  public:
    const PlaybackSnapshot& snapshot() const noexcept override { return state; }
    void setEventCallback(PlaybackEventCallback next) override { callback = std::move(next); }
    PlaybackCommandResult load(const std::filesystem::path&) override {
        state = {.status = PlaybackStatus::Loading, .generation = state.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult pause() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Busy; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override {
        state = {.status = PlaybackStatus::Empty, .generation = state.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }
    void fail(const PlaybackErrorCode error) {
        state.status = PlaybackStatus::Error;
        state.error = error;
        if (callback)
            callback({PlaybackEventType::SnapshotChanged, state});
    }
    PlaybackSnapshot state;
    PlaybackEventCallback callback;
};

class TestMetadataReader final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest) override { return MetadataCommandResult::Accepted; }
    MetadataCommandResult cancelRead(MetadataGeneration) override { return MetadataCommandResult::Accepted; }
    MetadataPollResult poll(MetadataGeneration) override { return {MetadataPollStatus::Pending, std::nullopt}; }
    std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
};

class TestPreferencesWriter final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest request) override {
        pending = request;
        return PreferencesWriterCommand::Accepted;
    }
    std::optional<PreferencesWriteCompletion> poll() override {
        if (!pending)
            return std::nullopt;
        const auto request = std::exchange(pending, {});
        return PreferencesWriteCompletion{request->revision, result};
    }
    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        return PreferencesWriteCompletion{request.revision, result};
    }
    bool shutdown() override { return true; }
    std::optional<PreferencesWriteRequest> pending;
    PreferencesSaveStatus result{PreferencesSaveStatus::Failed};
};

class TestSessionWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest) override { return SessionWriterCommand::Busy; }
    std::optional<SessionWriteCompletion> poll() override { return std::nullopt; }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest) override { return std::nullopt; }
    bool shutdown() override { return true; }
};

DiagnosticEntry entry(const DiagnosticEvent event, const DiagnosticCode code,
                      const DiagnosticSeverity severity = DiagnosticSeverity::Warning) {
    return {.severity = severity, .event = event, .code = code};
}

std::filesystem::path syntheticSource() {
    return std::filesystem::temp_directory_path() / "PRIVATE_source_filename.mp3";
}

} // namespace

class ShellDiagnosticBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void recordsSourceFailureOnceUntilRecoveryAndDoesNotLogOccurrenceOrPath() {
        TestPlatform platform;
        TestBackend backend;
        TestMetadataReader reader;
        RecordingSink sink;
        PlayerPresenter presenter(platform, backend, reader, [] { return std::vector{syntheticSource()}; });
        ShellDiagnosticBinding binding(presenter, sink);
        QVERIFY(sink.entries.empty());
        QVERIFY(presenter.openAudio());
        backend.fail(PlaybackErrorCode::SourceUnavailable);
        QTRY_VERIFY(presenter.hasError());
        QCOMPARE(sink.entries, std::vector{entry(DiagnosticEvent::SourceOpenFailed, DiagnosticCode::SourceUnavailable,
                                                 DiagnosticSeverity::Error)});
        presenter.stateChanged();
        binding.refresh();
        QCOMPARE(sink.entries.size(), std::size_t{1});
        const auto record = formatDiagnosticRecord({.timestamp_utc_ms = 1, .entry = sink.entries.front()});
        QVERIFY(record.find("PRIVATE_source_filename") == std::string::npos);
        QVERIFY(record.find("occurrence") == std::string::npos);

        QVERIFY(presenter.retryQueueOccurrence(presenter.currentOccurrenceId()));
        backend.fail(PlaybackErrorCode::SourceUnavailable);
        QTRY_VERIFY(presenter.hasError());
        QCOMPARE(sink.entries.size(), std::size_t{2});
        QCOMPARE(sink.entries.back(), sink.entries.front());
    }

    void capturesAnAlreadyPresentErrorWhenBindingIsCreated() {
        TestPlatform platform;
        TestBackend backend;
        TestMetadataReader reader;
        RecordingSink sink;
        PlayerPresenter presenter(platform, backend, reader, [] { return std::vector{syntheticSource()}; });
        QVERIFY(presenter.openAudio());
        backend.fail(PlaybackErrorCode::AudioOutputChanged);
        QTRY_VERIFY(presenter.hasError());
        ShellDiagnosticBinding binding(presenter, sink);
        QCOMPARE(sink.entries, std::vector{entry(DiagnosticEvent::SourceOpenFailed, DiagnosticCode::AudioOutputChanged,
                                                 DiagnosticSeverity::Error)});
    }

    void recordsDisconnectedSourceWithoutItsPathOrDriveLetter() {
        TestPlatform platform;
        TestBackend backend;
        TestMetadataReader reader;
        RecordingSink sink;
        PlayerPresenter presenter(platform, backend, reader, [] { return std::vector{syntheticSource()}; });
        ShellDiagnosticBinding binding(presenter, sink);
        QVERIFY(presenter.openAudio());
        backend.fail(PlaybackErrorCode::SourceVolumeRemoved);
        QTRY_VERIFY(presenter.hasError());
        QCOMPARE(sink.entries.size(), std::size_t{1});
        QCOMPARE(formatDiagnosticRecord({.timestamp_utc_ms = 1, .entry = sink.entries.front()}),
                 std::string{"utc_ms=1 severity=error event=source_open_failed code=source_volume_removed count=1\n"});
    }

    void recordsAutomaticSkipWithoutExposingItsTitle() {
        TestPlatform platform;
        TestBackend backend;
        TestMetadataReader reader;
        RecordingSink sink;
        PlayerPresenter presenter(platform, backend, reader,
                                  [] { return std::vector{syntheticSource(), syntheticSource()}; });
        ShellDiagnosticBinding binding(presenter, sink);
        QVERIFY(presenter.openAudio());
        backend.fail(PlaybackErrorCode::InvalidMedia);
        QTRY_COMPARE(sink.entries.size(), std::size_t{1});
        QCOMPARE(sink.entries.front(), entry(DiagnosticEvent::SourceSkipped, DiagnosticCode::InvalidMedia));
        QVERIFY(!presenter.hasError());
    }

    void mapsOnlyClosedSkipCodes_data() {
        QTest::addColumn<QString>("code");
        QTest::addColumn<DiagnosticCode>("expected");
        QTest::newRow("missing") << QStringLiteral("source_unavailable") << DiagnosticCode::SourceUnavailable;
        QTest::newRow("invalid") << QStringLiteral("invalid_media") << DiagnosticCode::InvalidMedia;
        QTest::newRow("denied") << QStringLiteral("access_denied") << DiagnosticCode::AccessDenied;
        QTest::newRow("backend") << QStringLiteral("backend_failure") << DiagnosticCode::BackendFailure;
        QTest::newRow("output-missing") << QStringLiteral("audio_output_unavailable")
                                        << DiagnosticCode::AudioOutputUnavailable;
        QTest::newRow("output-changed") << QStringLiteral("audio_output_changed") << DiagnosticCode::AudioOutputChanged;
        QTest::newRow("unknown-private") << QStringLiteral("https://private.invalid/file?secret=value")
                                         << DiagnosticCode::Unknown;
    }

    void mapsOnlyClosedSkipCodes() {
        QFETCH(QString, code);
        QFETCH(DiagnosticCode, expected);
        TestPlatform platform;
        RecordingSink sink;
        PlayerPresenter presenter(frozenBundle(), platform);
        ShellDiagnosticBinding binding(presenter, sink);
        presenter.occurrenceAutomaticallySkipped(QStringLiteral("PRIVATE title %2 C:/user/file 日本"), code);
        QCOMPARE(sink.entries, std::vector{entry(DiagnosticEvent::SourceSkipped, expected)});
        const auto record = formatDiagnosticRecord({.timestamp_utc_ms = 1, .entry = sink.entries.front()});
        QVERIFY(record.find("PRIVATE") == std::string::npos);
        QVERIFY(record.find("secret") == std::string::npos);
        QVERIFY(record.find("日本") == std::string::npos);
    }

    void recordsEachSeekRejectionWithoutInventingAnUnavailableCause() {
        TestPlatform platform;
        RecordingSink sink;
        PlayerPresenter presenter(frozenBundle(), platform);
        ShellDiagnosticBinding binding(presenter, sink);
        presenter.seekRejected();
        presenter.seekRejected();
        QCOMPARE(sink.entries, (std::vector{entry(DiagnosticEvent::SeekRejected, DiagnosticCode::Unknown),
                                            entry(DiagnosticEvent::SeekRejected, DiagnosticCode::Unknown)}));
    }

    void initialPreferenceWarningsAreClosedAndDeduplicated_data() {
        QTest::addColumn<PreferencesLoadStatus>("status");
        QTest::addColumn<DiagnosticCode>("expected");
        QTest::newRow("backup") << PreferencesLoadStatus::RecoveredFromBackup << DiagnosticCode::CorruptState;
        QTest::newRow("defaults") << PreferencesLoadStatus::InvalidUsingDefaults << DiagnosticCode::CorruptState;
        QTest::newRow("future") << PreferencesLoadStatus::FutureVersion << DiagnosticCode::UnsupportedVersion;
    }

    void initialPreferenceWarningsAreClosedAndDeduplicated() {
        QFETCH(PreferencesLoadStatus, status);
        QFETCH(DiagnosticCode, expected);
        TestPlatform platform;
        TestPreferencesWriter writer;
        PreferencesAutosave autosave(writer, Preferences{});
        RecordingSink sink;
        PlayerPresenter presenter(frozenBundle(), platform, {.load_status = status, .autosave = &autosave});
        ShellDiagnosticBinding binding(presenter, sink);
        QCOMPARE(sink.entries, std::vector{entry(DiagnosticEvent::PreferencesWarning, expected)});
        presenter.preferencePersistenceChanged();
        binding.refresh();
        QCOMPARE(sink.entries.size(), std::size_t{1});
    }

    void combinedInitialPersistenceWarningKeepsTheTwoClosedScopes() {
        TestPlatform platform;
        TestBackend backend;
        TestMetadataReader reader;
        TestPreferencesWriter preferences_writer;
        PreferencesAutosave preferences_autosave(preferences_writer, Preferences{});
        TestSessionWriter session_writer;
        SessionAutosave session_autosave(session_writer, SessionSnapshot{});
        RecordingSink sink;
        PlayerPresenter presenter(
            platform, backend, reader, [] { return std::vector<std::filesystem::path>{}; },
            {.load_status = PreferencesLoadStatus::InvalidUsingDefaults,
             .autosave = &preferences_autosave,
             .session = {.load_status = SessionLoadStatus::InvalidUsingDefaults, .autosave = &session_autosave}});
        QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::CombinedPersistence);
        ShellDiagnosticBinding binding(presenter, sink);
        QCOMPARE(sink.entries, (std::vector{entry(DiagnosticEvent::PreferencesWarning, DiagnosticCode::CorruptState),
                                            entry(DiagnosticEvent::SessionWarning, DiagnosticCode::CorruptState)}));
        presenter.preferencePersistenceChanged();
        QCOMPARE(sink.entries.size(), std::size_t{2});
    }

    void observesNewPersistenceFailureWithoutRepeatingItOnLaterTicks() {
        TestPlatform platform;
        TestPreferencesWriter writer;
        PreferencesAutosave autosave(writer, Preferences{}, {.debounce = std::chrono::milliseconds::zero()});
        RecordingSink sink;
        PlayerPresenter presenter(frozenBundle(), platform, {.autosave = &autosave});
        ShellDiagnosticBinding binding(presenter, sink);
        QVERIFY(sink.entries.empty());
        QVERIFY(presenter.toggleMute());
        QTRY_COMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);
        QCOMPARE(sink.entries, std::vector{entry(DiagnosticEvent::PreferencesWarning, DiagnosticCode::WriteFailed)});
        presenter.preferencePersistenceChanged();
        binding.refresh();
        QCOMPARE(sink.entries.size(), std::size_t{1});
    }

    void rejectedSinkDoesNotChangePlayerAndDestroyedBindingDisconnects() {
        TestPlatform platform;
        RecordingSink sink;
        sink.accept = false;
        PlayerPresenter presenter(frozenBundle(), platform);
        {
            ShellDiagnosticBinding binding(presenter, sink);
            presenter.seekRejected();
            QCOMPARE(sink.entries.size(), std::size_t{1});
            QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
        }
        presenter.seekRejected();
        presenter.occurrenceAutomaticallySkipped(QStringLiteral("private"), QStringLiteral("invalid_media"));
        QCOMPARE(sink.entries.size(), std::size_t{1});
    }

    void sinkMayDestroyTheBindingDuringNotification() {
        TestPlatform platform;
        RecordingSink sink;
        PlayerPresenter presenter(frozenBundle(), platform);
        auto binding = std::make_unique<ShellDiagnosticBinding>(presenter, sink);
        sink.on_submit = [&binding] { binding.reset(); };
        presenter.seekRejected();
        QVERIFY(!binding);
        QCOMPARE(sink.entries.size(), std::size_t{1});
        presenter.seekRejected();
        QCOMPARE(sink.entries.size(), std::size_t{1});
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::ShellDiagnosticBindingTests)

#include "ShellDiagnosticBindingTests.moc"
