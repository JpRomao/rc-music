#include "adapters/WindowsYoutubeExecution.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace music_player::m0;
namespace {
const QString video_id{QStringLiteral("testVideo01")};
const DownloadItem direct{"https://media.example.test/own.mp3", "Direct fixture", DownloadMediaKind::Audio};
DownloadItem youtube() {
    DownloadItem item{"https://www.youtube.com/watch?v=testVideo01", "Synthetic song", DownloadMediaKind::Audio};
    item.youtube = YoutubeAudioItem{"testVideo01", item.title, "Synthetic artist", "", false};
    return item;
}
bool writeNew(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QByteArray fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
struct Gate final {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{}, released{}, timed_out{};
    void enter() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        timed_out = !changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; });
    }
    bool wait() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
    }
    bool blocked() {
        std::lock_guard lock(mutex);
        return entered && !released && !timed_out;
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
struct Observations final {
    std::atomic_int preparations{}, destinations{}, direct_starts{}, resolved_starts{};
    std::atomic_bool worker{true}, valid_capability{true}, released_before_handoff{true}, copied{true};
    std::mutex mutex;
    std::weak_ptr<const WindowsYoutubeRuntime> lease;
    QStringList directories;
    void observed(const std::shared_ptr<const WindowsYoutubeRuntime>& files) {
        std::lock_guard lock(mutex);
        lease = files;
        if (files)
            directories.append(files->directory());
    }
    bool expired() {
        std::lock_guard lock(mutex);
        return lease.expired();
    }
    QStringList paths() {
        std::lock_guard lock(mutex);
        return directories;
    }
    void observeThread() { worker = worker && QThread::currentThread() != QCoreApplication::instance()->thread(); }
};
class Consumer final : public WindowsDownloadTask {
    std::shared_ptr<Observations> observations_;
    std::string id_;

  public:
    explicit Consumer(std::shared_ptr<Observations> observations) : observations_(std::move(observations)) {}
    bool start(const std::string& id, const DownloadItem&, qint64) override {
        id_ = id;
        observations_->observeThread();
        ++observations_->direct_starts;
        return true;
    }
    bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> capability, qint64, qint64 epoch,
                              const QString&) override {
        observations_->observeThread();
        observations_->valid_capability = observations_->valid_capability && capability &&
                                          capability->videoId() == video_id && capability->expiresAtSeconds() > epoch;
        observations_->released_before_handoff = observations_->released_before_handoff && observations_->expired();
        if (!capability)
            return false;
        id_ = capability->jobId();
        ++observations_->resolved_starts;
        return true;
    }
    void cancel() override {}
    DownloadExecutionUpdate poll(qint64) override {
        // Acknowledged deliberate stop: no transfer, conversion or fake final.
        return {.job_id = id_, .state = DownloadExecutionState::Failed, .error = DownloadFailure::UnsupportedMedia};
    }
};
WindowsDownloadExecution::TaskFactory consumer(const std::shared_ptr<Observations>& observations) {
    return [observations] {
        ++observations->destinations;
        observations->observeThread();
        return std::make_unique<Consumer>(observations);
    };
}
std::unique_ptr<WindowsDownloadExecution> execution(YoutubeResolverProcessFactory source,
                                                    const std::shared_ptr<Observations>& observations) {
    return makeWindowsMediaAndYoutubeExecution(std::move(source), consumer(observations),
                                               [] { return qint64{1'900'000'000}; });
}
struct Fixture final {
    struct OwnedEvent final {
        HANDLE value{};
        ~OwnedEvent() {
            if (value)
                CloseHandle(value);
        }
    };
    QTemporaryDir parent;
    QString runtime{parent.filePath(QStringLiteral("private-runtime"))};
    QString markers{parent.filePath(QStringLiteral("owned-markers"))};
    WindowsYoutubeRuntimeConfig config;
    QProcessEnvironment environment;
    std::array<OwnedEvent, 2> completion_gates;
    bool initialize() {
        if (!parent.isValid() || !QDir().mkdir(runtime) || !QDir().mkdir(markers))
            return false;
        const auto core = parent.filePath(QStringLiteral("core.js"));
        const auto library = parent.filePath(QStringLiteral("library.js"));
        if (!writeNew(core, QByteArrayLiteral("/* SYNTHETIC_CORE */ function jsc(data) { return data; }")) ||
            !writeNew(library, QByteArrayLiteral("/* SYNTHETIC_LIBRARY */ var lib = {};")))
            return false;
        const auto helper = QString::fromUtf8(RC_MUSIC_YOUTUBE_BROKER_HELPER_PATH);
        const auto hash = fileHash(helper);
        if (hash.size() != 64)
            return false;
        config = {
            runtime,
            {{WindowsYoutubeRuntimeRole::Extractor, QStringLiteral("extractor/yt-dlp.exe"), helper, hash},
             {WindowsYoutubeRuntimeRole::Node, QStringLiteral("javascript/node.exe"), helper, hash},
             {WindowsYoutubeRuntimeRole::EjsCore,
              QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js"), core, fileHash(core)},
             {WindowsYoutubeRuntimeRole::EjsLibrary,
              QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"), library, fileHash(library)}}};
        std::array<wchar_t, 32768> windows{};
        const auto count = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
        if (count == 0 || count >= static_cast<UINT>(windows.size()))
            return false;
        environment.insert(QStringLiteral("PATH"), QLibraryInfo::path(QLibraryInfo::BinariesPath));
        environment.insert(QStringLiteral("SystemRoot"), QString::fromWCharArray(windows.data(), count));
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_CASE"), QStringLiteral("environment"));
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_MARKERS"), markers);
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto node_prefix = QStringLiteral("Local\\RCMusic-factory-node-") + token + '-';
        const auto extractor_final = QStringLiteral("Local\\RCMusic-factory-extractor-final-") + token;
        const std::array<QString, 2> event_names{node_prefix + '1', extractor_final};
        for (std::size_t index = 0; index < completion_gates.size(); ++index) {
            completion_gates[index].value =
                CreateEventW(nullptr, TRUE, TRUE, event_names[index].toStdWString().c_str());
            if (!completion_gates[index].value || GetLastError() == ERROR_ALREADY_EXISTS)
                return false;
        }
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_NODE_GATE_PREFIX"), node_prefix);
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_EXTRACTOR_FINAL_GATE"), extractor_final);
        return true;
    }
    bool clean() const { return QDir(runtime).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(); }
    bool noMarkers() const { return QDir(markers).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(); }
    QJsonObject report(const QString& leaf) const {
        QFile file(markers + '/' + leaf);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
    }
};
struct Drain final {
    WindowsDownloadExecution& execution;
    Fixture& fixture;
    Gate* gate{};
    ~Drain() {
        if (gate)
            gate->release();
        execution.shutdown();
        QElapsedTimer elapsed;
        elapsed.start();
        while (!execution.stopped() && elapsed.elapsed() < 10000)
            QTest::qWait(5);
        if (!execution.stopped()) {
            fixture.parent.setAutoRemove(false);
            QTest::qFail("Worker stop was not acknowledged; preserve its owned TEMP.", __FILE__, __LINE__);
        }
    }
};
} // namespace

class WindowsYoutubeExecutionTests final : public QObject {
    Q_OBJECT
  private slots:
    void factoryAndPassiveResolversNeverPrepareOrLaunch() {
        Fixture h;
        QVERIFY(h.initialize());
        int preparations{};
        auto factory = makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment, [&](const auto&) {
            ++preparations;
            return std::shared_ptr<const WindowsYoutubeRuntime>{};
        });
        QVERIFY(factory);
        auto first = factory();
        auto second = factory();
        QVERIFY(first);
        QVERIFY(second);
        QVERIFY(first.get() != second.get());
        QCOMPARE(preparations, 0);
        QVERIFY(h.clean());
        QVERIFY(h.noMarkers());
        first.reset();
        second.reset();
        QCOMPARE(preparations, 0);
    }
    void factorySnapshotsEveryConfigurationStringHashAndRole() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto expected = h.config;
        auto input = h.config;
        std::vector<std::vector<QChar>> strings;
        std::vector<std::vector<char>> hashes;
        strings.reserve(1 + input.artifacts.size() * 2);
        hashes.reserve(input.artifacts.size());
        const auto raw = [&](const QString& text) {
            strings.emplace_back(text.begin(), text.end());
            return QString::fromRawData(strings.back().data(), static_cast<qsizetype>(strings.back().size()));
        };
        input.runtime_parent = raw(input.runtime_parent);
        for (auto& artifact : input.artifacts) {
            artifact.relative_path = raw(artifact.relative_path);
            artifact.source_path = raw(artifact.source_path);
            hashes.emplace_back(artifact.sha256.begin(), artifact.sha256.end());
            artifact.sha256 =
                QByteArray::fromRawData(hashes.back().data(), static_cast<qsizetype>(hashes.back().size()));
        }
        auto state = std::make_shared<Observations>();
        auto factory =
            makeWindowsYoutubeBrokerResolverFactory(input, h.environment, [state, expected](const auto& snapshot) {
                state->observeThread();
                ++state->preparations;
                bool same = snapshot.runtime_parent == expected.runtime_parent &&
                            snapshot.artifacts.size() == expected.artifacts.size();
                for (std::size_t index = 0; same && index < expected.artifacts.size(); ++index) {
                    const auto& left = snapshot.artifacts[index];
                    const auto& right = expected.artifacts[index];
                    same = left.role == right.role && left.relative_path == right.relative_path &&
                           left.source_path == right.source_path && left.sha256 == right.sha256;
                }
                state->copied = same;
                return std::shared_ptr<const WindowsYoutubeRuntime>{};
            });
        QVERIFY(factory);
        for (auto& text : strings)
            std::fill(text.begin(), text.end(), QChar(u'x'));
        for (auto& hash : hashes)
            std::fill(hash.begin(), hash.end(), 'x');
        input.artifacts.front().role = WindowsYoutubeRuntimeRole::Support;
        input.artifacts.clear();
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h};
        QVERIFY(value->start("copied_config", youtube()));
        QTRY_COMPARE(value->poll("copied_config").state, DownloadExecutionState::Failed);
        QCOMPARE(state->preparations.load(), 1);
        QVERIFY(state->copied);
        QVERIFY(state->worker);
        QCOMPARE(state->destinations.load(), 0);
        QVERIFY(h.clean());
    }
    void preparationFailuresStayOnWorkerAndNeverCreateADestination_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("null") << false;
        QTest::newRow("throw") << true;
    }
    void preparationFailuresStayOnWorkerAndNeverCreateADestination() {
        QFETCH(bool, throws);
        Fixture h;
        QVERIFY(h.initialize());
        auto state = std::make_shared<Observations>();
        auto factory = makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment, [state, throws](const auto&) {
            state->observeThread();
            ++state->preparations;
            if (throws)
                throw std::runtime_error("synthetic private details are not returned");
            return std::shared_ptr<const WindowsYoutubeRuntime>{};
        });
        QVERIFY(factory);
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h};
        QCOMPARE(state->preparations.load(), 0);
        QVERIFY(value->start("failed_prepare", youtube()));
        QTRY_COMPARE(value->poll("failed_prepare").state, DownloadExecutionState::Failed);
        QCOMPARE(value->poll("failed_prepare").error, DownloadFailure::Unavailable);
        QCOMPARE(state->preparations.load(), 1);
        QVERIFY(state->worker);
        QCOMPARE(state->destinations.load(), 0);
        QVERIFY(h.clean());
        QVERIFY(h.noMarkers());
    }
    void defaultPreparationRejectsAnIncorrectPinWithoutLaunching() {
        Fixture h;
        QVERIFY(h.initialize());
        h.config.artifacts.front().sha256 = QByteArray(64, '0');
        auto state = std::make_shared<Observations>();
        auto factory = makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment);
        QVERIFY(factory);
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h};
        QVERIFY(value->start("default_failure", youtube()));
        QTRY_COMPARE(value->poll("default_failure").state, DownloadExecutionState::Failed);
        QCOMPARE(state->destinations.load(), 0);
        QVERIFY(h.clean());
        QVERIFY(h.noMarkers());
    }
    void mixedDispatchPreparesOnlyTheConsistentTypedYoutubeJob() {
        Fixture h;
        QVERIFY(h.initialize());
        auto state = std::make_shared<Observations>();
        auto factory = makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment, [state](const auto&) {
            ++state->preparations;
            return std::shared_ptr<const WindowsYoutubeRuntime>{};
        });
        QVERIFY(factory);
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h};
        auto untyped = youtube();
        untyped.youtube.reset();
        QVERIFY(!value->start("untyped", untyped));
        auto incoherent = youtube();
        incoherent.youtube->video_id = "otherVideo1";
        QVERIFY(!value->start("incoherent", incoherent));
        QCOMPARE(state->preparations.load(), 0);
        QVERIFY(value->start("direct_first", direct));
        QTRY_COMPARE(value->poll("direct_first").state, DownloadExecutionState::Failed);
        QCOMPARE(value->poll("direct_first").error, DownloadFailure::UnsupportedMedia);
        QCOMPARE(state->preparations.load(), 0);
        QVERIFY(value->start("youtube_middle", youtube()));
        QTRY_COMPARE(value->poll("youtube_middle").state, DownloadExecutionState::Failed);
        QCOMPARE(value->poll("youtube_middle").error, DownloadFailure::Unavailable);
        QVERIFY(value->start("direct_last", direct));
        QTRY_COMPARE(value->poll("direct_last").state, DownloadExecutionState::Failed);
        QCOMPARE(state->preparations.load(), 1);
        QCOMPARE(state->direct_starts.load(), 2);
        QCOMPARE(state->resolved_starts.load(), 0);
        QVERIFY(h.clean());
        QVERIFY(h.noMarkers());
    }
    void cancelOrShutdownDuringPreparationIsPromptAndPreventsAllLaunches_data() {
        QTest::addColumn<bool>("shutdown");
        QTest::newRow("cancel") << false;
        QTest::newRow("shutdown") << true;
    }
    void cancelOrShutdownDuringPreparationIsPromptAndPreventsAllLaunches() {
        QFETCH(bool, shutdown);
        Fixture h;
        QVERIFY(h.initialize());
        Gate gate;
        auto state = std::make_shared<Observations>();
        auto factory =
            makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment, [state, &gate](const auto& config) {
                state->observeThread();
                ++state->preparations;
                auto files = WindowsYoutubeRuntime::prepare(config);
                state->observed(files);
                gate.enter();
                return files;
            });
        QVERIFY(factory);
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h, &gate};
        QVERIFY(value->start("cancel_prepare", youtube()));
        QVERIFY(gate.wait());
        QVERIFY(gate.blocked());
        QVERIFY(!state->expired());
        QVERIFY(state->worker);
        if (shutdown)
            value->shutdown();
        else
            value->cancel("cancel_prepare");
        QCOMPARE(value->poll("cancel_prepare").state, DownloadExecutionState::Running);
        QVERIFY(!value->stopped());
        QVERIFY(!value->start("must_wait", youtube()));
        QVERIFY(gate.blocked());
        gate.release();
        QTRY_COMPARE(value->poll("cancel_prepare").state, DownloadExecutionState::Cancelled);
        QCOMPARE(state->preparations.load(), 1);
        QCOMPARE(state->destinations.load(), 0);
        QVERIFY(state->expired());
        QVERIFY(h.clean());
        QVERIFY(h.noMarkers());
        value->shutdown();
        QTRY_VERIFY(value->stopped());
    }
    void environmentValuesAreDeepCopiedBeforeCaptureAndReachOnlyThePrivateWorkers() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto expected = h.environment;
        std::vector<std::vector<QChar>> raw;
        const auto keys = h.environment.keys();
        raw.reserve(static_cast<std::size_t>(keys.size()));
        for (const auto& key : keys) {
            const auto text = h.environment.value(key);
            raw.emplace_back(text.begin(), text.end());
            h.environment.insert(key,
                                 QString::fromRawData(raw.back().data(), static_cast<qsizetype>(raw.back().size())));
        }
        auto state = std::make_shared<Observations>();
        auto factory = makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment, [state](const auto& config) {
            state->observeThread();
            ++state->preparations;
            auto files = WindowsYoutubeRuntime::prepare(config);
            state->observed(files);
            return files;
        });
        QVERIFY(factory);
        for (auto& text : raw)
            std::fill(text.begin(), text.end(), QChar(u'x'));
        h.environment.clear();
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h};
        QVERIFY(value->start("copied_env", youtube()));
        QTRY_COMPARE_WITH_TIMEOUT(value->poll("copied_env").state, DownloadExecutionState::Failed, 10000);
        QCOMPARE(value->poll("copied_env").error, DownloadFailure::UnsupportedMedia);
        QCOMPARE(state->resolved_starts.load(), 1);
        QVERIFY(state->valid_capability);
        QVERIFY(state->released_before_handoff);
        QVERIFY(state->worker);
        for (const auto& leaf : {QStringLiteral("extractor.pid.json"), QStringLiteral("node-1.pid.json")}) {
            const auto report = h.report(leaf);
            QCOMPARE(report.value("testCase").toString(), expected.value(QStringLiteral("RC_MUSIC_BROKER_TEST_CASE")));
            QCOMPARE(report.value("path").toString(), expected.value(QStringLiteral("PATH")));
            QCOMPARE(report.value("systemRoot").toString(), expected.value(QStringLiteral("SystemRoot")));
            QCOMPARE(report.value("markerRoot").toString(),
                     expected.value(QStringLiteral("RC_MUSIC_BROKER_TEST_MARKERS")));
        }
        QVERIFY(state->expired());
        QVERIFY(h.clean());
    }
    void sequentialJobsUseIndependentRuntimeLeasesReleasedBeforeHandoff() {
        Fixture h;
        QVERIFY(h.initialize());
        auto state = std::make_shared<Observations>();
        auto factory = makeWindowsYoutubeBrokerResolverFactory(h.config, h.environment, [state](const auto& config) {
            ++state->preparations;
            auto files = WindowsYoutubeRuntime::prepare(config);
            state->observed(files);
            return files;
        });
        QVERIFY(factory);
        auto value = execution(std::move(factory), state);
        QVERIFY(value);
        Drain drain{*value, h};
        for (const auto* id : {"first_runtime", "second_runtime"}) {
            QVERIFY(value->start(id, youtube()));
            QTRY_COMPARE_WITH_TIMEOUT(value->poll(id).state, DownloadExecutionState::Failed, 10000);
            QCOMPARE(value->poll(id).error, DownloadFailure::UnsupportedMedia);
            QVERIFY(state->expired());
            QVERIFY(h.clean());
        }
        const auto directories = state->paths();
        QCOMPARE(directories.size(), qsizetype{2});
        QVERIFY(directories.at(0) != directories.at(1));
        QCOMPARE(state->preparations.load(), 2);
        QCOMPARE(state->resolved_starts.load(), 2);
        QVERIFY(state->released_before_handoff);
        QVERIFY(state->valid_capability);
    }
};
QTEST_GUILESS_MAIN(WindowsYoutubeExecutionTests)
#include "WindowsYoutubeExecutionTests.moc"
