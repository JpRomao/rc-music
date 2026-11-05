#include "adapters/WindowsConfiguredDownloadExecution.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/YoutubeAudioCodec.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <Sddl.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
const DownloadItem direct{"https://synthetic.example.test/audio.mp3", "Synthetic", DownloadMediaKind::Audio};
DownloadItem youtube() {
    YoutubeAudioItem selected{"aB0-_Cd1EF2", "Synthetic title", "Synthetic artist", "", true};
    return {.url = canonicalYoutubeWatchUrl(selected.video_id).toStdString(),
            .title = selected.title,
            .kind = DownloadMediaKind::Audio,
            .youtube = std::move(selected)};
}
bool privateDocumentDirectory(const QString& path) {
    HANDLE raw{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token{raw};
    DWORD size{};
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    if (!size || size > 64 * 1024)
        return false;
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
        return false;
    const auto release_sid = qScopeGuard([&] { LocalFree(sid); });
    const auto text = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR security{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1, &security,
                                                              nullptr))
        return false;
    const auto release_security = qScopeGuard([&] { LocalFree(security); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != FALSE;
}
bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer timer;
    timer.start();
    do {
        if (ready())
            return true;
        QTest::qWait(1);
    } while (timer.elapsed() < 5'000);
    return ready();
}
struct Gate final {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> entered{};
    bool released{};
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
struct Observations final {
    std::atomic<int> networks{}, media{}, youtubes{}, clocks{};
    std::atomic<bool> on_ui{}, matched{};
    void thread() {
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            on_ui = true;
    }
};
struct Fixture final {
    QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music configured 日本-XXXXXX")};
    WindowsDownloadExecutionProfile profile;
    bool initialize() {
        if (!directory.isValid())
            return false;
        profile.journal_directory = directory.filePath(QStringLiteral("private documents"));
        profile.network.runtime_parent = directory.filePath(QStringLiteral("network runtimes"));
        profile.media.runtime_parent = directory.filePath(QStringLiteral("media runtimes"));
        profile.job.workspace = {directory.filePath(QStringLiteral("staging")),
                                 directory.filePath(QStringLiteral("published")), 4096};
        if (!privateDocumentDirectory(profile.journal_directory))
            return false;
        for (const auto& path : {profile.network.runtime_parent, profile.media.runtime_parent,
                                 profile.job.workspace.staging_parent, profile.job.workspace.publish_parent})
            if (!QDir{}.mkdir(path))
                return false;
        const auto source = directory.filePath(QStringLiteral("reviewed-source.bin"));
        const QByteArray bytes{"synthetic authenticated bytes, deliberately not a PE; never execute"};
        QFile file{source};
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(bytes) != bytes.size())
            return false;
        file.close();
        const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        for (auto& artifact : profile.network.artifacts)
            artifact = {source, hash};
        profile.media.source_executable = source;
        profile.media.source_sha256 = hash;
        profile.job.converter.backend = WindowsLocalMediaBackend::MediaFoundation;
        profile.job.converter.ffmpeg_executable = directory.filePath(QStringLiteral("unused-ffmpeg.exe"));
        profile.job.converter.ffprobe_executable = directory.filePath(QStringLiteral("unused-ffprobe.exe"));
        profile.job.converter.media_foundation_executable = directory.filePath(QStringLiteral("unused-media.exe"));
        profile.job.total_timeout_ms = 15'000;
        return true;
    }
    void enableYoutube(const std::shared_ptr<Observations>& state) {
        WindowsYoutubeDownloadProfile youtube;
        youtube.runtime.runtime_parent = directory.filePath(QStringLiteral("unused-youtube-runtime"));
        const auto& source = profile.network.artifacts.front();
        youtube.runtime.artifacts = {
            {WindowsYoutubeRuntimeRole::Extractor, QStringLiteral("extractor/yt-dlp.exe"), source.path, source.sha256},
            {WindowsYoutubeRuntimeRole::Node, QStringLiteral("javascript/node.exe"), source.path, source.sha256},
            {WindowsYoutubeRuntimeRole::EjsCore, QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js"),
             source.path, source.sha256},
            {WindowsYoutubeRuntimeRole::EjsLibrary,
             QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"), source.path, source.sha256}};
        youtube.environment.insert(QStringLiteral("PATH"), QString{});
        youtube.environment.insert(QStringLiteral("SystemRoot"), QStringLiteral("C:/synthetic-system"));
        youtube.epoch_clock = [state] {
            state->thread();
            ++state->clocks;
            return qint64{1'999'999'000};
        };
        profile.youtube = std::move(youtube);
    }
    bool clean() const {
        for (const auto& path :
             {profile.network.runtime_parent, profile.media.runtime_parent, profile.job.workspace.staging_parent,
              profile.job.workspace.publish_parent, profile.journal_directory})
            if (!QDir{path}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
                return false;
        return true;
    }
};
struct Drain final {
    WindowsDownloadExecution& execution;
    Fixture& fixture;
    std::shared_ptr<Gate> gate{};
    ~Drain() {
        if (gate)
            gate->release();
        execution.shutdown();
        if (!waitFor([&] { return execution.stopped(); })) {
            fixture.directory.setAutoRemove(false);
            QTest::qFail("Configured executor did not acknowledge worker stop; preserve TEMP", __FILE__, __LINE__);
        }
    }
};
WindowsDownloadExecutionPreparers rejecting(const std::shared_ptr<Observations>& state) {
    return {.network =
                [state](const auto&) {
                    state->thread();
                    ++state->networks;
                    return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
                },
            .media =
                [state](const auto&) {
                    state->thread();
                    ++state->media;
                    return std::shared_ptr<const WindowsMediaFoundationLaunch>{};
                },
            .youtube =
                [state](const auto&) {
                    state->thread();
                    ++state->youtubes;
                    return std::shared_ptr<const WindowsYoutubeRuntime>{};
                }};
}
struct BorrowedStore final : DownloadOwnershipStore {
    int calls{};
    DownloadOwnershipLoadResult load() override {
        ++calls;
        return {};
    }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord&) override {
        ++calls;
        return DownloadCheckpointSaveStatus::Failed;
    }
};
struct RawStorage final {
    std::vector<std::vector<QChar>> strings;
    std::vector<std::vector<char>> hashes;
    QString text(const QString& value) {
        strings.emplace_back(value.begin(), value.end());
        return QString::fromRawData(strings.back().data(), static_cast<qsizetype>(strings.back().size()));
    }
    QByteArray bytes(const QByteArray& value) {
        hashes.emplace_back(value.begin(), value.end());
        return QByteArray::fromRawData(hashes.back().data(), static_cast<qsizetype>(hashes.back().size()));
    }
    void overwrite() {
        for (auto& text : strings)
            std::fill(text.begin(), text.end(), QChar(u'x'));
        for (auto& hash : hashes)
            std::fill(hash.begin(), hash.end(), 'x');
    }
};
bool sameNetwork(const WindowsDownloadNetworkLaunchConfig& a, const WindowsDownloadNetworkLaunchConfig& b) {
    if (a.runtime_parent != b.runtime_parent)
        return false;
    for (std::size_t index = 0; index < a.artifacts.size(); ++index)
        if (a.artifacts[index].path != b.artifacts[index].path ||
            a.artifacts[index].sha256 != b.artifacts[index].sha256)
            return false;
    return true;
}
} // namespace

class WindowsConfiguredDownloadExecutionTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndUnusedShutdownNeverCallPreparersOrTheClock() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        h.enableYoutube(state);
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state));
        QVERIFY(execution);
        Drain drain{*execution, h};
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->media.load(), 0);
        QCOMPARE(state->youtubes.load(), 0);
        QCOMPARE(state->clocks.load(), 0);
        QVERIFY(h.clean());
        execution->shutdown();
        QVERIFY(waitFor([&] { return execution->stopped(); }));
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->youtubes.load(), 0);
        QCOMPARE(state->clocks.load(), 0);
        QVERIFY(h.clean());
    }
    void borrowedOwnershipStoreIsRejectedWithoutCallingAnyPort() {
        Fixture h;
        QVERIFY(h.initialize());
        BorrowedStore borrowed;
        h.profile.job.ownership_store = &borrowed;
        const auto state = std::make_shared<Observations>();
        QVERIFY(!makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state)));
        QCOMPARE(borrowed.calls, 0);
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->media.load(), 0);
        QCOMPARE(state->youtubes.load(), 0);
        QVERIFY(h.clean());
    }
    void youtubeProfileRequiresAnExplicitEpochClock() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        h.enableYoutube(state);
        h.profile.youtube->epoch_clock = {};
        QVERIFY(!makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state)));
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->youtubes.load(), 0);
        QVERIFY(h.clean());
    }
    void directOnlyProfileDoesNotAcquireYoutubeAdmission() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state));
        QVERIFY(execution);
        Drain drain{*execution, h};
        QVERIFY(!execution->start("unconfigured_youtube", youtube()));
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->youtubes.load(), 0);
        QVERIFY(h.clean());
    }
    void directRequestsInAMixedProfileUseOnlyTheExistingPreparedWorker() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        h.enableYoutube(state);
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state));
        QVERIFY(execution);
        Drain drain{*execution, h};
        QVERIFY(execution->start("direct_job", direct));
        QVERIFY(waitFor([&] { return execution->poll("direct_job").state == DownloadExecutionState::Failed; }));
        QCOMPARE(execution->poll("direct_job").error, DownloadFailure::Unavailable);
        QCOMPARE(state->networks.load(), 1);
        QCOMPARE(state->media.load(), 0);
        QCOMPARE(state->youtubes.load(), 0);
        QCOMPARE(state->clocks.load(), 0);
        QVERIFY(!state->on_ui);
        QVERIFY(h.clean());
    }
    void youtubePreparationFailureNeverFallsBackToDirectAcquisition_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("null") << false;
        QTest::newRow("throw") << true;
    }
    void youtubePreparationFailureNeverFallsBackToDirectAcquisition() {
        QFETCH(bool, throws);
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        h.enableYoutube(state);
        auto preparers = rejecting(state);
        preparers.youtube = [state, throws](const auto&) {
            state->thread();
            ++state->youtubes;
            if (throws)
                throw std::runtime_error{"private synthetic preparer details"};
            return std::shared_ptr<const WindowsYoutubeRuntime>{};
        };
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, std::move(preparers));
        QVERIFY(execution);
        Drain drain{*execution, h};
        QVERIFY(execution->start("youtube_job", youtube()));
        QVERIFY(waitFor([&] { return execution->poll("youtube_job").state == DownloadExecutionState::Failed; }));
        QCOMPARE(execution->poll("youtube_job").error, DownloadFailure::Unavailable);
        QCOMPARE(state->youtubes.load(), 1);
        QVERIFY(state->clocks > 0);
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->media.load(), 0);
        QVERIFY(!state->on_ui);
        QVERIFY(h.clean());
    }
    void configurationSnapshotsNetworkMediaAndJournalBeforeAdmission() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto expected = h.profile;
        auto supplied = expected;
        RawStorage storage;
        supplied.network.runtime_parent = storage.text(supplied.network.runtime_parent);
        for (auto& artifact : supplied.network.artifacts) {
            artifact.path = storage.text(artifact.path);
            artifact.sha256 = storage.bytes(artifact.sha256);
        }
        supplied.media.runtime_parent = storage.text(supplied.media.runtime_parent);
        supplied.media.source_executable = storage.text(supplied.media.source_executable);
        supplied.media.source_sha256 = storage.bytes(supplied.media.source_sha256);
        supplied.job.workspace.staging_parent = storage.text(supplied.job.workspace.staging_parent);
        supplied.job.workspace.publish_parent = storage.text(supplied.job.workspace.publish_parent);
        supplied.job.converter.ffmpeg_executable = storage.text(supplied.job.converter.ffmpeg_executable);
        supplied.job.converter.ffprobe_executable = storage.text(supplied.job.converter.ffprobe_executable);
        supplied.job.converter.media_foundation_executable =
            storage.text(supplied.job.converter.media_foundation_executable);
        supplied.journal_directory = storage.text(supplied.journal_directory);
        const auto state = std::make_shared<Observations>();
        auto preparers = rejecting(state);
        preparers.network = [state, expected](const auto& snapshot) {
            state->thread();
            ++state->networks;
            state->matched = sameNetwork(snapshot, expected.network);
            return WindowsDownloadNetworkLaunch::prepare(snapshot);
        };
        preparers.media = [state, expected](const auto& snapshot) {
            state->thread();
            ++state->media;
            state->matched = state->matched && snapshot.runtime_parent == expected.media.runtime_parent &&
                             snapshot.source_executable == expected.media.source_executable &&
                             snapshot.source_sha256 == expected.media.source_sha256 &&
                             snapshot.app_container == expected.media.app_container &&
                             snapshot.require_app_container == expected.media.require_app_container;
            return std::shared_ptr<const WindowsMediaFoundationLaunch>{};
        };
        auto execution = makeWindowsConfiguredDownloadExecution(supplied, std::move(preparers));
        QVERIFY(execution);
        Drain drain{*execution, h};
        storage.overwrite();
        BorrowedStore borrowed;
        supplied.job.ownership_store = &borrowed;
        QVERIFY(execution->start("copied_config", direct));
        QVERIFY(waitFor([&] { return execution->poll("copied_config").state == DownloadExecutionState::Failed; }));
        QCOMPARE(execution->poll("copied_config").error, DownloadFailure::Unavailable);
        QCOMPARE(state->networks.load(), 1);
        QCOMPARE(state->media.load(), 1);
        QVERIFY(state->matched);
        QCOMPARE(borrowed.calls, 0);
        QVERIFY(!state->on_ui);
        QVERIFY(h.clean());
    }
    void youtubeConfigurationIsSnapshottedBeforeWorkerAdmission() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        h.enableYoutube(state);
        const auto expected = h.profile.youtube->runtime;
        auto supplied = h.profile;
        RawStorage storage;
        supplied.youtube->runtime.runtime_parent = storage.text(supplied.youtube->runtime.runtime_parent);
        for (auto& artifact : supplied.youtube->runtime.artifacts) {
            artifact.relative_path = storage.text(artifact.relative_path);
            artifact.source_path = storage.text(artifact.source_path);
            artifact.sha256 = storage.bytes(artifact.sha256);
        }
        auto preparers = rejecting(state);
        preparers.youtube = [state, expected](const auto& snapshot) {
            state->thread();
            ++state->youtubes;
            bool same = snapshot.runtime_parent == expected.runtime_parent &&
                        snapshot.artifacts.size() == expected.artifacts.size();
            for (std::size_t i = 0; same && i < expected.artifacts.size(); ++i) {
                const auto& a = snapshot.artifacts[i];
                const auto& b = expected.artifacts[i];
                same = a.role == b.role && a.relative_path == b.relative_path && a.source_path == b.source_path &&
                       a.sha256 == b.sha256;
            }
            state->matched = same;
            return std::shared_ptr<const WindowsYoutubeRuntime>{};
        };
        auto execution = makeWindowsConfiguredDownloadExecution(supplied, std::move(preparers));
        QVERIFY(execution);
        Drain drain{*execution, h};
        storage.overwrite();
        supplied.youtube.reset();
        QVERIFY(execution->start("copied_youtube", youtube()));
        QVERIFY(waitFor([&] { return execution->poll("copied_youtube").state == DownloadExecutionState::Failed; }));
        QCOMPARE(state->youtubes.load(), 1);
        QCOMPARE(state->networks.load(), 0);
        QVERIFY(state->matched);
        QVERIFY(!state->on_ui);
        QVERIFY(h.clean());
    }
    void missingOwnedJournalFailsBeforeAnyRuntimePreparer() {
        Fixture h;
        QVERIFY(h.initialize());
        h.profile.journal_directory = h.directory.filePath(QStringLiteral("not-created"));
        const auto state = std::make_shared<Observations>();
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state));
        QVERIFY(execution);
        Drain drain{*execution, h};
        QVERIFY(execution->start("invalid_journal", direct));
        QVERIFY(waitFor([&] { return execution->poll("invalid_journal").state == DownloadExecutionState::Failed; }));
        QCOMPARE(execution->poll("invalid_journal").error, DownloadFailure::InvalidResult);
        QCOMPARE(state->networks.load(), 0);
        QCOMPARE(state->media.load(), 0);
        QVERIFY(!QDir{h.profile.journal_directory}.exists());
    }
    void cancellationDuringPreparationDoesNotLaunchASessionOrCreateWorkspaceFiles() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        const auto gate = std::make_shared<Gate>();
        auto preparers = rejecting(state);
        preparers.network = [state](const auto& snapshot) {
            state->thread();
            ++state->networks;
            return WindowsDownloadNetworkLaunch::prepare(snapshot);
        };
        preparers.media = [state, gate](const auto& snapshot) {
            state->thread();
            ++state->media;
            gate->hold();
            return WindowsMediaFoundationLaunch::prepare(snapshot);
        };
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, std::move(preparers));
        QVERIFY(execution);
        Drain drain{*execution, h, gate};
        QVERIFY(execution->start("cancel_preparation", direct));
        QVERIFY(waitFor([&] { return gate->entered.load(); }));
        execution->cancel("cancel_preparation");
        execution->shutdown();
        QVERIFY(!execution->stopped());
        gate->release();
        QVERIFY(waitFor([&] { return execution->stopped(); }));
        QCOMPARE(execution->poll("cancel_preparation").state, DownloadExecutionState::Cancelled);
        QCOMPARE(state->networks.load(), 1);
        QCOMPARE(state->media.load(), 1);
        QVERIFY(!state->on_ui);
        QVERIFY(h.clean());
    }
    void everyJobRequestsItsOwnPreparedTaskWithoutCachingFailure() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto state = std::make_shared<Observations>();
        auto execution = makeWindowsConfiguredDownloadExecution(h.profile, rejecting(state));
        QVERIFY(execution);
        Drain drain{*execution, h};
        for (const auto& id : {std::string{"first_job"}, std::string{"second_job"}}) {
            QVERIFY(execution->start(id, direct));
            QVERIFY(waitFor([&] { return execution->poll(id).state == DownloadExecutionState::Failed; }));
            QCOMPARE(execution->poll(id).error, DownloadFailure::Unavailable);
        }
        QCOMPARE(state->networks.load(), 2);
        QCOMPARE(state->youtubes.load(), 0);
        QVERIFY(!state->on_ui);
        QVERIFY(h.clean());
    }
};
QTEST_GUILESS_MAIN(WindowsConfiguredDownloadExecutionTests)
#include "WindowsConfiguredDownloadExecutionTests.moc"
