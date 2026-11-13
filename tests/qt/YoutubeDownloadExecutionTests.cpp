#include "adapters/YoutubeDownloadExecution.hpp"
#include "support/YoutubeResolverTestSupport.hpp"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

using namespace music_player::m0;
namespace {
struct State final {
    std::atomic_int resolver_starts{}, resolver_cancels{}, destinations{}, destination_starts{}, destination_cancels{},
        direct_starts{};
    std::atomic_bool resolve{true}, resolver_ack{true}, destination_ack{true}, worker{false};
    std::atomic_bool malformed{}, false_destination_start{};
    std::atomic<DownloadFailure> destination_failure{DownloadFailure::None};
    std::atomic_bool block_preparation{}, preparation_entered{}, preparation_released{};
    std::atomic_int transfer_starts{};
    std::atomic_bool invalid_destination_state{}, await_destination_cancel{}, throw_destination_cancel_once{};
    std::atomic_bool force_destination_terminal{};
    std::atomic_int invalid_destination_polls{};
    std::atomic_int invalid_destination_kind{};
    std::mutex preparation_mutex;
    std::condition_variable preparation_changed;
    QString stream, publication_name;
};
struct ReleaseAcks final {
    std::shared_ptr<State> state;
    ~ReleaseAcks() {
        state->resolver_ack = true;
        state->destination_ack = true;
        state->force_destination_terminal = true;
        state->preparation_released = true;
        state->preparation_changed.notify_all();
    }
};
class Process final : public YoutubeResolverProcess {
    std::shared_ptr<State> state_;
    bool cancelled_{};

  public:
    explicit Process(std::shared_ptr<State> state) : state_(std::move(state)) {}
    bool start(const QString& id, qint64) override {
        if (id != QStringLiteral("aB0-_Cd1EF2"))
            return false;
        ++state_->resolver_starts;
        return true;
    }
    YoutubeResolverProcessUpdate poll(qint64) override {
        if (cancelled_)
            return state_->resolver_ack ? YoutubeResolverProcessUpdate{.state = YoutubeResolverProcessState::Cancelled}
                                        : YoutubeResolverProcessUpdate{};
        if (!state_->resolve)
            return {};
        return {.state = YoutubeResolverProcessState::Exited,
                .stdout_chunk = state_->malformed ? QByteArray("not JSON") : test::youtubeResolverFixtureJson(),
                .exit_code = 0};
    }
    void cancel() override {
        cancelled_ = true;
        ++state_->resolver_cancels;
    }
};
class Destination final : public WindowsDownloadTask {
    std::shared_ptr<State> state_;
    std::string id_;
    bool cancelled_{};

  public:
    explicit Destination(std::shared_ptr<State> state) : state_(std::move(state)) {}
    bool start(const std::string& id, const DownloadItem&, qint64) override {
        id_ = id;
        ++state_->direct_starts;
        return true;
    }
    bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> cap, qint64, qint64,
                              const QString& publication_name) override {
        if (!cap)
            return false;
        id_ = cap->jobId();
        state_->stream = cap->streamUrl();
        state_->publication_name = publication_name;
        ++state_->destination_starts;
        if (state_->block_preparation) {
            std::unique_lock lock(state_->preparation_mutex);
            state_->preparation_entered = true;
            if (!state_->preparation_changed.wait_for(lock, std::chrono::seconds(5),
                                                      [&] { return state_->preparation_released.load(); }))
                return false;
        }
        return !state_->false_destination_start;
    }
    void cancel() override {
        ++state_->destination_cancels;
        if (state_->throw_destination_cancel_once.exchange(false))
            throw std::runtime_error("synthetic first cancellation failed before requesting stop");
        cancelled_ = true;
    }
    DownloadExecutionUpdate poll(qint64) override {
        if (state_->force_destination_terminal)
            return {.job_id = id_, .state = DownloadExecutionState::Cancelled};
        if (cancelled_) {
            if (!state_->destination_ack)
                return {.job_id = id_};
            const auto error = state_->destination_failure.load();
            return {.job_id = id_,
                    .state = error == DownloadFailure::None ? DownloadExecutionState::Cancelled
                                                            : DownloadExecutionState::Failed,
                    .error = error};
        }
        if (state_->invalid_destination_state) {
            ++state_->invalid_destination_polls;
            if (state_->invalid_destination_kind == 1)
                return {.job_id = id_,
                        .state = DownloadExecutionState::Published,
                        .phase = DownloadPhase::Publishing,
                        .published_mp3 = std::filesystem::path(L"relative.mp3")};
            if (state_->invalid_destination_kind == 2)
                return {.job_id = id_, .state = DownloadExecutionState::Failed, .error = DownloadFailure::None};
            return {.job_id = id_, .state = static_cast<DownloadExecutionState>(99)};
        }
        if (state_->await_destination_cancel)
            return {.job_id = id_};
        ++state_->transfer_starts;
        return {.job_id = id_,
                .state = DownloadExecutionState::Published,
                .phase = DownloadPhase::Publishing,
                .published_mp3 = std::filesystem::path(L"C:\\synthetic-youtube\\final.mp3")};
    }
};
std::unique_ptr<WindowsDownloadExecution> execution(const std::shared_ptr<State>& state) {
    return makeWindowsYoutubeAudioExecution(
        [state] {
            state->worker = QThread::currentThread() != QCoreApplication::instance()->thread();
            return std::make_unique<Process>(state);
        },
        [state] {
            ++state->destinations;
            return std::make_unique<Destination>(state);
        },
        [] { return qint64{1'999'999'000}; });
}
const DownloadItem watch{"https://www.youtube.com/watch?v=aB0-_Cd1EF2", "Synthetic song", DownloadMediaKind::Audio};
void stop(WindowsDownloadExecution& value) {
    value.shutdown();
    QTRY_VERIFY(value.stopped());
}
} // namespace
class YoutubeDownloadExecutionTests final : public QObject {
    Q_OBJECT
  private slots:
    void legacyDirectExecutionRefusesYoutubeContextBeforeCopyingIt() {
        auto state = std::make_shared<State>();
        WindowsDownloadExecution legacy([state] { return std::make_unique<Destination>(state); });
        DownloadItem invalid{"https://media.example.test/music.mp3", "Direct", DownloadMediaKind::Audio};
        invalid.youtube = YoutubeAudioItem{"aB0-_Cd1EF2", "Direct", std::string(513, 'a'), "", false};
        QVERIFY(!legacy.start("direct_1", invalid));
        QCOMPARE(state->direct_starts.load(), 0);
        stop(legacy);
    }
    void oneTrustedExecutorRunsDirectAndTypedYoutubeSequentially() {
        auto state = std::make_shared<State>();
        auto value = makeWindowsMediaAndYoutubeExecution([state] { return std::make_unique<Process>(state); },
                                                         [state] {
                                                             ++state->destinations;
                                                             return std::make_unique<Destination>(state);
                                                         },
                                                         [] { return qint64{1'999'999'000}; });
        QVERIFY(value);
        const DownloadItem direct{"https://media.example.test/music.mp3", "Direct", DownloadMediaKind::Audio};
        QVERIFY(value->start("direct_1", direct));
        QTRY_COMPARE(value->poll("direct_1").state, DownloadExecutionState::Published);
        QCOMPARE(state->direct_starts.load(), 1);
        QCOMPARE(state->resolver_starts.load(), 0);
        auto youtube = watch;
        youtube.youtube = YoutubeAudioItem{"aB0-_Cd1EF2", youtube.title, "Artist", "", true};
        QVERIFY(value->start("youtube_1", youtube));
        QTRY_COMPARE(value->poll("youtube_1").state, DownloadExecutionState::Published);
        QCOMPARE(state->direct_starts.load(), 1);
        QCOMPARE(state->resolver_starts.load(), 1);
        QCOMPARE(state->destination_starts.load(), 1);
        stop(*value);
    }
    void youtubePublicationNameUsesOriginalMetadataOrTheCompleteOverride() {
        const auto state = std::make_shared<State>();
        auto value = makeWindowsMediaAndYoutubeExecution([state] { return std::make_unique<Process>(state); },
                                                         [state] { return std::make_unique<Destination>(state); },
                                                         [] { return 1'999'999'000; });
        auto youtube = watch;
        youtube.youtube = YoutubeAudioItem{"aB0-_Cd1EF2", youtube.title, "Artista 日本", "", false};
        QVERIFY(value->start("default_name", youtube));
        QTRY_COMPARE(value->poll("default_name").state, DownloadExecutionState::Published);
        QCOMPARE(state->publication_name, QStringLiteral("Synthetic song - Artista 日本"));

        youtube.youtube->file_name = "Minha música.MP3";
        QVERIFY(value->start("custom_name", youtube));
        QTRY_COMPARE(value->poll("custom_name").state, DownloadExecutionState::Published);
        QCOMPARE(state->publication_name, QStringLiteral("Minha música.MP3"));

        youtube.youtube->file_name.clear();
        youtube.youtube->artist.clear();
        QVERIFY(value->start("title_only", youtube));
        QTRY_COMPARE(value->poll("title_only").state, DownloadExecutionState::Published);
        QCOMPARE(state->publication_name, QStringLiteral("Synthetic song"));
    }

    void theMixedExecutorDoesNotInferYoutubeAuthorityFromAUrlOrIncoherentContext() {
        auto state = std::make_shared<State>();
        auto value = makeWindowsMediaAndYoutubeExecution([state] { return std::make_unique<Process>(state); },
                                                         [state] { return std::make_unique<Destination>(state); },
                                                         [] { return qint64{1'999'999'000}; });
        QVERIFY(value);
        QVERIFY(!value->start("job_1", watch));
        for (int field = 0; field < 5; ++field) {
            auto invalid = watch;
            invalid.youtube = YoutubeAudioItem{"aB0-_Cd1EF2", invalid.title, "Artist", "", false};
            if (field == 0)
                invalid.youtube->video_id = "zzzzzzzzzzz";
            else if (field == 1)
                invalid.youtube->title = "Mismatched";
            else if (field == 2)
                invalid.youtube->artist = std::string(513, 'x');
            else if (field == 3)
                invalid.youtube->lyrics_page = "https://attacker.example/";
            else
                invalid.url = "https://media.example.test/file.mp3";
            QVERIFY(!value->start("job_1", invalid));
        }
        QCOMPARE(state->direct_starts.load(), 0);
        QCOMPARE(state->resolver_starts.load(), 0);
        stop(*value);
    }
    void resolutionHandsTheExactCapabilityToTheExistingTaskOnTheWorker() {
        auto state = std::make_shared<State>();
        auto value = execution(state);
        QVERIFY(value);
        QCOMPARE(state->resolver_starts.load(), 0);
        QVERIFY(value->start("youtube_job", watch));
        QTRY_COMPARE(value->poll("youtube_job").state, DownloadExecutionState::Published);
        QVERIFY(state->worker);
        QCOMPARE(state->destination_starts.load(), 1);
        QCOMPARE(state->stream,
                 QStringLiteral("https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A%2fb%2F&x=1"));
        QCOMPARE(value->poll("youtube_job").published_mp3, std::filesystem::path(L"C:\\synthetic-youtube\\final.mp3"));
        stop(*value);
    }
    void legacyExecutionAndTheYoutubeFactoryKeepTheirAdmissionBoundaries() {
        auto state = std::make_shared<State>();
        WindowsDownloadExecution legacy([state] {
            ++state->destinations;
            return std::make_unique<Destination>(state);
        });
        QVERIFY(!legacy.start("job_1", watch));
        auto value = execution(state);
        QVERIFY(value);
        QVERIFY(!value->start("job_1", {"https://public.example.test/song.mp3", "Song", DownloadMediaKind::Audio}));
        auto tracking = watch;
        tracking.url += "&list=private";
        QVERIFY(!value->start("job_1", tracking));
        auto video = watch;
        video.kind = DownloadMediaKind::Video;
        QVERIFY(!value->start("job_1", video));
        QCOMPARE(state->resolver_starts.load(), 0);
        stop(*value);
        stop(legacy);
    }
    void cancelledResolutionMustStopBeforeASecondJobCanBeAdmitted() {
        auto state = std::make_shared<State>();
        state->resolve = false;
        state->resolver_ack = false;
        auto value = execution(state);
        QVERIFY(value);
        ReleaseAcks cleanup{state};
        QVERIFY(value->start("job_1", watch));
        QTRY_COMPARE(state->resolver_starts.load(), 1);
        value->cancel("job_1");
        QTRY_VERIFY(state->resolver_cancels > 0);
        QCOMPARE(value->poll("job_1").state, DownloadExecutionState::Running);
        QVERIFY(!value->start("job_2", watch));
        QCOMPARE(state->destinations.load(), 0);
        state->resolver_ack = true;
        QTRY_COMPARE(value->poll("job_1").state, DownloadExecutionState::Cancelled);
        stop(*value);
    }
    void invalidResolverOutputNeverStartsTransferOrConversion() {
        auto state = std::make_shared<State>();
        state->malformed = true;
        auto value = execution(state);
        QVERIFY(value);
        QVERIFY(value->start("job_1", watch));
        QTRY_COMPARE(value->poll("job_1").state, DownloadExecutionState::Failed);
        QCOMPARE(value->poll("job_1").error, DownloadFailure::InvalidResult);
        QCOMPARE(state->destinations.load(), 0);
        stop(*value);
    }
    void uncertainDestinationStartStillDrainsBeforeTerminal() {
        auto state = std::make_shared<State>();
        state->false_destination_start = true;
        state->destination_ack = false;
        auto value = execution(state);
        QVERIFY(value);
        ReleaseAcks cleanup{state};
        QVERIFY(value->start("job_1", watch));
        QTRY_VERIFY(state->destination_cancels > 0);
        QCOMPARE(value->poll("job_1").state, DownloadExecutionState::Running);
        state->destination_ack = true;
        QTRY_COMPARE(value->poll("job_1").state, DownloadExecutionState::Failed);
        QCOMPARE(value->poll("job_1").error, DownloadFailure::Unavailable);
        stop(*value);
    }
    void refusedDestinationStartPreservesMatchingFailureAfterDrain_data() {
        QTest::addColumn<int>("error");
        QTest::newRow("journal refusal") << static_cast<int>(DownloadFailure::InvalidResult);
        QTest::newRow("publication failure") << static_cast<int>(DownloadFailure::Publication);
    }
    void refusedDestinationStartPreservesMatchingFailureAfterDrain() {
        QFETCH(int, error);
        auto state = std::make_shared<State>();
        state->false_destination_start = true;
        state->destination_failure = static_cast<DownloadFailure>(error);
        state->destination_ack = false;
        auto value = execution(state);
        QVERIFY(value);
        ReleaseAcks cleanup{state};
        QVERIFY(value->start("job_1", watch));
        QTRY_VERIFY(state->destination_cancels > 0);
        QCOMPARE(value->poll("job_1").state, DownloadExecutionState::Running);
        QVERIFY(!value->start("job_2", watch));
        state->destination_ack = true;
        QTRY_COMPARE(value->poll("job_1").state, DownloadExecutionState::Failed);
        QCOMPARE(value->poll("job_1").error, static_cast<DownloadFailure>(error));
        QCOMPARE(state->transfer_starts.load(), 0);
        stop(*value);
    }
    void cancellationDuringPreparationPrecedesTheFirstTransferPoll() {
        auto state = std::make_shared<State>();
        state->block_preparation = true;
        auto value = execution(state);
        QVERIFY(value);
        ReleaseAcks cleanup{state};
        QVERIFY(value->start("job_1", watch));
        QTRY_VERIFY(state->preparation_entered.load());
        value->cancel("job_1");
        state->preparation_released = true;
        state->preparation_changed.notify_all();
        QTRY_VERIFY(value->poll("job_1").state != DownloadExecutionState::Running);
        QCOMPARE(state->transfer_starts.load(), 0);
        QCOMPARE(value->poll("job_1").state, DownloadExecutionState::Cancelled);
        stop(*value);
    }
    void youtubeAdmissionChecksMetadataBeforeCopyingOrStartingWork() {
        auto state = std::make_shared<State>();
        state->resolve = false;
        auto value = execution(state);
        QVERIFY(value);
        for (const auto& title :
             {std::string{}, std::string(801, 'a'), std::string("bad\0title", 9), std::string("\xc0\xaf", 2)}) {
            auto invalid = watch;
            invalid.title = title;
            QVERIFY(!value->start("job_1", invalid));
        }
        QCOMPARE(state->resolver_starts.load(), 0);
        QCOMPARE(state->destinations.load(), 0);
        auto maximum = watch;
        maximum.title = std::string(800, 'a');
        QVERIFY(value->start("job_1", maximum));
        QTRY_COMPARE(state->resolver_starts.load(), 1);
        stop(*value);
    }
    void invalidDestinationStateKeepsOwnershipUntilAMatchingTerminalAcknowledgement() {
        constexpr auto child_flag = "RC_MUSIC_YOUTUBE_INVALID_STATE_CHILD";
        const auto child_mode = qEnvironmentVariable(child_flag);
        if (child_mode.isEmpty()) {
            // A regressed wrapper latches the invalid state forever and blocks
            // its destructor. Isolate that Red in our own bounded QtTest child;
            // no worker/native helper or media is created by either process.
            QStringList failures;
            int passed_children{};
            for (const auto* mode : {"state", "published", "failed"}) {
                auto child = std::make_unique<QProcess>();
                auto environment = QProcessEnvironment::systemEnvironment();
                environment.insert(QLatin1String(child_flag), QString::fromLatin1(mode));
                child->setProcessEnvironment(environment);
                child->start(
                    QCoreApplication::applicationFilePath(),
                    {QStringLiteral("invalidDestinationStateKeepsOwnershipUntilAMatchingTerminalAcknowledgement"),
                     QStringLiteral("-o"), QStringLiteral("-,txt")});
                QVERIFY2(child->waitForStarted(5000), "The isolated QtTest child did not start.");
                if (!child->waitForFinished(4000)) {
                    child->kill(); // Only the process created immediately above.
                    if (!child->waitForFinished(2000)) {
                        static_cast<void>(child.release());
                        QFAIL("The owned QtTest child did not acknowledge termination; preserve it for review.");
                    }
                    failures.push_back(
                        QStringLiteral("%1 prevented draining: %2")
                            .arg(QString::fromLatin1(mode), QString::fromUtf8(child->readAllStandardOutput())));
                    continue;
                }
                const auto output = child->readAllStandardOutput();
                if (child->exitStatus() != QProcess::NormalExit || child->exitCode() != 0)
                    failures.push_back(QStringLiteral("%1 failed its contract: %2")
                                           .arg(QString::fromLatin1(mode), QString::fromUtf8(output)));
                else
                    ++passed_children;
            }
            QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
            QCOMPARE(passed_children, 3);
            return;
        }
        QVERIFY(child_mode == QLatin1String("state") || child_mode == QLatin1String("published") ||
                child_mode == QLatin1String("failed"));
        auto state = std::make_shared<State>();
        state->invalid_destination_state = true;
        state->invalid_destination_kind = child_mode == QLatin1String("published") ? 1
                                          : child_mode == QLatin1String("failed")  ? 2
                                                                                   : 0;
        state->destination_ack = false;
        auto value = execution(state);
        QVERIFY(value);
        ReleaseAcks cleanup{state};
        QVERIFY(value->start("job_1", watch));
        QTRY_VERIFY_WITH_TIMEOUT(state->invalid_destination_polls > 0, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(state->destination_cancels > 0, 1000);
        QCOMPARE(value->poll("job_1").state, DownloadExecutionState::Running);
        QVERIFY(!value->start("job_2", watch));
        state->destination_ack = true;
        QTRY_COMPARE_WITH_TIMEOUT(value->poll("job_1").state, DownloadExecutionState::Failed, 1000);
        QCOMPARE(value->poll("job_1").error, DownloadFailure::InvalidResult);
        stop(*value);
    }
    void aThrowingFirstCancellationMustBeRetriedUntilTheDestinationAcknowledgesStopping() {
        auto state = std::make_shared<State>();
        state->await_destination_cancel = true;
        state->throw_destination_cancel_once = true;
        state->destination_ack = false;
        auto value = execution(state);
        QVERIFY(value);
        ReleaseAcks cleanup{state};
        QVERIFY(value->start("job_1", watch));
        QTRY_COMPARE(state->destination_starts.load(), 1);
        value->cancel("job_1");
        QTRY_VERIFY_WITH_TIMEOUT(state->destination_cancels >= 2, 1000);
        QCOMPARE(value->poll("job_1").state, DownloadExecutionState::Running);
        QVERIFY(!value->start("job_2", watch));
        state->destination_ack = true;
        QTRY_VERIFY_WITH_TIMEOUT(value->poll("job_1").state != DownloadExecutionState::Running, 1000);
        const auto terminal = value->poll("job_1");
        QVERIFY(terminal.state == DownloadExecutionState::Cancelled ||
                terminal.state == DownloadExecutionState::Failed);
        QVERIFY(terminal.published_mp3.empty());
        stop(*value);
    }
};
QTEST_GUILESS_MAIN(YoutubeDownloadExecutionTests)
#include "YoutubeDownloadExecutionTests.moc"
