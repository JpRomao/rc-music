#include "../../qt/support/YoutubeResolverTestSupport.hpp"
#include "adapters/DownloadJobsModel.hpp"
#include "adapters/QtJsonWindowsSessionStore.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/YoutubeDownloadExecution.hpp"
#include "m0/adapters/SingleWorkerSessionWriter.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/WindowsPlayerDownloads.hpp"

#ifdef RC_MUSIC_TEST_YOUTUBE_OWNER_HOST_PATH
#include "NativeHostBuildConfiguration.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/YoutubeAudioCodec.hpp"
#endif

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
#error "Player download composition must use the production UI ABI without fixture scenarios"
#endif

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QtEndian>

#include <Sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

using namespace music_player::m0;
using namespace std::chrono_literals;

namespace {
// Only fresh directories inside this fixture receive ACLs. Document stores
// require these two exact ACEs without inheritance flags; no host ACL changes.
bool privateDirectory(const QString& path) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token{raw};
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    if (!size || size > 64 * 1024)
        return false;
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
        return false;
    const auto free_sid = qScopeGuard([&] { LocalFree(sid); });
    const auto text = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != FALSE;
}

QByteArray fileHash(const QString& path) {
    QFile file{path};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}

QStringList entries(const QString& directory) {
    return QDir{directory}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer timer;
    timer.start();
    do {
        if (ready())
            return true;
        QTest::qWait(5);
    } while (timer.elapsed() < 15'000);
    return ready();
}

struct Gate final {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> entered{false};
    bool released{false};
    void hold() {
        std::unique_lock lock{mutex};
        entered = true;
        changed.wait(lock, [&] { return released; });
    }
    void release() {
        const std::lock_guard lock{mutex};
        released = true;
        changed.notify_all();
    }
};

struct Observation final {
    Gate admission_ack;
    Gate archive_ack;
    Gate session_ack;
    std::atomic<bool> admission_saved{false};
    std::atomic<bool> factory_before_saved{false};
    std::atomic<bool> factory_on_ui{false};
    std::atomic<bool> checkpoint_on_ui{false};
    std::atomic<bool> checkpoint_destroyed{false};
    std::atomic<bool> fail_session_before_commit{false};
    std::atomic<int> factories{0};
    std::atomic<int> resolvers{0};
    std::atomic<bool> resolver_before_saved{false}, resolver_on_ui{false};
    std::atomic<int> session_failures{0};
    void release() {
        admission_ack.release();
        archive_ack.release();
        session_ack.release();
        fail_session_before_commit = false;
    }
};

// The resolver port alone is synthetic. Capability decoding, worker routing,
// local framed transfer, MF conversion, publication and persistence stay real.
class ObservedYoutubeResolver final : public YoutubeResolverProcess {
  public:
    explicit ObservedYoutubeResolver(std::shared_ptr<Observation> state) : state_(std::move(state)) {}
    bool start(const QString& video, qint64) override {
        ++state_->resolvers;
        if (!state_->admission_saved)
            state_->resolver_before_saved = true;
        state_->resolver_on_ui = QThread::currentThread() == QCoreApplication::instance()->thread();
        auto object =
            QJsonDocument::fromJson(
                music_player::m0::test::youtubeResolverFixtureJson(
                    video,
                    QStringLiteral("https://r1.googlevideo.com/videoplayback?expire=2000000000&fixture=synthetic-mp4")))
                .object();
        // The general fixture's 1024 is not the AAC fixture's byte count.
        // An absent optional estimate lets the real HTTP length govern quotas.
        object.remove(QStringLiteral("filesize"));
        output_ = QJsonDocument{object}.toJson(QJsonDocument::Compact);
        return true;
    }
    YoutubeResolverProcessUpdate poll(qint64) override {
        return cancelled_ ? YoutubeResolverProcessUpdate{.state = YoutubeResolverProcessState::Cancelled}
                          : YoutubeResolverProcessUpdate{
                                .state = YoutubeResolverProcessState::Exited, .stdout_chunk = output_, .exit_code = 0};
    }
    void cancel() override { cancelled_ = true; }

  private:
    std::shared_ptr<Observation> state_;
    QByteArray output_;
    bool cancelled_{};
};

// The real store commits first; only its Saved acknowledgement is withheld.
// This distinguishes durable bytes/readback from the admission owner's ACK.
class ObservedCheckpointStore final : public DownloadCheckpointStore {
  public:
    ObservedCheckpointStore(QString directory, std::shared_ptr<Observation> state)
        : real_(std::move(directory)), state_(std::move(state)) {
        state_->checkpoint_on_ui = QThread::currentThread() == QCoreApplication::instance()->thread();
    }
    ~ObservedCheckpointStore() override { state_->checkpoint_destroyed = true; }
    DownloadCheckpointLoadResult load() override { return real_.load(); }
    DownloadCheckpointSaveStatus save(const DownloadCheckpoint& checkpoint) override {
        const auto saved = real_.save(checkpoint);
        if (saved == DownloadCheckpointSaveStatus::Saved && !checkpoint.receipts.empty()) {
            state_->admission_ack.hold();
            state_->admission_saved = true;
        }
        if (saved == DownloadCheckpointSaveStatus::Saved && !checkpoint.archived_receipts.empty())
            state_->archive_ack.hold();
        return saved;
    }

  private:
    WindowsDownloadCheckpointStore real_;
    std::shared_ptr<Observation> state_;
};

class Platform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return false; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
};

class Audio final : public PlaybackBackend {
  public:
    const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path&) override {
        ++loads;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override {
        ++plays;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult pause() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }
    int loads{0};
    int plays{0};

  private:
    PlaybackSnapshot snapshot_{};
};

class Metadata final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest) override {
        ++reads;
        return MetadataCommandResult::Accepted;
    }
    MetadataCommandResult cancelRead(MetadataGeneration) override { return MetadataCommandResult::Accepted; }
    MetadataPollResult poll(MetadataGeneration) override { return {}; }
    std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
    int reads{0};
};

class PreferencesWriterStub final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest request) override {
        completion_ = PreferencesWriteCompletion{request.revision, PreferencesSaveStatus::Saved};
        return PreferencesWriterCommand::Accepted;
    }
    std::optional<PreferencesWriteCompletion> poll() override { return std::exchange(completion_, std::nullopt); }
    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        return PreferencesWriteCompletion{request.revision, PreferencesSaveStatus::Saved};
    }
    bool shutdown() override { return true; }

  private:
    std::optional<PreferencesWriteCompletion> completion_;
};

Preferences initialPreferences() {
    Preferences preferences;
    preferences.restore_last_session = false;
    return preferences;
}

struct Files final {
    QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/RC Music player downloads espaço-音-XXXXXX")};
    QString documents{temporary.filePath(QStringLiteral("documents"))};
    QString network_parent{temporary.filePath(QStringLiteral("network runtimes"))};
    QString media_parent{temporary.filePath(QStringLiteral("media runtimes"))};
    QString staging{temporary.filePath(QStringLiteral("staging"))};
    QString publication{temporary.filePath(QStringLiteral("published"))};
    WindowsDownloadNetworkLaunchConfig network;
    WindowsMediaFoundationLaunchConfig media;
    WindowsDownloadJobSessionConfig job;

    bool initialize() {
        if (!temporary.isValid() || !privateDirectory(documents))
            return false;
        for (const auto& path : {network_parent, media_parent, staging, publication})
            if (!QDir{}.mkdir(path))
                return false;
        const QString crt = QString::fromUtf8(RC_MUSIC_TEST_CRT_DIR);
        const std::array<QString, download_launch_artifact_count> sources{
            QString::fromUtf8(RC_MUSIC_TEST_IO_HELPER_PATH), QString::fromUtf8(RC_MUSIC_TEST_PAYLOAD_PATH),
            QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH),  QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH),
            crt + QStringLiteral("/msvcp140.dll"),           crt + QStringLiteral("/msvcp140_1.dll"),
            crt + QStringLiteral("/vcruntime140.dll"),       crt + QStringLiteral("/vcruntime140_1.dll"),
            QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH)};
        network.runtime_parent = network_parent;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            network.artifacts[index] = {sources[index], fileHash(sources[index])};
            if (network.artifacts[index].sha256.size() != 64)
                return false;
        }
        const auto worker = QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH);
        media = {media_parent, worker, fileHash(worker)};
        job.workspace = {staging, publication, 2 * 1024 * 1024};
        job.converter.backend = WindowsLocalMediaBackend::MediaFoundation;
        job.total_timeout_ms = 15'000;
        return media.source_sha256.size() == 64;
    }
    bool sourcesPreserved() const {
        return fileHash(media.source_executable) == media.source_sha256 &&
               std::ranges::all_of(network.artifacts,
                                   [](const auto& artifact) { return fileHash(artifact.path) == artifact.sha256; });
    }
    bool transientParentsEmpty() const {
        return entries(network_parent).isEmpty() && entries(media_parent).isEmpty() && entries(staging).isEmpty();
    }
};

DownloadRequest request(const QString& suffix, const bool stall = false) {
    return {"01234567-89ab-4cde-8fab-0123456789ab",
            {{"https://media.example.test/" + std::string(stall ? "stall-after-data-" : "") + "synthetic-" +
                  suffix.toStdString(),
              "../../Synthetic & not-a-command.mp3",
              suffix == QStringLiteral("mp4") ? DownloadMediaKind::Video : DownloadMediaKind::Audio}}};
}

struct Player final {
    Files& files;
    std::shared_ptr<Observation> observation;
    Platform platform;
    Audio audio;
    Metadata metadata;
    PreferencesWriterStub preferences_writer;
    PreferencesAutosave preferences;
    QtJsonWindowsSessionStore session_store;
    SessionLoadResult loaded;
    SingleWorkerSessionWriter writer;
    SessionAutosave autosave;
    PlayerPresenter presenter;
    std::unique_ptr<WindowsPlayerDownloads> owner;
    bool started{false};

    explicit Player(Files& paths, std::shared_ptr<Observation> state = std::make_shared<Observation>(),
                    std::optional<WindowsPlayerNativeIngressOptions> ingress = {}, bool youtube = false)
        : files(paths), observation(std::move(state)), preferences(preferences_writer, initialPreferences()),
          session_store(files.documents, {.inject_fault =
                                              [state = observation](SessionStoreFaultPoint point) {
                                                  if (point == SessionStoreFaultPoint::BeforePrimaryCommit &&
                                                      state->fail_session_before_commit) {
                                                      ++state->session_failures;
                                                      return true;
                                                  }
                                                  if (point == SessionStoreFaultPoint::AfterPrimaryCommit)
                                                      state->session_ack.hold();
                                                  return false;
                                              }}),
          loaded(session_store.load()), writer(session_store),
          autosave(writer, loaded.snapshot, {.structural_debounce = 0ms}),
          presenter(platform, audio, metadata, [] { return std::vector<std::filesystem::path>{}; },
                    {.initial = initialPreferences(),
                     .autosave = &preferences,
                     .session = {.initial = loaded.snapshot, .load_status = loaded.status, .autosave = &autosave}}) {
        WindowsDownloadExecution::TaskFactory destination = [state = observation, &paths] {
            ++state->factories;
            if (!state->admission_saved)
                state->factory_before_saved = true;
            if (QThread::currentThread() == QCoreApplication::instance()->thread())
                state->factory_on_ui = true;
            // Every real preparer, the journal and the task are owned by the
            // execution worker. No prepared capability crosses from this UI.
            return makeWindowsOwnedPreparedDownloadTask(paths.network, paths.media, paths.job, paths.documents);
        };
        auto execution = youtube
                             ? makeWindowsMediaAndYoutubeExecution(
                                   [state = observation] { return std::make_unique<ObservedYoutubeResolver>(state); },
                                   destination, [] { return qint64{1'999'999'000}; })
                             : std::make_unique<WindowsDownloadExecution>(destination);
        owner = std::make_unique<WindowsPlayerDownloads>(
            presenter, loaded, std::move(execution),
            [state = observation, directory = files.documents] {
                return std::make_unique<ObservedCheckpointStore>(directory, state);
            },
            WindowsPlayerDownloadsOptions{.native_ingress = std::move(ingress)});
    }
    ~Player() {
        observation->release();
        if (started && !owner->stopped() && !drain())
            QTest::qFail("Player download owner did not drain before fixture destruction", __FILE__, __LINE__);
        owner.reset();
        static_cast<void>(writer.shutdown());
    }
    bool ready() {
        started = owner->start();
        return started && waitFor([&] {
                   return owner->binding()->statusText() ==
                          DownloadApplicationBinding::tr("Histórico de downloads carregado");
               });
    }
    QAbstractItemModel* jobs() const { return owner->binding()->jobsModel(); }
    QString state(int row = 0) const {
        return jobs()->data(jobs()->index(row, 0), DownloadJobsModel::StateRole).toString();
    }
    QString jobId(int row = 0) const {
        return jobs()->data(jobs()->index(row, 0), DownloadJobsModel::JobIdRole).toString();
    }
    bool drain() {
        observation->release();
        if (!presenter.exiting() && !presenter.requestExit())
            return false;
        return waitFor([&] { return owner->stopped() && presenter.exitCommitted(); }) && writer.shutdown();
    }
};

bool publishedAndClean(const Files& files, const std::string& id) {
    WindowsDownloadOwnershipStore ownership{files.documents};
    const auto result = ownership.load();
    return result.status == DownloadCheckpointLoadStatus::Loaded && result.journal &&
           result.journal->records.size() == 1 && result.journal->records.front().job_id == id &&
           result.journal->records.front().stage == DownloadOwnershipStage::Published &&
           result.journal->records.front().cleanup_complete && entries(files.staging).isEmpty();
}
#ifdef RC_MUSIC_TEST_YOUTUBE_OWNER_HOST_PATH
class BrowserHost final {
  public:
    QProcess process;
    ~BrowserHost() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            static_cast<void>(process.waitForFinished(3'000));
        }
    }
    bool start(const QByteArray& frame) {
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QString::fromUtf8(RC_MUSIC_TEST_YOUTUBE_OWNER_HOST_PATH),
                      {nativeHostBuildConfiguration().allowed_origin, QStringLiteral("--parent-window=0")});
        if (!process.waitForStarted(3'000) || process.write(frame.first(2)) != 2)
            return false;
        QTest::qWait(10);
        if (process.write(frame.sliced(2, 3)) != 3)
            return false;
        QTest::qWait(10);
        // Stdin deliberately remains open during admission Pending.
        return process.write(frame.sliced(5)) == frame.size() - 5;
    }
    void read() {
        constexpr qsizetype maximum = 4'100;
        if (output.size() <= maximum)
            output += process.read(maximum + 1 - output.size());
        overflow = overflow || output.size() > maximum || process.bytesAvailable() > 0;
    }
    QByteArray output{};
    bool overflow{};
};
#endif
} // namespace

class PlayerDownloadsCompositionTests final : public QObject {
    Q_OBJECT

  private slots:
#ifdef RC_MUSIC_TEST_YOUTUBE_OWNER_HOST_PATH
    void nativeYoutubeHostUsesOwnedIngressAndCommitsBothSavedAcknowledgements() {
        Files files;
        QVERIFY(files.initialize());
        QVERIFY(QFileInfo::exists(QString::fromUtf8(RC_MUSIC_TEST_YOUTUBE_OWNER_HOST_PATH)));
        const auto profile = nativeHostBuildConfiguration();
        QVERIFY(isValidNativeHostConfiguration(profile));
        const WindowsPlayerNativeIngressOptions profiles{.youtube = profile};
        const YoutubeAudioRequest selected{"01234567-89ab-4cde-8fab-0123456789ab",
                                           {{"aB0-_Cd1EF2", "Synthetic song", "Synthetic artist", "", false}}};
        const auto frame = encodeNativeMessageFrame(encodeYoutubeAudioRequest(selected));
        QVERIFY(!frame.isEmpty());
        std::vector<std::string> original_ids;
        SessionSnapshot committed;
        QByteArray final_hash;
        for (int run = 0; run < 2; ++run) {
            auto observation = std::make_shared<Observation>();
            if (run != 0)
                observation->release();
            Player player{files, observation, profiles, true};
            QVERIFY(player.ready());
            BrowserHost browser;
            QVERIFY(browser.start(frame));
            if (run == 0) {
                QVERIFY(waitFor([&] { return observation->admission_ack.entered.load(); }));
                browser.read();
                QVERIFY(browser.output.isEmpty());
                QVERIFY(!browser.overflow);
                QCOMPARE(browser.process.state(), QProcess::Running);
                QCOMPARE(observation->resolvers.load(), 0);
                QCOMPARE(observation->factories.load(), 0);
                QCOMPARE(player.presenter.queueModelObject()->rowCount(), 0);
                WindowsDownloadCheckpointStore checkpoint{files.documents};
                const auto saved_bytes = checkpoint.load();
                QCOMPARE(saved_bytes.status, DownloadCheckpointLoadStatus::Loaded);
                QVERIFY(saved_bytes.checkpoint);
                QCOMPARE(saved_bytes.checkpoint->receipts.size(), std::size_t{1});
                observation->admission_ack.release();
            }
            QVERIFY(waitFor([&] {
                browser.read();
                return browser.process.state() == QProcess::NotRunning || browser.overflow;
            }));
            browser.read();
            QVERIFY(!browser.overflow);
            QCOMPARE(browser.process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(browser.process.exitCode(), 0);
            QVERIFY(browser.process.readAllStandardError().isEmpty());
            QVERIFY(browser.output.size() > 4 && browser.output.size() <= 4'100);
            QCOMPARE(qFromLittleEndian<quint32>(browser.output.constData()),
                     static_cast<quint32>(browser.output.size() - 4));
            const auto admitted = decodeYoutubeAudioResponse(browser.output.sliced(4), selected.request_id, 1);
            QVERIFY(admitted);
            QCOMPARE(admitted->error, DownloadSubmissionError::None);
            if (run == 0)
                original_ids = admitted->job_ids;
            QCOMPARE(admitted->job_ids, original_ids);
            QCOMPARE(original_ids.size(), std::size_t{1});
            if (run == 0) {
                QVERIFY(waitFor([&] { return observation->session_ack.entered.load(); }));
                QCOMPARE(observation->resolvers.load(), 1);
                QCOMPARE(observation->factories.load(), 1);
                QVERIFY(!observation->resolver_before_saved);
                QVERIFY(!observation->resolver_on_ui);
                QVERIFY(!observation->factory_before_saved);
                QVERIFY(!observation->factory_on_ui);
                QVERIFY(!observation->checkpoint_on_ui);
                QCOMPARE(player.state(), QStringLiteral("importing"));
                QCOMPARE(player.presenter.queueModelObject()->rowCount(), 1);
                QCOMPARE(player.autosave.savedRevision(), SessionRevision{0});
                QtJsonWindowsSessionStore fresh_session{files.documents};
                const auto bytes_without_ack = fresh_session.load();
                QCOMPARE(bytes_without_ack.status, SessionLoadStatus::Loaded);
                QCOMPARE(bytes_without_ack.snapshot.download_import_receipts.size(), std::size_t{1});
                QCOMPARE(bytes_without_ack.snapshot.download_import_receipts.front().job_id, original_ids.front());
                QVERIFY(publishedAndClean(files, original_ids.front()));
                observation->session_ack.release();
                QVERIFY(waitFor([&] { return player.state() == QStringLiteral("completed"); }));
                QVERIFY(player.autosave.savedRevision() > 0);
                QCOMPARE(player.autosave.savedRevision(), player.autosave.latestRevision());
                committed = fresh_session.load().snapshot;
                QCOMPARE(committed, bytes_without_ack.snapshot);
                const auto final =
                    QString::fromStdWString(committed.download_import_receipts.front().source_path.wstring());
                QCOMPARE(QFileInfo{final}.absolutePath(), files.publication);
                QVERIFY(final.endsWith(QStringLiteral(".mp3")));
                QVERIFY(QFileInfo{final}.size() > 0);
                final_hash = fileHash(final);
                QCOMPARE(final_hash.size(), 64);
            } else {
                QCOMPARE(player.loaded.snapshot.download_import_receipts, committed.download_import_receipts);
                QCOMPARE(observation->resolvers.load(), 0);
                QCOMPARE(observation->factories.load(), 0);
                QCOMPARE(player.presenter.queueModelObject()->rowCount(), 0);
                QCOMPARE(player.autosave.savedRevision(), SessionRevision{0});
                QCOMPARE(
                    fileHash(QString::fromStdWString(committed.download_import_receipts.front().source_path.wstring())),
                    final_hash);
            }
            BrowserHost replay;
            QVERIFY(replay.start(frame));
            QVERIFY(waitFor([&] {
                replay.read();
                return replay.process.state() == QProcess::NotRunning || replay.overflow;
            }));
            replay.read();
            QVERIFY(!replay.overflow);
            QCOMPARE(replay.process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(replay.process.exitCode(), 0);
            QVERIFY(replay.process.readAllStandardError().isEmpty());
            QVERIFY(replay.output.size() > 4 && replay.output.size() <= 4'100);
            QCOMPARE(qFromLittleEndian<quint32>(replay.output.constData()),
                     static_cast<quint32>(replay.output.size() - 4));
            const auto repeated = decodeYoutubeAudioResponse(replay.output.sliced(4), selected.request_id, 1);
            QVERIFY(repeated);
            QCOMPARE(repeated->error, DownloadSubmissionError::None);
            QCOMPARE(repeated->job_ids, original_ids);
            QCOMPARE(observation->resolvers.load(), run == 0 ? 1 : 0);
            QCOMPARE(observation->factories.load(), run == 0 ? 1 : 0);
            QCOMPARE(player.jobs()->rowCount(), 1);
            QCOMPARE(player.presenter.queueModelObject()->rowCount(), run == 0 ? 1 : 0);
            QCOMPARE(player.audio.loads, 0);
            QCOMPARE(player.audio.plays, 0);
            QVERIFY(!player.presenter.hasCurrentTrack());
            QCOMPARE(entries(files.publication).size(), 1);
            WindowsDownloadCheckpointStore checkpoint{files.documents};
            const auto checkpoint_after_replay = checkpoint.load();
            QCOMPARE(checkpoint_after_replay.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(checkpoint_after_replay.checkpoint);
            QCOMPARE(checkpoint_after_replay.checkpoint->jobs.size(), std::size_t{1});
            QCOMPARE(checkpoint_after_replay.checkpoint->receipts.size(), std::size_t{1});
            QCOMPARE(checkpoint_after_replay.checkpoint->receipts.front().request_id, selected.request_id);
            QCOMPARE(checkpoint_after_replay.checkpoint->jobs.front().job_id, original_ids.front());
            QVERIFY(player.drain());
            QVERIFY(player.owner->stopped());
            QVERIFY(observation->checkpoint_destroyed);
        }
        QVERIFY(files.transientParentsEmpty());
        QVERIFY(files.sourcesPreserved());
    }
#endif
    void realPreparedWorkersImportOnlyAfterDurableAdmissionAndSessionSaved_data() {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<bool>("archive");
        QTest::newRow("MP3") << QStringLiteral("mp3") << false;
        QTest::newRow("MP4-AAC-archive") << QStringLiteral("mp4") << true;
    }
    void realPreparedWorkersImportOnlyAfterDurableAdmissionAndSessionSaved() {
        QFETCH(QString, suffix);
        QFETCH(bool, archive);
        Files files;
        QVERIFY(files.initialize());
        const auto batch = request(suffix);
        std::vector<std::string> admitted_ids;
        SessionSnapshot committed;
        QByteArray final_hash;
        {
            Player player{files};
            QCOMPARE(player.loaded.status, SessionLoadStatus::Missing);
            QCOMPARE(player.observation->factories.load(), 0);
            QVERIFY(files.transientParentsEmpty());
            QVERIFY(player.ready());
            QCOMPARE(player.owner->submission().begin(batch).state, DownloadAdmissionState::Pending);
            QVERIFY(waitFor([&] { return player.observation->admission_ack.entered.load(); }));
            QVERIFY(!player.owner->submission().poll(batch.request_id));
            QCOMPARE(player.observation->factories.load(), 0);
            QCOMPARE(player.presenter.queueModelObject()->rowCount(), 0);
            QVERIFY(files.transientParentsEmpty());
            WindowsDownloadCheckpointStore checkpoint{files.documents};
            const auto saved_bytes = checkpoint.load();
            QCOMPARE(saved_bytes.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(saved_bytes.checkpoint);
            QCOMPARE(saved_bytes.checkpoint->receipts.size(), std::size_t{1});
            player.observation->admission_ack.release();
            std::optional<DownloadSubmissionResult> admitted;
            QVERIFY(waitFor([&] {
                admitted = player.owner->submission().poll(batch.request_id);
                return admitted.has_value();
            }));
            QCOMPARE(admitted->error, DownloadSubmissionError::None);
            QCOMPARE(admitted->job_ids.size(), std::size_t{1});
            admitted_ids = admitted->job_ids;
            QVERIFY(waitFor([&] { return player.observation->session_ack.entered.load(); }));
            QCOMPARE(player.observation->factories.load(), 1);
            QVERIFY(!player.observation->factory_before_saved);
            QVERIFY(!player.observation->factory_on_ui);
            QVERIFY(!player.observation->checkpoint_on_ui);
            QCOMPARE(player.state(), QStringLiteral("importing"));
            QCOMPARE(player.presenter.queueModelObject()->rowCount(), 1);
            QCOMPARE(player.autosave.savedRevision(), SessionRevision{0});
            QtJsonWindowsSessionStore fresh_session{files.documents};
            const auto bytes_without_ack = fresh_session.load();
            QCOMPARE(bytes_without_ack.status, SessionLoadStatus::Loaded);
            QCOMPARE(bytes_without_ack.snapshot.queue.items.size(), std::size_t{1});
            QCOMPARE(bytes_without_ack.snapshot.download_import_receipts.size(), std::size_t{1});
            QVERIFY(publishedAndClean(files, admitted_ids.front()));
            player.observation->session_ack.release();
            QVERIFY(waitFor([&] { return player.state() == QStringLiteral("completed"); }));
            QVERIFY(player.autosave.savedRevision() > 0);
            QCOMPARE(player.autosave.savedRevision(), player.autosave.latestRevision());
            committed = fresh_session.load().snapshot;
            QCOMPARE(committed, bytes_without_ack.snapshot);
            QCOMPARE(committed.download_import_receipts.front().job_id, admitted_ids.front());
            const auto& final = committed.download_import_receipts.front().source_path;
            QCOMPARE(committed.queue.items.front().source_path, final);
            const auto final_path = QString::fromStdWString(final.wstring());
            QCOMPARE(QFileInfo{final_path}.absolutePath(), files.publication);
            QVERIFY(final_path.endsWith(QStringLiteral(".mp3")));
            QVERIFY(QFileInfo{final_path}.size() > 0);
            final_hash = fileHash(final_path);
            QCOMPARE(final_hash.size(), 64);
            const auto repeated = player.owner->submission().begin(batch);
            QCOMPARE(repeated.state, DownloadAdmissionState::Completed);
            QCOMPARE(repeated.result.error, DownloadSubmissionError::None);
            QCOMPARE(repeated.result.job_ids, admitted_ids);
            QTest::qWait(100);
            QCOMPARE(player.observation->factories.load(), 1);
            QCOMPARE(player.presenter.queueModelObject()->rowCount(), 1);
            QCOMPARE(player.audio.loads, 0);
            QCOMPARE(player.audio.plays, 0);
            QVERIFY(!player.presenter.hasCurrentTrack());
            if (archive) {
                auto* binding = player.owner->binding();
                QVERIFY(binding->canArchiveSettled());
                QVERIFY(binding->requestArchiveConfirmation());
                const auto token = binding->archiveConsentToken();
                QVERIFY(!token.isEmpty());
                QVERIFY(binding->confirmArchive(token));
                QVERIFY(waitFor([&] { return player.observation->archive_ack.entered.load(); }));
                QCOMPARE(player.jobs()->rowCount(), 1);
                const auto archived = checkpoint.load();
                QCOMPARE(archived.status, DownloadCheckpointLoadStatus::Loaded);
                QVERIFY(archived.checkpoint);
                QVERIFY(archived.checkpoint->jobs.empty());
                QCOMPARE(archived.checkpoint->archived_receipts.size(), std::size_t{1});
                QCOMPARE(archived.checkpoint->archived_receipts.front().job_ids, admitted_ids);
                player.observation->archive_ack.release();
                QVERIFY(waitFor([&] { return player.jobs()->rowCount() == 0; }));
                QCOMPARE(player.presenter.queueModelObject()->rowCount(), 1);
            }
            QVERIFY(player.drain());
            QVERIFY(player.owner->stopped());
            QVERIFY(player.observation->checkpoint_destroyed);
            QCOMPARE(player.platform.exits, 1);
        }
        QVERIFY(files.transientParentsEmpty());
        QVERIFY(files.sourcesPreserved());
        {
            auto observation = std::make_shared<Observation>();
            observation->release();
            Player restarted{files, observation};
            QCOMPARE(restarted.loaded.status, SessionLoadStatus::Loaded);
            QCOMPARE(restarted.loaded.snapshot.download_import_receipts, committed.download_import_receipts);
            QVERIFY(!restarted.presenter.restoreLastSession());
            QCOMPARE(restarted.presenter.queueModelObject()->rowCount(), 0);
            QCOMPARE(restarted.audio.loads, 0);
            QCOMPARE(restarted.metadata.reads, 0);
            QVERIFY(restarted.ready());
            const auto replay = restarted.owner->submission().begin(batch);
            QCOMPARE(replay.state, DownloadAdmissionState::Completed);
            QCOMPARE(replay.result.error, DownloadSubmissionError::None);
            QCOMPARE(replay.result.job_ids, admitted_ids);
            QTest::qWait(100);
            QCOMPARE(observation->factories.load(), 0);
            QCOMPARE(restarted.jobs()->rowCount(), archive ? 0 : 1);
            QCOMPARE(restarted.presenter.queueModelObject()->rowCount(), 0);
            QCOMPARE(restarted.audio.loads, 0);
            QCOMPARE(restarted.audio.plays, 0);
            QCOMPARE(restarted.metadata.reads, 0);
            QCOMPARE(restarted.autosave.savedRevision(), SessionRevision{0});
            QtJsonWindowsSessionStore fresh_session{files.documents};
            QCOMPARE(fresh_session.load().snapshot.download_import_receipts, committed.download_import_receipts);
            QCOMPARE(
                fileHash(QString::fromStdWString(committed.download_import_receipts.front().source_path.wstring())),
                final_hash);
            QCOMPARE(entries(files.publication).size(), 1);
            QVERIFY(restarted.drain());
            QVERIFY(observation->checkpoint_destroyed);
        }
        QVERIFY(files.transientParentsEmpty());
        QVERIFY(files.sourcesPreserved());
    }

    void failedRealSessionCommitRetriesImportWithoutExecutingOrConvertingAgain() {
        Files files;
        QVERIFY(files.initialize());
        auto observation = std::make_shared<Observation>();
        observation->release();
        observation->fail_session_before_commit = true;
        Player player{files, observation};
        QVERIFY(player.ready());
        const auto batch = request(QStringLiteral("mp4"));
        QCOMPARE(player.owner->submission().begin(batch).state, DownloadAdmissionState::Pending);
        QVERIFY(waitFor([&] { return player.state() == QStringLiteral("import_failed"); }));
        QCOMPARE(observation->factories.load(), 1);
        QVERIFY(observation->session_failures > 0);
        QCOMPARE(player.autosave.savedRevision(), SessionRevision{0});
        QCOMPARE(player.presenter.queueModelObject()->rowCount(), 1);
        QtJsonWindowsSessionStore fresh_session{files.documents};
        QVERIFY(fresh_session.load().snapshot.download_import_receipts.empty());
        const auto finals = entries(files.publication);
        QCOMPARE(finals.size(), 1);
        const auto final_hash = fileHash(files.publication + QLatin1Char('/') + finals.front());
        QVERIFY(publishedAndClean(files, player.jobId().toStdString()));
        observation->fail_session_before_commit = false;
        auto* model = qobject_cast<DownloadJobsModel*>(player.jobs());
        QVERIFY(model);
        QVERIFY(model->retryImport(player.jobId()));
        QVERIFY(waitFor([&] { return player.state() == QStringLiteral("completed"); }));
        QCOMPARE(observation->factories.load(), 1);
        QCOMPARE(player.presenter.queueModelObject()->rowCount(), 1);
        QCOMPARE(fresh_session.load().snapshot.download_import_receipts.size(), std::size_t{1});
        QVERIFY(player.autosave.savedRevision() > 0);
        QCOMPARE(entries(files.publication), finals);
        QCOMPARE(fileHash(files.publication + QLatin1Char('/') + finals.front()), final_hash);
        QCOMPARE(player.audio.loads, 0);
        QCOMPARE(player.audio.plays, 0);
        QVERIFY(player.drain());
        QVERIFY(files.transientParentsEmpty());
        QVERIFY(files.sourcesPreserved());
    }

    void exitCancelsStalledTransferAndDrainsWithoutSuccessorOrImport() {
        Files files;
        QVERIFY(files.initialize());
        auto observation = std::make_shared<Observation>();
        observation->release();
        Player player{files, observation};
        QVERIFY(player.ready());
        auto batch = request(QStringLiteral("mp3"), true);
        batch.items.push_back(request(QStringLiteral("mp4")).items.front());
        QCOMPARE(player.owner->submission().begin(batch).state, DownloadAdmissionState::Pending);
        QVERIFY(waitFor([&] {
            return observation->factories == 1 && player.state() == QStringLiteral("downloading") &&
                   player.jobs()
                           ->data(player.jobs()->index(0, 0), DownloadJobsModel::CompletedUnitsRole)
                           .toULongLong() > 0;
        }));
        // The helper resolves the ordinary host and stalls only on the transfer
        // request-target after delivering a partial body, without Terminal/EOF.
        // Real progress proves transfer reached the owned workspace; factory
        // admission or an elapsed delay cannot establish that. Its bytes are
        // local authenticated fixtures, without a socket or public TLS request.
        WindowsDownloadOwnershipStore pending_ownership{files.documents};
        const auto pending = pending_ownership.load();
        QCOMPARE(pending.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(pending.journal);
        QCOMPARE(pending.journal->records.size(), std::size_t{1});
        QCOMPARE(pending.journal->records.front().stage, DownloadOwnershipStage::WorkspaceReady);
        QVERIFY(!pending.journal->records.front().cleanup_complete);
        QCOMPARE(entries(files.staging).size(), 1);
        QCOMPARE(observation->factories.load(), 1);
        QCOMPARE(player.presenter.queueModelObject()->rowCount(), 0);
        QVERIFY(player.presenter.requestExit());
        QVERIFY(player.drain());
        QCOMPARE(observation->factories.load(), 1);
        QCOMPARE(player.state(0), QStringLiteral("cancelled"));
        QCOMPARE(player.state(1), QStringLiteral("cancelled"));
        QCOMPARE(player.presenter.queueModelObject()->rowCount(), 0);
        QCOMPARE(player.audio.loads, 0);
        QCOMPARE(player.audio.plays, 0);
        QVERIFY(entries(files.publication).isEmpty());
        QVERIFY(files.transientParentsEmpty());
        QVERIFY(observation->checkpoint_destroyed);
        QtJsonWindowsSessionStore fresh_session{files.documents};
        QVERIFY(fresh_session.load().snapshot.download_import_receipts.empty());
        WindowsDownloadCheckpointStore checkpoint{files.documents};
        const auto saved = checkpoint.load();
        QCOMPARE(saved.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(saved.checkpoint);
        QCOMPARE(saved.checkpoint->jobs.size(), std::size_t{2});
        QVERIFY(std::ranges::all_of(saved.checkpoint->jobs,
                                    [](const auto& job) { return job.state == DownloadJobState::Cancelled; }));
        WindowsDownloadOwnershipStore ownership{files.documents};
        const auto journal = ownership.load();
        QCOMPARE(journal.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(journal.journal);
        QCOMPARE(journal.journal->records.size(), std::size_t{1});
        QVERIFY(journal.journal->records.front().cleanup_complete);
        QCOMPARE(journal.journal->records.front().output_bytes, std::uint64_t{0});
        QCOMPARE(player.owner->submission().begin(batch).result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(files.sourcesPreserved());
    }
};

QTEST_MAIN(PlayerDownloadsCompositionTests)
#include "PlayerDownloadsCompositionTests.moc"
