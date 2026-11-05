#include "../support/ConfiguredDownloadsContractProfile.hpp"
#include "app/PlayerDownloadsBuildProfile.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
std::optional<WindowsPlayerDownloadProfile> next_profile;
QString observed_root;
int calls{};
NativeHostBuildProfiles expected_profiles;
NativeHostBuildProfiles fixtureProfiles() {
    const QStringList owners{QStringLiteral("C:/trusted/player-a.exe"), QStringLiteral("C:/trusted/player-b.exe")};
    return {{QStringLiteral("principal"),
             QStringLiteral("principal-pipe"),
             owners,
             {QStringLiteral("C:/trusted/principal-a.exe"), QStringLiteral("C:/trusted/principal-b.exe")}},
            {QStringLiteral("youtube"),
             QStringLiteral("youtube-pipe"),
             owners,
             {QStringLiteral("C:/trusted/youtube-a.exe"), QStringLiteral("C:/trusted/youtube-b.exe")}}};
}
void matchingProfile() {
    next_profile.emplace();
    next_profile->execution.youtube.emplace();
    next_profile->options.native_ingress = WindowsPlayerNativeIngressOptions{.principal = expected_profiles.principal,
                                                                             .youtube = expected_profiles.youtube};
}
#ifndef RC_MUSIC_TEST_NATIVE_PROFILE_OFF
template <class Operation> auto onWorker(Operation operation) {
    return std::async(std::launch::async, std::move(operation)).get();
}
void compareProfile(const NativeHostConfiguration& actual, const NativeHostConfiguration& expected) {
    QCOMPARE(actual.allowed_origin, expected.allowed_origin);
    QCOMPARE(actual.endpoint_name, expected.endpoint_name);
    QCOMPARE(actual.expected_owner_images, expected.expected_owner_images);
    QCOMPARE(actual.expected_host_images, expected.expected_host_images);
}
#endif
} // namespace
namespace music_player::m0 {
NativeHostBuildProfiles configuredDownloadsContractNativeProfiles() { return expected_profiles; }
std::optional<WindowsPlayerDownloadProfile> configuredDownloadsContractProfile(const QString& root) {
    ++calls;
    observed_root = root;
    return next_profile;
}
} // namespace music_player::m0

class ConfiguredDownloadsProfileTests final : public QObject {
    Q_OBJECT
  private slots:
    void init() {
        next_profile.reset();
        observed_root.clear();
        calls = 0;
        expected_profiles = fixtureProfiles();
    }
#ifndef RC_MUSIC_TEST_NATIVE_PROFILE_OFF
    void providerReceivesTrustedRootOnceAndMayReturnNone() {
        QVERIFY(!playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 1);
        QCOMPARE(observed_root, QStringLiteral("C:/trusted/config"));
    }
    void principalProfileDoesNotRequireYoutubeResolver() {
        next_profile.emplace();
        next_profile->checkpoint_directory = QStringLiteral("C:/trusted/private");
        next_profile->options.native_ingress.emplace();
        next_profile->options.native_ingress->principal = expected_profiles.principal;
        next_profile->options.native_ingress->principal.allowed_origin = QStringLiteral("principal");
        next_profile->execution.journal_directory = QStringLiteral("C:/trusted/journal");
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(result);
        QCOMPARE(result->checkpoint_directory, next_profile->checkpoint_directory);
        QCOMPARE(result->execution.journal_directory, next_profile->execution.journal_directory);
        QVERIFY(!result->execution.youtube);
        QCOMPARE(calls, 1);
    }
    void activeYoutubeIngressWithoutResolverIsRefused_data() {
        QTest::addColumn<QString>("origin");
        QTest::addColumn<QString>("endpoint");
        QTest::newRow("origin") << QStringLiteral("youtube") << QString{};
        QTest::newRow("endpoint") << QString{} << QStringLiteral("youtube");
        QTest::newRow("both") << QStringLiteral("youtube") << QStringLiteral("youtube");
    }
    void activeYoutubeIngressWithoutResolverIsRefused() {
        QFETCH(QString, origin);
        QFETCH(QString, endpoint);
        next_profile.emplace();
        next_profile->options.native_ingress.emplace();
        next_profile->options.native_ingress->youtube.allowed_origin = origin;
        next_profile->options.native_ingress->youtube.endpoint_name = endpoint;
        QVERIFY(!playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 1);
    }
    void explicitYoutubeResolverPreservesSeparateIngressProfile() {
        next_profile.emplace();
        next_profile->execution.youtube.emplace();
        next_profile->options.native_ingress.emplace();
        next_profile->options.native_ingress->youtube = expected_profiles.youtube;
        next_profile->options.native_ingress->youtube.allowed_origin = QStringLiteral("youtube");
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(result);
        QVERIFY(result->execution.youtube);
        QCOMPARE(result->options.native_ingress->youtube.allowed_origin, QStringLiteral("youtube"));
        QCOMPARE(calls, 1);
    }
    void compiledHelperCopiesBothProfilesWithoutCallingTheProvider() {
        const auto profiles = compiledPlayerNativeIngressProfiles();
        QVERIFY(profiles);
        compareProfile(profiles->principal, expected_profiles.principal);
        compareProfile(profiles->youtube, expected_profiles.youtube);
        QVERIFY(!profiles->principal_peer_check_factory);
        QVERIFY(!profiles->youtube_peer_check_factory);
        QCOMPARE(calls, 0);
    }
    void activeYoutubeProfileMustMatchEveryCompiledIdentityField_data() {
        QTest::addColumn<int>("field");
        QTest::newRow("origin") << 0;
        QTest::newRow("endpoint") << 1;
        QTest::newRow("owner-image-allowset") << 2;
        QTest::newRow("host-image-allowset") << 3;
    }
    void activeYoutubeProfileMustMatchEveryCompiledIdentityField() {
        QFETCH(int, field);
        matchingProfile();
        auto& actual = next_profile->options.native_ingress->youtube;
        if (field == 0)
            actual.allowed_origin = QStringLiteral("different-youtube");
        else if (field == 1)
            actual.endpoint_name = QStringLiteral("different-pipe");
        else if (field == 2)
            actual.expected_owner_images = {QStringLiteral("C:/other/player.exe")};
        else
            actual.expected_host_images = {QStringLiteral("C:/other/youtube.exe")};
        QVERIFY(!playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 1);
    }
    void principalIdentityAlsoMustMatchTheCompiledProfile() {
        matchingProfile();
        next_profile->options.native_ingress->principal.allowed_origin = QStringLiteral("different-principal");
        QVERIFY(!playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 1);
    }
    void endpointAndImageAllowsetsIgnoreCaseAndImageOrder() {
        matchingProfile();
        for (auto* actual :
             {&next_profile->options.native_ingress->principal, &next_profile->options.native_ingress->youtube}) {
            actual->endpoint_name = actual->endpoint_name.toUpper();
            actual->expected_owner_images.swapItemsAt(0, 1);
            actual->expected_host_images.swapItemsAt(0, 1);
            for (auto& image : actual->expected_owner_images)
                image = image.toUpper();
            for (auto& image : actual->expected_host_images)
                image = image.toUpper();
        }
        QVERIFY(playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 1);
    }
    void providerCanOptOutOfCompiledActiveChannels() {
        next_profile.emplace();
        QVERIFY(playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        next_profile->options.native_ingress.emplace();
        next_profile->options.native_ingress->principal = expected_profiles.principal;
        const auto principal_only = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(principal_only);
        QVERIFY(!principal_only->execution.youtube);
        QVERIFY(principal_only->options.native_ingress->youtube.allowed_origin.isEmpty());
        QCOMPARE(calls, 2);
    }
    void peerCheckCallbacksDoNotChangeCompiledIdentityMatching() {
        matchingProfile();
        next_profile->options.native_ingress->principal_peer_check_factory = [] {
            return std::unique_ptr<NativeDownloadPeerCheck>{};
        };
        next_profile->options.native_ingress->youtube_peer_check_factory = [] {
            return std::unique_ptr<NativeDownloadPeerCheck>{};
        };
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(result);
        QVERIFY(result->options.native_ingress->principal_peer_check_factory);
        QVERIFY(result->options.native_ingress->youtube_peer_check_factory);
    }
    void compiledDisabledChannelCannotBeActivatedByTheProvider_data() {
        QTest::addColumn<bool>("youtube");
        QTest::newRow("principal disabled") << false;
        QTest::newRow("youtube disabled") << true;
    }
    void compiledDisabledChannelCannotBeActivatedByTheProvider() {
        QFETCH(bool, youtube);
        matchingProfile();
        if (youtube)
            expected_profiles.youtube = {};
        else
            expected_profiles.principal = {};
        QVERIFY(!playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 1);
    }
    void twoCompiledDisabledChannelsOfferNoIngressProfile() {
        expected_profiles = {};
        QVERIFY(!compiledPlayerNativeIngressProfiles());
        next_profile.emplace();
        QVERIFY(playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
    }
    void storageOptInDerivesOneFixedLayoutWithoutChangingLimitsPinsOrOptions() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        next_profile.emplace();
        next_profile->prepare_storage = true;
        next_profile->checkpoint_directory = QStringLiteral("C:/ignored/checkpoint");
        next_profile->execution.journal_directory = QStringLiteral("C:/ignored/journal");
        next_profile->execution.network.artifacts.front() = {QStringLiteral("C:/trusted/network.bin"),
                                                             QByteArray(64, 'a')};
        next_profile->execution.media.source_executable = QStringLiteral("C:/trusted/media.exe");
        next_profile->execution.media.source_sha256 = QByteArray(64, 'b');
        next_profile->execution.job.network.max_body_bytes = 1234;
        next_profile->execution.job.total_timeout_ms = 5678;
        next_profile->execution.youtube.emplace();
        next_profile->execution.youtube->limits.max_stdout_bytes = 4321;
        next_profile->options.job_namespace = "preserved";
        // An explicitly disabled ingress must not return before layout is applied.
        next_profile->options.native_ingress.emplace();
        const auto result = playerDownloadsBuildProfile(root.path());
        QVERIFY(result);
        const auto base = root.path() + QStringLiteral("/downloads");
        QCOMPARE(result->checkpoint_directory, base + QStringLiteral("/state"));
        QCOMPARE(result->execution.journal_directory, result->checkpoint_directory);
        QCOMPARE(result->execution.network.runtime_parent, base + QStringLiteral("/network"));
        QCOMPARE(result->execution.media.runtime_parent, base + QStringLiteral("/media"));
        QCOMPARE(result->execution.youtube->runtime.runtime_parent, base + QStringLiteral("/youtube"));
        QCOMPARE(result->execution.job.workspace.staging_parent, base + QStringLiteral("/staging"));
        QCOMPARE(result->execution.job.workspace.publish_parent, base + QStringLiteral("/published"));
        QCOMPARE(result->execution.network.artifacts.front().path, QStringLiteral("C:/trusted/network.bin"));
        QCOMPARE(result->execution.network.artifacts.front().sha256, QByteArray(64, 'a'));
        QCOMPARE(result->execution.media.source_executable, QStringLiteral("C:/trusted/media.exe"));
        QCOMPARE(result->execution.media.source_sha256, QByteArray(64, 'b'));
        QCOMPARE(result->execution.job.network.max_body_bytes, quint64{1234});
        QCOMPARE(result->execution.job.total_timeout_ms, qint64{5678});
        QCOMPARE(result->execution.youtube->limits.max_stdout_bytes, quint64{4321});
        QCOMPARE(result->options.job_namespace, std::string{"preserved"});
        QVERIFY(!QDir{base}.exists());
    }
    void storageRemainsOptInAndPreservesExistingCustomPathsByDefault() {
        next_profile.emplace();
        next_profile->checkpoint_directory = QStringLiteral("C:/existing/state");
        next_profile->execution.journal_directory = QStringLiteral("C:/existing/journal");
        next_profile->execution.network.runtime_parent = QStringLiteral("C:/existing/network");
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/ignored/root"));
        QVERIFY(result);
        QVERIFY(!result->prepare_storage);
        QCOMPARE(result->checkpoint_directory, next_profile->checkpoint_directory);
        QCOMPARE(result->execution.journal_directory, next_profile->execution.journal_directory);
        QCOMPARE(result->execution.network.runtime_parent, next_profile->execution.network.runtime_parent);
    }
    void checkpointFactoryAndStoreConstructionArePassiveAndSnapshotRawConfigRoot() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto expected = root.path();
        std::vector<QChar> bytes(expected.begin(), expected.end());
        const auto raw = QString::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size()));
        WindowsPlayerDownloadProfile profile;
        profile.prepare_storage = true;
        std::atomic<int> preparations{};
        std::atomic<bool> on_ui{}, matched{};
        const auto ui = std::this_thread::get_id();
        const auto factory = makePlayerDownloadCheckpointFactory(profile, raw, [&](const QString& observed) {
            ++preparations;
            on_ui = std::this_thread::get_id() == ui;
            matched = observed == expected;
            return false;
        });
        QVERIFY(factory);
        std::fill(bytes.begin(), bytes.end(), QChar(u'x'));
        QCOMPARE(preparations.load(), 0);
        QVERIFY(onWorker([factory] { return static_cast<bool>(factory()); }));
        QCOMPARE(preparations.load(), 0);
        QVERIFY(!QDir{expected + QStringLiteral("/downloads")}.exists());
        const auto loaded = onWorker([factory] { return factory()->load(); });
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(preparations.load(), 1);
        QVERIFY(matched);
        QVERIFY(!on_ui);
    }
    void failedStorageLoadsCanRetryButSavesNeverPrepareAndSuccessIsCached() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        WindowsPlayerDownloadProfile profile;
        profile.prepare_storage = true;
        std::atomic<int> preparations{};
        const auto factory = makePlayerDownloadCheckpointFactory(profile, root.path(), [&](const QString& path) {
            const auto attempt = ++preparations;
            if (attempt == 1)
                return false;
            if (attempt == 2)
                throw std::runtime_error{"synthetic private storage error"};
            return prepareWindowsDownloadStorage(path);
        });
        QVERIFY(factory);
        struct Results {
            DownloadCheckpointSaveStatus first_save{}, second_save{}, final_save{};
            DownloadCheckpointLoadStatus first_load{}, second_load{}, healthy_load{}, cached_load{};
            int calls_after_first_save{}, calls_after_second_save{};
        };
        const auto observed = onWorker([&] {
            auto store = factory();
            Results result;
            result.first_save = store->save({});
            result.calls_after_first_save = preparations.load();
            result.first_load = store->load().status;
            result.second_save = store->save({});
            result.calls_after_second_save = preparations.load();
            result.second_load = store->load().status;
            result.healthy_load = store->load().status;
            result.final_save = store->save({});
            result.cached_load = store->load().status;
            return result;
        });
        QCOMPARE(observed.first_save, DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(observed.calls_after_first_save, 0);
        QCOMPARE(observed.first_load, DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(observed.second_save, DownloadCheckpointSaveStatus::Failed);
        QCOMPARE(observed.calls_after_second_save, 1);
        QCOMPARE(observed.second_load, DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(observed.healthy_load, DownloadCheckpointLoadStatus::Missing);
        QCOMPARE(observed.final_save, DownloadCheckpointSaveStatus::Saved);
        QCOMPARE(observed.cached_load, DownloadCheckpointLoadStatus::Loaded);
        QCOMPARE(preparations.load(), 3);
    }
    void nullStoragePreparerCannotCreateOrAcknowledgeHealthyStorage() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        WindowsPlayerDownloadProfile profile;
        profile.prepare_storage = true;
        const auto factory = makePlayerDownloadCheckpointFactory(profile, root.path(), {});
        QVERIFY(factory);
        const auto observed = onWorker([factory] {
            auto store = factory();
            return std::pair{store->load().status, store->save({})};
        });
        QCOMPARE(observed.first, DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(observed.second, DownloadCheckpointSaveStatus::Failed);
        QVERIFY(!QDir{root.filePath(QStringLiteral("downloads"))}.exists());
    }
    void successfulStoragePreparationNeverRepairsAChangedDirectoryOnLaterLoad() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        WindowsPlayerDownloadProfile profile;
        profile.prepare_storage = true;
        std::atomic<int> preparations{};
        const auto factory = makePlayerDownloadCheckpointFactory(profile, root.path(), [&](const QString& path) {
            ++preparations;
            return prepareWindowsDownloadStorage(path);
        });
        QVERIFY(factory);
        const auto state_path = root.filePath(QStringLiteral("downloads/state"));
        const auto observed = onWorker([&] {
            auto store = factory();
            const auto first = store->load().status;
            const bool removed = QDir{}.rmdir(state_path);
            return std::tuple{first, removed, store->load().status};
        });
        QCOMPARE(std::get<0>(observed), DownloadCheckpointLoadStatus::Missing);
        QVERIFY(std::get<1>(observed));
        QCOMPARE(std::get<2>(observed), DownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(preparations.load(), 1);
        QVERIFY(!QDir{state_path}.exists());
    }
    void legacyCheckpointFactorySnapshotsItsPathAndNeverCallsThePreparer() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto checkpoint = root.filePath(QStringLiteral("downloads/state"));
        std::vector<QChar> bytes(checkpoint.begin(), checkpoint.end());
        WindowsPlayerDownloadProfile profile;
        profile.checkpoint_directory = QString::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size()));
        std::atomic<int> preparations{};
        const auto factory =
            makePlayerDownloadCheckpointFactory(profile, QStringLiteral("C:/ignored/config"), [&](const QString&) {
                ++preparations;
                return false;
            });
        QVERIFY(factory);
        std::fill(bytes.begin(), bytes.end(), QChar(u'x'));
        QVERIFY(onWorker([path = root.path()] { return prepareWindowsDownloadStorage(path); }));
        QCOMPARE(onWorker([factory] { return factory()->load().status; }), DownloadCheckpointLoadStatus::Missing);
        QCOMPARE(preparations.load(), 0);
    }
    void storageOptInUsesTheSameFixedStateDespiteAnUnappliedCustomCheckpoint() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto custom = root.filePath(QStringLiteral("custom"));
        QVERIFY(QDir{}.mkdir(custom));
        QFile sentinel{custom + QStringLiteral("/preserve.txt")};
        QVERIFY(sentinel.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(sentinel.write("unchanged"), qint64{9});
        sentinel.close();
        WindowsPlayerDownloadProfile profile;
        profile.prepare_storage = true;
        profile.checkpoint_directory = custom;
        const auto factory = makePlayerDownloadCheckpointFactory(profile, root.path());
        QVERIFY(factory);
        QCOMPARE(onWorker([factory] { return factory()->load().status; }), DownloadCheckpointLoadStatus::Missing);
        QVERIFY(QDir{root.filePath(QStringLiteral("downloads/state"))}.exists());
        QVERIFY(!QFile::exists(custom + QStringLiteral("/download-checkpoint.json")));
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArray{"unchanged"});
    }
#else
    void nativeOffHasNoCompiledProfileAndRefusesActiveProviderChannels() {
        QVERIFY(!compiledPlayerNativeIngressProfiles());
        matchingProfile();
        QVERIFY(!playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        next_profile->options.native_ingress.reset();
        QVERIFY(playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config")));
        QCOMPARE(calls, 2);
    }
#endif
};

QTEST_GUILESS_MAIN(ConfiguredDownloadsProfileTests)
#include "ConfiguredDownloadsProfileTests.moc"
