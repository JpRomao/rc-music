#include "../../qt/support/YoutubeResolverTestSupport.hpp"
#include "adapters/DownloadJobsModel.hpp"
#include "adapters/NativeYoutubeAudioSession.hpp"
#include "adapters/QtJsonLyricsVariantsStore.hpp"
#include "adapters/QtLyricsService.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/YoutubeAudioCodec.hpp"
#include "adapters/YoutubeDownloadExecution.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "presentation/DownloadLyricsBinding.hpp"
#include "presentation/LyricsBinding.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/WindowsPlayerDownloads.hpp"

#ifdef RC_MUSIC_TEST_PLAYER_NATIVE_INGRESS
#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeYoutubeAudioIpcProtocol.hpp"
#include "adapters/QtNativeDownloadClient.hpp"
#include "adapters/QtNativeYoutubeAudioClient.hpp"
#include "adapters/WindowsNativeDownloadLease.hpp"
#include <QLocalServer>
#include <QLocalSocket>
#include <qt_windows.h>
#endif

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <cstring>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

using namespace music_player::m0;

namespace {
class LyricsReply final : public QNetworkReply {
  public:
    LyricsReply(const QNetworkRequest& request, QObject* parent, int status) : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {
        if (!isFinished()) {
            setFinished(true);
            setError(OperationCanceledError, "cancelled");
            emit finished();
        }
    }
    qint64 bytesAvailable() const override { return bytes_.size() - offset_ + QNetworkReply::bytesAvailable(); }

  protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = std::min(maximum, qint64{bytes_.size() - offset_});
        if (count <= 0)
            return isFinished() ? -1 : 0;
        std::memcpy(data, bytes_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    QByteArray bytes_{
        R"({"syncedLyrics":"[00:01.000]Synthetic source","plainLyrics":"Other source form","instrumental":false})"};
    qint64 offset_{};
};
class LyricsNetwork final : public QNetworkAccessManager {
  public:
    int calls{}, status{200};
    QUrl last_url;

  protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++calls;
        last_url = request.url();
        return new LyricsReply(request, this, status);
    }
};
DownloadRequest request() {
    return {"01234567-89ab-4cde-8fab-0123456789ab",
            {{"https://synthetic.example.test/known.mp3", "Synthetic", DownloadMediaKind::Audio}}};
}
NativeHostConfiguration nativeProfile(const char letter) {
    const auto image = QCoreApplication::applicationFilePath();
    return {QStringLiteral("chrome-extension://") + QString(32, QLatin1Char{letter}) + '/',
            QStringLiteral("rc-music-player-owner-") + QUuid::createUuid().toString(QUuid::Id128),
            {image},
            {image}};
}
WindowsPlayerNativeIngressOptions nativeProfiles() { return {nativeProfile('a'), nativeProfile('b')}; }
class Platform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
};
class Backend final : public PlaybackBackend {
  public:
    const PlaybackSnapshot& snapshot() const noexcept override { return state; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path&) override {
        ++loads;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult pause() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }
    PlaybackSnapshot state{};
    int loads{0};
};
class Metadata final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest) override { return MetadataCommandResult::Accepted; }
    MetadataCommandResult cancelRead(MetadataGeneration) override { return MetadataCommandResult::Accepted; }
    MetadataPollResult poll(MetadataGeneration) override { return {}; }
    std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
};
class PreferencesWriterStub final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest) override { return PreferencesWriterCommand::Accepted; }
    std::optional<PreferencesWriteCompletion> poll() override { return std::nullopt; }
    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest value) override {
        return PreferencesWriteCompletion{.revision = value.revision, .status = PreferencesSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
};
class SessionWriterStub final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest value) override {
        if (active)
            return SessionWriterCommand::Busy;
        active = std::move(value);
        return SessionWriterCommand::Accepted;
    }
    std::optional<SessionWriteCompletion> poll() override { return std::exchange(completion, std::nullopt); }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest value) override {
        return SessionWriteCompletion{.revision = value.revision, .status = SessionSaveStatus::Saved};
    }
    bool shutdown() override { return !active; }
    void save() {
        disk = active->snapshot;
        completion = SessionWriteCompletion{.revision = active->revision, .status = SessionSaveStatus::Saved};
        active.reset();
    }
    std::optional<SessionWriteRequest> active;
    std::optional<SessionWriteCompletion> completion;
    SessionSnapshot disk{};
};
struct CheckpointState final {
    std::mutex mutex;
    std::condition_variable changed;
    bool allow_save{true};
    std::atomic<bool> fail_save{false};
    std::atomic<int> loads{0}, saves{0};
    std::atomic<bool> destroyed{false};
    std::optional<DownloadCheckpoint> disk;
    void allow(bool value) {
        {
            const std::lock_guard lock(mutex);
            allow_save = value;
        }
        changed.notify_all();
    }
};
class CheckpointStore final : public DownloadCheckpointStore {
  public:
    explicit CheckpointStore(std::shared_ptr<CheckpointState> value) : state_(std::move(value)) {}
    ~CheckpointStore() override { state_->destroyed = true; }
    DownloadCheckpointLoadResult load() override {
        ++state_->loads;
        const std::lock_guard lock(state_->mutex);
        return state_->disk ? DownloadCheckpointLoadResult{state_->disk, DownloadCheckpointLoadStatus::Loaded}
                            : DownloadCheckpointLoadResult{};
    }
    DownloadCheckpointSaveStatus save(const DownloadCheckpoint& value) override {
        ++state_->saves;
        std::unique_lock lock(state_->mutex);
        state_->changed.wait(lock, [&] { return state_->allow_save; });
        if (state_->fail_save)
            return DownloadCheckpointSaveStatus::Failed;
        state_->disk = value;
        return DownloadCheckpointSaveStatus::Saved;
    }

  private:
    std::shared_ptr<CheckpointState> state_;
};
struct TaskState final {
    std::atomic<int> starts{0};
    std::atomic<bool> complete{true}, cancelled{false}, allow_cancel_terminal{true};
    std::function<void()> during_cancel{};
};
class Task final : public WindowsDownloadTask {
  public:
    explicit Task(std::shared_ptr<TaskState> value) : state_(std::move(value)) {}
    bool start(const std::string& id, const DownloadItem&, qint64) override {
        ++state_->starts;
        id_ = id;
        return true;
    }
    bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> cap, qint64 now, qint64,
                              const QString&) override {
        return cap && start(cap->jobId(), {}, now);
    }
    void cancel() override {
        if (state_->during_cancel)
            state_->during_cancel();
        state_->cancelled = true;
    }
    DownloadExecutionUpdate poll(qint64) override {
        if (state_->cancelled && state_->allow_cancel_terminal)
            return {.job_id = id_, .state = DownloadExecutionState::Cancelled};
        if (!state_->cancelled && state_->complete)
            return {.job_id = id_,
                    .state = DownloadExecutionState::Published,
                    .phase = DownloadPhase::Publishing,
                    .published_mp3 = std::filesystem::path{L"C:/synthetic-published/final.mp3"}};
        return {.job_id = id_};
    }

  private:
    std::shared_ptr<TaskState> state_;
    std::string id_;
};
#ifdef RC_MUSIC_TEST_PLAYER_NATIVE_INGRESS
struct NativePeerState final {
    std::atomic<bool> released{true};
    std::atomic<int> begins{0}, shutdowns{0};
    QStringList images{};
};
class NativePeer final : public NativeDownloadPeerCheck {
  public:
    explicit NativePeer(std::shared_ptr<NativePeerState> state) : state_(std::move(state)) {}
    bool begin(qintptr pipe, NativeDownloadPeerSide, const QStringList& images) override {
        ++state_->begins;
        state_->images = images;
        return pipe != 0 && pipe != -1;
    }
    NativeDownloadPeerState poll() override { return NativeDownloadPeerState::Eligible; }
    void shutdown() noexcept override { ++state_->shutdowns; }
    bool stopped() override { return state_->released; }

  private:
    std::shared_ptr<NativePeerState> state_;
};
NativeDownloadPeerCheckFactory nativePeerFactory(std::shared_ptr<NativePeerState> state = {}) {
    if (!state)
        state = std::make_shared<NativePeerState>();
    return [state] { return std::make_unique<NativePeer>(state); };
}
struct ReleaseNativePeer final {
    std::shared_ptr<NativePeerState> state;
    ~ReleaseNativePeer() { state->released = true; }
};
bool nativeEndpointOpenable(const QString& endpoint) {
    const auto path = (QStringLiteral("\\\\.\\pipe\\") + endpoint).toStdWString();
    const auto pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(pipe);
    return true;
}
#endif
struct Harness final {
    Platform platform;
    Backend backend;
    Metadata metadata;
    SessionWriterStub session_writer;
    PreferencesWriterStub preferences_writer;
    PreferencesAutosave preferences{preferences_writer, {}};
    SessionAutosave autosave{session_writer, {}};
    PlayerPresenter presenter{platform,
                              backend,
                              metadata,
                              [] { return std::vector<std::filesystem::path>{}; },
                              {.autosave = &preferences, .session = {.autosave = &autosave}}};
    std::shared_ptr<CheckpointState> checkpoint{std::make_shared<CheckpointState>()};
    std::shared_ptr<TaskState> task{std::make_shared<TaskState>()};
    std::unique_ptr<WindowsPlayerDownloads> owner;
    explicit Harness(bool youtube = false, LyricsBinding* lyrics_writer = nullptr,
                     QNetworkAccessManager* lyrics_network = nullptr, bool automatic_polling = false,
                     std::optional<WindowsPlayerNativeIngressOptions> native_ingress = {},
                     bool embedded_browser_ingress = false) {
        WindowsDownloadExecution::TaskFactory destination = [state = task] { return std::make_unique<Task>(state); };
        auto execution =
            youtube ? makeWindowsMediaAndYoutubeExecution(
                          [] { return std::make_unique<music_player::m0::test::FixtureYoutubeResolverProcess>(); },
                          destination, [] { return qint64{1'999'999'000}; })
                    : std::make_unique<WindowsDownloadExecution>(destination);
        WindowsPlayerDownloadsOptions options{.job_namespace = "owner",
                                              .binding = {.automatic_polling = automatic_polling},
                                              .lyrics_writer = lyrics_writer,
                                              .lyrics_network = lyrics_network,
                                              .native_ingress = std::move(native_ingress)};
        if (embedded_browser_ingress)
            options.embedded_browser_ingress = true;
        owner = std::make_unique<WindowsPlayerDownloads>(
            presenter, SessionLoadResult{}, std::move(execution),
            [state = checkpoint] { return std::make_unique<CheckpointStore>(state); }, std::move(options));
    }
    ~Harness() {
        // Failed assertions must not strand a deliberately held worker.
        checkpoint->allow(true);
        task->allow_cancel_terminal = true;
        task->complete = true;
    }
    void poll() {
        music_player::m0::test::PlayerPresenterTestAccess::pollPersistence(presenter);
        owner->poll();
    }
    bool wait(const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        do {
            poll();
            if (ready())
                return true;
            QTest::qWait(5);
        } while (timer.elapsed() < 4000);
        return false;
    }
    bool ready() {
        if (!owner->start())
            return false;
        return wait([&] {
            return checkpoint->loads == 1 &&
                   owner->binding()->statusText() == DownloadApplicationBinding::tr("Histórico de downloads carregado");
        });
    }
    QString state(int row = 0) const {
        auto* model = owner->binding()->jobsModel();
        return model->data(model->index(row, 0), DownloadJobsModel::StateRole).toString();
    }
    bool drain() {
        static_cast<void>(presenter.requestExit());
        return wait([&] {
            if (session_writer.active)
                session_writer.save();
            return owner->stopped();
        });
    }
};
} // namespace

class WindowsPlayerDownloadsTests final : public QObject {
    Q_OBJECT
  private slots:
    void embeddedBrowserAdmissionRequiresOptInAndClosesAtExit_data() {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("default-off") << false;
        QTest::newRow("trusted-opt-in") << true;
    }
    void embeddedBrowserAdmissionRequiresOptInAndClosesAtExit() {
        QFETCH(bool, enabled);
        bool observed_exit = false;
        bool available_during_exit = false;
        Harness h(false, nullptr, nullptr, false, {}, enabled);
        QCOMPARE(h.owner->embeddedBrowserSubmission(), nullptr);
        QCOMPARE(h.checkpoint->loads.load(), 0);
        QVERIFY(h.ready());
        auto* const admission = h.owner->embeddedBrowserSubmission();
        QCOMPARE(admission != nullptr, enabled);
        if (admission) {
            auto youtube = request();
            youtube.items.front().youtube = YoutubeAudioItem{.video_id = "syntheticID"};
            QCOMPARE(admission->begin(youtube).result.error, DownloadSubmissionError::UnsupportedSource);
        }

        connect(&h.presenter, &PlayerPresenter::lifecycleChanged, this, [&] {
            if (h.presenter.exiting()) {
                observed_exit = true;
                available_during_exit = available_during_exit || h.owner->embeddedBrowserSubmission() != nullptr;
            }
        });
        QVERIFY(h.presenter.requestExit());
        QVERIFY(observed_exit);
        QVERIFY(!available_during_exit);
        QCOMPARE(h.owner->embeddedBrowserSubmission(), nullptr);
        if (admission)
            QCOMPARE(admission->begin(request()).result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(h.drain());
        QCOMPARE(h.task->starts.load(), 0);
    }
    void embeddedBrowserAdmissionStaysUnavailableWhenStartFails() {
        Harness h(false, nullptr, nullptr, false, {}, true);
        QVERIFY(h.presenter.setExitPreparationCallbacks(
            {.begin = [] {}, .poll = [] { return ExitPreparationState::Ready; }}));
        QVERIFY(!h.owner->start());
        QCOMPARE(h.owner->embeddedBrowserSubmission(), nullptr);
        QCOMPARE(h.checkpoint->loads.load(), 0);
    }
#ifdef RC_MUSIC_TEST_PLAYER_NATIVE_INGRESS
    void nativeProfilesArePassiveAndOwnTheirCapturedTransportStringsBeforeStart() {
        auto profiles = nativeProfiles();
        const auto principal = profiles.principal;
        const auto youtube = profiles.youtube;
        auto origin_units = youtube.allowed_origin.toStdU16String();
        auto endpoint_units = youtube.endpoint_name.toStdU16String();
        auto image_units = youtube.expected_host_images.front().toStdU16String();
        profiles.youtube.allowed_origin = QString::fromRawData(reinterpret_cast<const QChar*>(origin_units.data()),
                                                               static_cast<qsizetype>(origin_units.size()));
        profiles.youtube.endpoint_name = QString::fromRawData(reinterpret_cast<const QChar*>(endpoint_units.data()),
                                                              static_cast<qsizetype>(endpoint_units.size()));
        profiles.youtube.expected_host_images = {QString::fromRawData(
            reinterpret_cast<const QChar*>(image_units.data()), static_cast<qsizetype>(image_units.size()))};
        const auto principal_peer = std::make_shared<NativePeerState>();
        const auto youtube_peer = std::make_shared<NativePeerState>();
        profiles.principal_peer_check_factory = nativePeerFactory(principal_peer);
        profiles.youtube_peer_check_factory = nativePeerFactory(youtube_peer);
        Harness h(true, nullptr, nullptr, false, profiles);
        WindowsNativeDownloadLease principal_probe, youtube_probe;
        QVERIFY(principal_probe.acquire(principal.endpoint_name));
        QVERIFY(youtube_probe.acquire(youtube.endpoint_name));
        principal_probe.reset();
        youtube_probe.reset();
        QLocalServer endpoint_probe;
        QVERIFY(endpoint_probe.listen(principal.endpoint_name));
        endpoint_probe.close();
        QVERIFY(endpoint_probe.listen(youtube.endpoint_name));
        endpoint_probe.close();
        QCOMPARE(principal_peer->begins.load(), 0);
        QCOMPARE(youtube_peer->begins.load(), 0);
        origin_units[origin_units.size() - 2] = u'p';
        endpoint_units.back() = endpoint_units.back() == u'z' ? u'a' : u'z';
        image_units.front() = image_units.front() == u'Z' ? u'Y' : u'Z';
        QVERIFY(h.ready());
        QLocalSocket principal_observer;
        principal_observer.connectToServer(principal.endpoint_name);
        QTRY_COMPARE(principal_observer.state(), QLocalSocket::ConnectedState);
        QTRY_COMPARE(principal_peer->begins.load(), 1);
        principal_observer.abort();
        QtNativeYoutubeAudioClient client{youtube, {.peer_check_factory = nativePeerFactory() }};
        const YoutubeAudioRequest selected{"01234567-89ab-4cde-8fab-0123456789ab",
                                           { { "aB0-_Cd1EF2",
                                               "Synthetic song",
                                               "Artist",
                                               "",
                                               false } }};
        QCOMPARE(client.begin(selected).state, DownloadAdmissionState::Pending);
        std::optional<DownloadSubmissionResult> reply;
        QVERIFY(h.wait([&] { return (reply = client.poll(selected.request_id)).has_value(); }));
        QCOMPARE(reply->error, DownloadSubmissionError::None);
        QCOMPARE(principal_peer->images, principal.expected_host_images);
        QCOMPARE(youtube_peer->images, youtube.expected_host_images);
        client.shutdown();
        QVERIFY(h.drain());
    }
    void bothNativePortsWaitForTheSharedCheckpointBeforeReply_data() {
        QTest::addColumn<bool>("youtube");
        QTest::newRow("principal port") << false;
        QTest::newRow("YouTube port") << true;
    }
    void bothNativePortsWaitForTheSharedCheckpointBeforeReply() {
        QFETCH(bool, youtube);
        auto profiles = nativeProfiles();
        profiles.principal_peer_check_factory = nativePeerFactory();
        profiles.youtube_peer_check_factory = nativePeerFactory();
        Harness h(true, nullptr, nullptr, false, profiles);
        QVERIFY(h.ready());
        h.checkpoint->allow(false);
        QtNativeDownloadClient principal{profiles.principal, {.peer_check_factory = nativePeerFactory() }};
        QtNativeYoutubeAudioClient yt{profiles.youtube, {.peer_check_factory = nativePeerFactory() }};
        const YoutubeAudioRequest selected{request().request_id,
                                           { { "aB0-_Cd1EF2",
                                               "Synthetic song",
                                               "Artist",
                                               "",
                                               false } }};
        const auto result = youtube ? yt.begin(selected) : principal.begin(request());
        QCOMPARE(result.state, DownloadAdmissionState::Pending);
        const auto observe = [&] {
            return youtube ? yt.poll(selected.request_id) : principal.poll(request().request_id);
        };
        QVERIFY(h.wait([&] { return h.checkpoint->saves == 1; }));
        QVERIFY(!observe());
        QCOMPARE(h.task->starts.load(), 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        h.checkpoint->allow(true);
        std::optional<DownloadSubmissionResult> reply;
        QVERIFY(h.wait([&] { return (reply = observe()).has_value(); }));
        QCOMPARE(reply->error, DownloadSubmissionError::None);
        QCOMPARE(reply->job_ids.size(), std::size_t{1});
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.state(), QStringLiteral("importing"));
        QCOMPARE(h.autosave.savedRevision(), SessionRevision{0});
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(!h.presenter.hasCurrentTrack());
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed"); }));
        QCOMPARE(h.session_writer.disk.download_import_receipts.front().job_id, reply->job_ids.front());
        principal.shutdown();
        yt.shutdown();
        QVERIFY(h.drain());
    }
    void nativeIngressClosesBeforeExecutionCancellationAndRetainsBothNamesThroughDrain() {
        auto profiles = nativeProfiles();
        const auto peers = std::make_shared<NativePeerState>();
        peers->released = false;
        profiles.principal_peer_check_factory = nativePeerFactory(peers);
        profiles.youtube_peer_check_factory = nativePeerFactory(peers);
        Harness h(true, nullptr, nullptr, false, profiles);
        ReleaseNativePeer release_peers{peers};
        h.task->complete = false;
        h.task->allow_cancel_terminal = false;
        const auto listener_open_during_cancel = std::make_shared<std::atomic<bool>>(false);
        h.task->during_cancel = [listener_open_during_cancel, principal = profiles.principal.endpoint_name,
                                 youtube = profiles.youtube.endpoint_name] {
            *listener_open_during_cancel = nativeEndpointOpenable(principal) || nativeEndpointOpenable(youtube);
        };
        QVERIFY(h.ready());
        QtNativeYoutubeAudioClient client{profiles.youtube, {.peer_check_factory = nativePeerFactory() }};
        const YoutubeAudioRequest selected{request().request_id,
                                           { { "aB0-_Cd1EF2",
                                               "Synthetic song",
                                               "Artist",
                                               "",
                                               false } }};
        QCOMPARE(client.begin(selected).state, DownloadAdmissionState::Pending);
        std::optional<DownloadSubmissionResult> reply;
        QVERIFY(h.wait([&] { return (reply = client.poll(selected.request_id)).has_value(); }));
        QCOMPARE(reply->error, DownloadSubmissionError::None);
        QVERIFY(h.wait([&] { return h.task->starts == 1; }));
        client.shutdown();
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.wait([&] { return h.task->cancelled.load(); }));
        QVERIFY(!listener_open_during_cancel->load());
        QVERIFY(!h.owner->stopped());
        QCOMPARE(h.platform.exits, 0);
        WindowsNativeDownloadLease principal_probe, youtube_probe;
        QVERIFY(!principal_probe.acquire(profiles.principal.endpoint_name));
        QVERIFY(!youtube_probe.acquire(profiles.youtube.endpoint_name));
        h.task->allow_cancel_terminal = true;
        for (int index = 0; index < 10; ++index) {
            h.poll();
            QTest::qWait(1);
        }
        QVERIFY(!h.owner->stopped());
        peers->released = true;
        QVERIFY(h.drain());
        QVERIFY(!principal_probe.acquire(profiles.principal.endpoint_name));
        QVERIFY(!youtube_probe.acquire(profiles.youtube.endpoint_name));
        h.owner.reset();
        QVERIFY(principal_probe.acquire(profiles.principal.endpoint_name));
        QVERIFY(youtube_probe.acquire(profiles.youtube.endpoint_name));
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
    }
    void secondListenerConflictRollsBackTheFirstWithoutAdmittingAnyWork() {
        auto profiles = nativeProfiles();
        profiles.principal_peer_check_factory = nativePeerFactory();
        profiles.youtube_peer_check_factory = nativePeerFactory();
        QLocalServer blocker;
        QVERIFY(blocker.listen(profiles.youtube.endpoint_name));
        Harness h(true, nullptr, nullptr, false, profiles);
        QVERIFY(!h.owner->start());
        QtNativeDownloadClient principal{profiles.principal, {.peer_check_factory = nativePeerFactory() }};
        const auto result = principal.begin(request());
        QCOMPARE(result.state, DownloadAdmissionState::Completed);
        QCOMPARE(result.result.error, DownloadSubmissionError::NotAvailable);
        QCOMPARE(h.task->starts.load(), 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        principal.shutdown();
        QVERIFY(h.drain());
        h.owner.reset();
        WindowsNativeDownloadLease principal_probe;
        QVERIFY(principal_probe.acquire(profiles.principal.endpoint_name));
    }
    void invalidNativeProfilesAreRejectedBeforeAnyListener_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("partial principal") << 0;
        QTest::newRow("foreign YouTube origin") << 1;
        QTest::newRow("endpoint alias") << 2;
        QTest::newRow("origin alias") << 3;
        QTest::newRow("missing host images") << 4;
    }
    void invalidNativeProfilesAreRejectedBeforeAnyListener() {
        QFETCH(int, mode);
        auto profiles = nativeProfiles();
        if (mode == 0)
            profiles.principal.endpoint_name.clear();
        else if (mode == 1)
            profiles.youtube.allowed_origin = QStringLiteral("https://example.invalid/");
        else if (mode == 2)
            profiles.youtube.endpoint_name = profiles.principal.endpoint_name.toUpper();
        else if (mode == 3)
            profiles.youtube.allowed_origin = profiles.principal.allowed_origin;
        else
            profiles.youtube.expected_host_images.clear();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Harness(false, nullptr, nullptr, false, profiles));
    }
#else
    void aBuildWithoutNativeMessagingRefusesAnActiveIngressProfile() {
        const auto profiles = nativeProfiles();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Harness(false, nullptr, nullptr, false, profiles));
    }
#endif
    void automaticPollingOffersTheNextPublishedLyricsOnlyAfterExplicitDismissal() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        LyricsNetwork network;
        QtLyricsService foreground;
        auto store = std::make_shared<QtJsonLyricsVariantsStore>(temp.path());
        LyricsBinding writer(foreground, store);
        Harness h(true, &writer, &network, true);
        QVERIFY(h.ready());
        auto* lyrics = qobject_cast<DownloadLyricsBinding*>(h.owner->binding()->lyrics());
        QVERIFY(lyrics);
        YoutubeAudioRequest first{"01234567-89ab-4cde-8fab-0123456789ab",
                                  {{"aB0-_Cd1EF2", "First song", "Artist", "", true}}};
        QCOMPARE(h.owner->youtubeSubmission().begin(first).state, DownloadAdmissionState::Pending);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value() && lyrics->canSave(); }));
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed"); }));
        auto second = first;
        second.request_id.back() = 'c';
        second.items[0].title = "Second song";
        QCOMPARE(h.owner->youtubeSubmission().begin(second).state, DownloadAdmissionState::Pending);
        QVERIFY(h.wait([&] { return h.task->starts == 2 && h.session_writer.active.has_value(); }));
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state(1) == QStringLiteral("completed"); }));
        QCOMPARE(lyrics->title(), QStringLiteral("First song"));
        QCOMPARE(network.calls, 1);
        const auto first_token = lyrics->requestToken();
        QVERIFY(lyrics->dismiss());
        // No owner.poll(): the normal automatic lifecycle must advance this.
        QTRY_COMPARE(lyrics->title(), QStringLiteral("Second song"));
        QVERIFY(lyrics->hasRequest());
        QVERIFY(lyrics->requestToken() != first_token);
        auto* downloads = h.owner->binding();
        QTRY_VERIFY(downloads->canArchiveSettled());
        QVERIFY(downloads->requestArchiveConfirmation());
        const auto stale_archive = downloads->archiveConsentToken();
        QVERIFY(!stale_archive.isEmpty());
        QVERIFY(lyrics->dismiss());
        QTRY_VERIFY(downloads->archiveConsentToken().isEmpty());
        QVERIFY(!downloads->confirmArchive(stale_archive));
        QVERIFY(downloads->requestArchiveConfirmation());
        QVERIFY(downloads->confirmArchive(downloads->archiveConsentToken()));
        QTRY_COMPARE(downloads->jobsModel()->rowCount(), 0);
        QCOMPARE(h.task->starts.load(), 2);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 2);
        QVERIFY(h.drain());
    }
    void publishedYoutubeLyricsUseTheSharedWriterAndARealTemporaryCatalogueAfterExplicitSave() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        LyricsNetwork network;
        QtLyricsService foreground;
        auto store = std::make_shared<QtJsonLyricsVariantsStore>(temp.path());
        LyricsBinding writer(foreground, store);
        Harness h(true, &writer, &network);
        QVERIFY(h.ready());
        auto* lyrics = qobject_cast<DownloadLyricsBinding*>(h.owner->binding()->lyrics());
        QVERIFY(lyrics);
        const YoutubeAudioRequest selected{"01234567-89ab-4cde-8fab-0123456789ab",
                                           {{"aB0-_Cd1EF2", "Synthetic song", "Artist", "", true}}};
        QCOMPARE(h.owner->youtubeSubmission().begin(selected).state, DownloadAdmissionState::Pending);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed") && lyrics->canSave(); }));
        QCOMPARE(network.calls, 1);
        QCOMPARE(network.last_url.host(), QStringLiteral("lrclib.net"));
        QCOMPARE(lyrics->candidates().size(), 2);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(!h.presenter.hasCurrentTrack());
        const auto target = std::filesystem::path(L"C:/synthetic-published/final.mp3");
        QCOMPARE(store->load(target).status, LyricsVariantsLoadStatus::Missing);
        QVERIFY(lyrics->saveCandidate(1, "Romanized form", "ja", "Latn", "romanized"));
        QVERIFY(h.wait([&] { return writer.acquiredLyricsSave().state == AcquiredLyricsSaveState::Saved; }));
        const auto saved = store->load(target);
        QCOMPARE(saved.status, LyricsVariantsLoadStatus::Loaded);
        QCOMPARE(saved.variants.size(), std::size_t{1});
        QCOMPARE(saved.variants[0].kind, LyricsVariantKind::Romanized);
        QCOMPARE(saved.variants[0].content, std::string("Other source form"));
        QCOMPARE(network.calls, 1);
        QCOMPARE(h.task->starts.load(), 1);
        const auto replay = h.owner->youtubeSubmission().begin(selected);
        QCOMPARE(replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(replay.result.job_ids.size(), std::size_t{1});
        for (int i = 0; i < 3; ++i)
            h.poll();
        auto* downloads = h.owner->binding();
        QVERIFY(!downloads->canArchiveSettled());
        QVERIFY(lyrics->dismiss());
        QVERIFY(h.wait([&] { return downloads->canArchiveSettled(); }));
        QVERIFY(downloads->requestArchiveConfirmation());
        QVERIFY(downloads->confirmArchive(downloads->archiveConsentToken()));
        QVERIFY(h.wait([&] { return downloads->jobsModel()->rowCount() == 0; }));
        const auto archived_replay = h.owner->youtubeSubmission().begin(selected);
        QCOMPARE(archived_replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(archived_replay.result.job_ids, replay.result.job_ids);
        QCOMPARE(store->load(target).variants, saved.variants);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(network.calls, 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(h.drain());
    }
    void lyricsFailureOrAbsentConsentCannotFailOrRepeatPublishedAudio_data() {
        QTest::addColumn<bool>("consent");
        QTest::newRow("lookup404") << true;
        QTest::newRow("unchecked") << false;
    }
    void lyricsFailureOrAbsentConsentCannotFailOrRepeatPublishedAudio() {
        QFETCH(bool, consent);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        LyricsNetwork network;
        network.status = 404;
        QtLyricsService foreground;
        auto store = std::make_shared<QtJsonLyricsVariantsStore>(temp.path());
        LyricsBinding writer(foreground, store);
        Harness h(true, &writer, &network);
        QVERIFY(h.ready());
        auto* lyrics = qobject_cast<DownloadLyricsBinding*>(h.owner->binding()->lyrics());
        QVERIFY(lyrics);
        const YoutubeAudioRequest selected{"01234567-89ab-4cde-8fab-0123456789ab",
                                           {{"aB0-_Cd1EF2", "Synthetic song", "Artist", "", consent}}};
        QCOMPARE(h.owner->youtubeSubmission().begin(selected).state, DownloadAdmissionState::Pending);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        h.session_writer.save();
        QVERIFY(h.wait([&] {
            return h.state() == QStringLiteral("completed") && (!consent || (network.calls == 1 && !lyrics->busy()));
        }));
        QCOMPARE(network.calls, consent ? 1 : 0);
        QCOMPARE(lyrics->hasRequest(), consent);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(!writer.variantsHasPendingChange());
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(h.drain());
    }
    void youtubeSessionSurvivesDisconnectAndImportsOnlyAfterBothSavedAcknowledgements() {
        Harness h(true);
        QVERIFY(h.ready());
        h.checkpoint->allow(false);
        const YoutubeAudioRequest selected{"01234567-89ab-4cde-8fab-0123456789ab",
                                           {{"aB0-_Cd1EF2", "Synthetic song", "Artist", "", true}}};
        const auto frame = encodeNativeMessageFrame(encodeYoutubeAudioRequest(selected));
        const QString origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");
        {
            NativeYoutubeAudioSession observer(origin, origin, h.owner->youtubeSubmission());
            QCOMPARE(observer.receive(frame, 1).status, NativeSessionStatus::Awaiting);
            QCOMPARE(observer.finishInput(2).status, NativeSessionStatus::Closed);
        }
        QVERIFY(h.wait([&] { return h.checkpoint->saves == 1; }));
        QCOMPARE(h.task->starts.load(), 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        NativeYoutubeAudioSession reconnected(origin, origin, h.owner->youtubeSubmission());
        QCOMPARE(reconnected.receive(frame, 3).status, NativeSessionStatus::Awaiting);
        h.checkpoint->allow(true);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        const auto reply = reconnected.poll(4);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        const auto ack = decodeYoutubeAudioResponse(reply.response_frame.mid(4), selected.request_id, 1);
        QVERIFY(ack);
        QCOMPARE(ack->error, DownloadSubmissionError::None);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QCOMPARE(h.state(), QStringLiteral("importing"));
        QCOMPARE(h.autosave.savedRevision(), SessionRevision{0});
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(!h.presenter.hasCurrentTrack());
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed"); }));
        QCOMPARE(h.session_writer.disk.download_import_receipts.size(), std::size_t{1});
        const auto replay = h.owner->youtubeSubmission().begin(selected);
        QCOMPARE(replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(replay.result.job_ids, ack->job_ids);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(h.drain());
    }
    void refusesMissingExecutionOrCheckpointFactory() {
        Harness h;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 WindowsPlayerDownloads(h.presenter, {}, nullptr, [state = h.checkpoint] {
                                     return std::make_unique<CheckpointStore>(state);
                                 }));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 WindowsPlayerDownloads(h.presenter, {}, std::make_unique<WindowsDownloadExecution>([] {
                                                            return std::unique_ptr<WindowsDownloadTask>{};
                                                        }),
                                                        {}));
    }
    void admissionWaitsForCheckpointAndImportWaitsForMatchingSessionSaved_data() {
        QTest::addColumn<bool>("embedded");
        QTest::newRow("common") << false;
        QTest::newRow("embedded") << true;
    }
    void admissionWaitsForCheckpointAndImportWaitsForMatchingSessionSaved() {
        QFETCH(bool, embedded);
        Harness h(true, nullptr, nullptr, false, {}, embedded);
        QVERIFY(h.ready());
        QVERIFY(!h.owner->start());
        QVERIFY(!h.owner->stopped());
        auto* const admission = embedded ? h.owner->embeddedBrowserSubmission() : &h.owner->submission();
        QVERIFY(admission);
        h.checkpoint->allow(false);
        QVERIFY(h.wait([&] { return admission->begin(request()).state == DownloadAdmissionState::Pending; }));
        QVERIFY(h.wait([&] { return h.checkpoint->saves == 1; }));
        QVERIFY(!admission->poll(request().request_id));
        QCOMPARE(h.task->starts.load(), 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        h.checkpoint->allow(true);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QCOMPARE(h.state(), QStringLiteral("importing"));
        QCOMPARE(h.autosave.savedRevision(), SessionRevision{0});
        QCOMPARE(h.session_writer.active->snapshot.download_import_receipts.size(), std::size_t{1});
        QCOMPARE(h.session_writer.active->snapshot.queue.items.size(), std::size_t{1});
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(!h.presenter.hasCurrentTrack());
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed"); }));
        QCOMPARE(h.session_writer.disk.download_import_receipts.size(), std::size_t{1});
        const auto repeated = admission->begin(request());
        QCOMPARE(repeated.state, DownloadAdmissionState::Completed);
        QCOMPARE(repeated.result.error, DownloadSubmissionError::None);
        for (int index = 0; index < 5; ++index)
            h.poll();
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(h.drain());
        QVERIFY(h.checkpoint->destroyed);
        QVERIFY(!h.owner->start());
    }
    void finalCheckpointFailureRequiresExplicitRetryAndPhysicalDrain() {
        Harness h;
        QVERIFY(h.ready());
        h.checkpoint->fail_save = true;
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.wait([&] { return h.owner->binding()->canRetryFinalSave(); }));
        QVERIFY(!h.owner->stopped());
        QCOMPARE(h.platform.exits, 0);
        h.checkpoint->fail_save = false;
        QVERIFY(h.owner->binding()->retryFinalSave());
        QVERIFY(h.wait([&] { return h.owner->stopped(); }));
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 1);
        QVERIFY(h.checkpoint->destroyed);
        QCOMPARE(h.task->starts.load(), 0);
    }
    void exitWaitsForTaskAcknowledgementWithoutStartingSuccessorOrImporting() {
        Harness h;
        h.task->complete = false;
        h.task->allow_cancel_terminal = false;
        QVERIFY(h.ready());
        auto batch = request();
        batch.items.push_back(batch.items.front());
        QVERIFY(h.wait([&] { return h.owner->submission().begin(batch).state == DownloadAdmissionState::Pending; }));
        QVERIFY(h.wait([&] { return h.task->starts == 1; }));
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.wait([&] { return h.task->cancelled.load(); }));
        QVERIFY(!h.owner->stopped());
        QCOMPARE(h.platform.exits, 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        h.task->allow_cancel_terminal = true;
        QVERIFY(h.drain());
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.state(0), QStringLiteral("cancelled"));
        QCOMPARE(h.state(1), QStringLiteral("cancelled"));
        QCOMPARE(h.owner->submission().begin(batch).result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(h.session_writer.disk.download_import_receipts.empty());
    }
};
QTEST_MAIN(WindowsPlayerDownloadsTests)
#include "WindowsPlayerDownloadsTests.moc"
