#include "../../qt/support/TestEcdsaSigningKey.hpp"
#include "../support/ConfiguredDownloadsContractProfile.hpp"
#include "app/PlayerDownloadsBuildProfile.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <future>
#include <qt_windows.h>
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
// Writes a signed runtime manifest next to a temporary player image (ADR-0020).
QByteArray signedRuntimeManifest(const QString& application, const test::SigningKey& key, qint64 sequence = 2) {
    QJsonArray artifacts;
    for (const auto& [role, path] :
         std::vector<std::pair<QString, QString>>{{"Extractor", "extractor/yt-dlp.exe"},
                                                  {"Node", "javascript/node.exe"},
                                                  {"EjsCore", "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js"},
                                                  {"EjsLibrary", "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"},
                                                  {"Support", "notices/NEW-NOTICE.txt"}})
        artifacts.append(QJsonObject{{"role", role}, {"path", path}, {"bytes", 1}, {"sha256", QString(64, 'c')}});
    const auto manifest = QJsonDocument(QJsonObject{{"schemaVersion", 1},
                                                    {"purpose", "rcmusic-youtube-runtime-v1"},
                                                    {"sequence", sequence},
                                                    {"ytDlpVersion", "2026.11.01"},
                                                    {"artifacts", artifacts}})
                              .toJson(QJsonDocument::Compact);
    QDir{}.mkpath(application + QStringLiteral("/download-tools"));
    QFile json(application + QStringLiteral("/download-tools/youtube-runtime.json"));
    QFile signature(application + QStringLiteral("/download-tools/youtube-runtime.sig"));
    if (!json.open(QIODevice::WriteOnly) || !signature.open(QIODevice::WriteOnly))
        return {};
    json.write(manifest);
    signature.write(key.sign(manifest));
    return manifest;
}
WindowsPlayerDownloadProfile packagedPrototype() {
    WindowsPlayerDownloadProfile profile;
    const QStringList names{QStringLiteral("RCMusicDownloadWorker.exe"),
                            QStringLiteral("rcmusic-download-network.dll"),
                            QStringLiteral("Qt6Core.dll"),
                            QStringLiteral("Qt6Network.dll"),
                            QStringLiteral("msvcp140.dll"),
                            QStringLiteral("msvcp140_1.dll"),
                            QStringLiteral("vcruntime140.dll"),
                            QStringLiteral("vcruntime140_1.dll"),
                            QStringLiteral("qrcmusicschanneloffline.dll")};
    for (std::size_t i = 0; i < profile.execution.network.artifacts.size(); ++i)
        profile.execution.network.artifacts[i] = {QStringLiteral("download-tools/network/") +
                                                      names.at(static_cast<qsizetype>(i)),
                                                  QByteArray(64, static_cast<char>('0' + i))};
    profile.execution.media.source_executable = QStringLiteral("download-tools/media/RCMusicMediaWorker.exe");
    profile.execution.media.source_sha256 = QByteArray(64, 'a');
    profile.execution.job.network.max_body_bytes = 1234;
    profile.execution.job.total_timeout_ms = 5678;
    profile.execution.job.converter.backend = WindowsLocalMediaBackend::MediaFoundation;
    profile.execution.job.converter.max_process_memory_bytes = 123456;
    profile.execution.youtube.emplace();
    profile.execution.youtube->limits.max_stdout_bytes = 4567;
    const std::vector<std::pair<WindowsYoutubeRuntimeRole, QString>> sources{
        {WindowsYoutubeRuntimeRole::Extractor, QStringLiteral("extractor/yt-dlp.exe")},
        {WindowsYoutubeRuntimeRole::Node, QStringLiteral("javascript/node.exe")},
        {WindowsYoutubeRuntimeRole::EjsCore, QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js")},
        {WindowsYoutubeRuntimeRole::EjsLibrary, QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js")},
        {WindowsYoutubeRuntimeRole::Support, QStringLiteral("notices/NOTICE.txt")}};
    for (const auto& [role, relative] : sources)
        profile.execution.youtube->runtime.artifacts.push_back(
            {role, relative, QStringLiteral("download-tools/youtube/") + relative, QByteArray(64, 'b')});
    profile.execution.youtube->environment.insert(QStringLiteral("PATH"), QStringLiteral("C:/untrusted"));
    profile.execution.youtube->environment.insert(QStringLiteral("NODE_OPTIONS"), QStringLiteral("--require poison"));
    profile.execution.youtube->environment.insert(QStringLiteral("PYTHONPATH"), QStringLiteral("C:/poison"));
    profile.execution.youtube->epoch_clock = [] { return qint64{-1}; };
    profile.options.job_namespace = "preserved-package-namespace";
    profile.options.binding.automatic_polling = false;
    profile.options.native_ingress = WindowsPlayerNativeIngressOptions{.principal = expected_profiles.principal,
                                                                       .youtube = expected_profiles.youtube};
    return profile;
}
struct PackagedRawStorage final {
    std::vector<std::vector<QChar>> texts;
    std::vector<std::vector<char>> pins;
    QString text(const QString& source) {
        texts.emplace_back(source.cbegin(), source.cend());
        return QString::fromRawData(texts.back().data(), static_cast<qsizetype>(texts.back().size()));
    }
    QByteArray pin(const QByteArray& source) {
        pins.emplace_back(source.cbegin(), source.cend());
        return QByteArray::fromRawData(pins.back().data(), static_cast<qsizetype>(pins.back().size()));
    }
    void overwrite() {
        for (auto& text : texts)
            std::fill(text.begin(), text.end(), QChar(u'x'));
        for (auto& pin : pins)
            std::fill(pin.begin(), pin.end(), 'x');
    }
};
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
    void installedComponentGateIsOptInAndItsReaderIsPassive() {
        WindowsPlayerDownloadProfile profile;
        QVERIFY(!profile.requires_installed_component);
        QVERIFY(!profile.component_reader);
        next_profile = profile;
        int reads = 0;
        next_profile->component_reader = [&] {
            ++reads;
            return PlayerDownloadComponentState::Enabled;
        };
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(result);
        QVERIFY(!result->requires_installed_component);
        QVERIFY(result->component_reader);
        QCOMPARE(reads, 0);
    }
    void packagedProfileRequiresInstalledComponentWithoutReadingIt() {
        auto profile = packagedPrototype();
        QVERIFY(!profile.requires_installed_component);
        int reads = 0;
        profile.component_reader = [&] {
            ++reads;
            return PlayerDownloadComponentState::Enabled;
        };
        const auto result =
            makePackagedPlayerDownloadProfile(std::move(profile), QStringLiteral("C:/unused/user"),
                                              QStringLiteral("D:/unused/app"), QStringLiteral("W:/Windows"));
        QVERIFY(result);
        QVERIFY(result->requires_installed_component);
        QVERIFY(result->component_reader);
        QCOMPARE(reads, 0);
    }
    void retainedInstalledComponentProfileOwnsRawPathsPinsNativeIdentityAndEnvironment() {
        auto expected = packagedPrototype();
        expected.requires_installed_component = true;
        expected.checkpoint_directory = QStringLiteral("C:/trusted/checkpoint");
        auto& execution = expected.execution;
        execution.journal_directory = QStringLiteral("C:/trusted/journal");
        execution.network.runtime_parent = QStringLiteral("C:/trusted/network");
        execution.media.runtime_parent = QStringLiteral("C:/trusted/media");
        execution.youtube->runtime.runtime_parent = QStringLiteral("C:/trusted/youtube");
        execution.job.workspace.staging_parent = QStringLiteral("C:/trusted/staging");
        execution.job.workspace.publish_parent = QStringLiteral("C:/trusted/published");
        execution.job.converter.ffmpeg_executable = QStringLiteral("C:/trusted/ffmpeg.exe");
        execution.job.converter.ffprobe_executable = QStringLiteral("C:/trusted/ffprobe.exe");
        execution.job.converter.media_foundation_executable = QStringLiteral("C:/trusted/media-worker.exe");
        execution.youtube->environment.clear();
        execution.youtube->environment.insert(QStringLiteral("SystemRoot"), QStringLiteral("W:/Windows"));
        execution.youtube->environment.insert(QStringLiteral("PATH"), QStringLiteral("C:/trusted/explicit"));
        next_profile = expected;
        PackagedRawStorage raw;
        auto& pending = *next_profile;
        pending.checkpoint_directory = raw.text(pending.checkpoint_directory);
        auto& stored = pending.execution;
        stored.journal_directory = raw.text(stored.journal_directory);
        stored.network.runtime_parent = raw.text(stored.network.runtime_parent);
        for (auto& artifact : stored.network.artifacts) {
            artifact.path = raw.text(artifact.path);
            artifact.sha256 = raw.pin(artifact.sha256);
        }
        stored.media.runtime_parent = raw.text(stored.media.runtime_parent);
        stored.media.source_executable = raw.text(stored.media.source_executable);
        stored.media.source_sha256 = raw.pin(stored.media.source_sha256);
        stored.youtube->runtime.runtime_parent = raw.text(stored.youtube->runtime.runtime_parent);
        for (auto& artifact : stored.youtube->runtime.artifacts) {
            artifact.source_path = raw.text(artifact.source_path);
            artifact.relative_path = raw.text(artifact.relative_path);
            artifact.sha256 = raw.pin(artifact.sha256);
        }
        stored.job.workspace.staging_parent = raw.text(stored.job.workspace.staging_parent);
        stored.job.workspace.publish_parent = raw.text(stored.job.workspace.publish_parent);
        stored.job.converter.ffmpeg_executable = raw.text(stored.job.converter.ffmpeg_executable);
        stored.job.converter.ffprobe_executable = raw.text(stored.job.converter.ffprobe_executable);
        stored.job.converter.media_foundation_executable = raw.text(stored.job.converter.media_foundation_executable);
        stored.youtube->environment.clear();
        for (const auto& key : execution.youtube->environment.keys())
            stored.youtube->environment.insert(raw.text(key), raw.text(execution.youtube->environment.value(key)));
        for (auto* native : {&pending.options.native_ingress->principal, &pending.options.native_ingress->youtube}) {
            native->allowed_origin = raw.text(native->allowed_origin);
            native->endpoint_name = raw.text(native->endpoint_name);
            for (auto& image : native->expected_owner_images)
                image = raw.text(image);
            for (auto& image : native->expected_host_images)
                image = raw.text(image);
        }
        int reads = 0;
        pending.component_reader = [&] {
            ++reads;
            return PlayerDownloadComponentState::Enabled;
        };
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(result);
        QVERIFY(result->requires_installed_component);
        QCOMPARE(reads, 0);
        raw.overwrite();
        QCOMPARE(result->checkpoint_directory, expected.checkpoint_directory);
        const auto& actual = result->execution;
        QCOMPARE(actual.journal_directory, execution.journal_directory);
        QCOMPARE(actual.network.runtime_parent, execution.network.runtime_parent);
        for (std::size_t index = 0; index < actual.network.artifacts.size(); ++index) {
            QCOMPARE(actual.network.artifacts[index].path, execution.network.artifacts[index].path);
            QCOMPARE(actual.network.artifacts[index].sha256, execution.network.artifacts[index].sha256);
        }
        QCOMPARE(actual.media.runtime_parent, execution.media.runtime_parent);
        QCOMPARE(actual.media.source_executable, execution.media.source_executable);
        QCOMPARE(actual.media.source_sha256, execution.media.source_sha256);
        QCOMPARE(actual.youtube->runtime.runtime_parent, execution.youtube->runtime.runtime_parent);
        QCOMPARE(actual.youtube->runtime.artifacts.size(), execution.youtube->runtime.artifacts.size());
        for (std::size_t index = 0; index < actual.youtube->runtime.artifacts.size(); ++index) {
            QCOMPARE(actual.youtube->runtime.artifacts[index].source_path,
                     execution.youtube->runtime.artifacts[index].source_path);
            QCOMPARE(actual.youtube->runtime.artifacts[index].relative_path,
                     execution.youtube->runtime.artifacts[index].relative_path);
            QCOMPARE(actual.youtube->runtime.artifacts[index].sha256,
                     execution.youtube->runtime.artifacts[index].sha256);
        }
        QCOMPARE(actual.job.workspace.staging_parent, execution.job.workspace.staging_parent);
        QCOMPARE(actual.job.workspace.publish_parent, execution.job.workspace.publish_parent);
        QCOMPARE(actual.job.converter.ffmpeg_executable, execution.job.converter.ffmpeg_executable);
        QCOMPARE(actual.job.converter.ffprobe_executable, execution.job.converter.ffprobe_executable);
        QCOMPARE(actual.job.converter.media_foundation_executable, execution.job.converter.media_foundation_executable);
        QCOMPARE(actual.youtube->environment, execution.youtube->environment);
        compareProfile(result->options.native_ingress->principal, expected.options.native_ingress->principal);
        compareProfile(result->options.native_ingress->youtube, expected.options.native_ingress->youtube);
        QVERIFY(result->component_reader);
        QCOMPARE(result->component_reader(), PlayerDownloadComponentState::Enabled);
        QCOMPARE(reads, 1);
    }
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
    void packagedProfileRelocatesOnlySourcesAcrossTwoAbsentUnicodeRoots() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto config = temporary.filePath(QStringLiteral("unused config 日本"));
        const auto prototype = packagedPrototype();
        const auto first_root = temporary.filePath(QStringLiteral("Moved A 日本"));
        const auto second_root = temporary.filePath(QStringLiteral("Moved B espaço"));
        const auto first = makePackagedPlayerDownloadProfile(
            prototype, config, QDir::toNativeSeparators(first_root) + QStringLiteral("\\"),
            QStringLiteral("W:\\Windows\\"));
        const auto second =
            makePackagedPlayerDownloadProfile(prototype, config, second_root, QStringLiteral("W:/Windows"));
        QVERIFY(first);
        QVERIFY(second);
        for (std::size_t i = 0; i < prototype.execution.network.artifacts.size(); ++i) {
            const auto relative = prototype.execution.network.artifacts[i].path;
            QCOMPARE(QDir::fromNativeSeparators(first->execution.network.artifacts[i].path),
                     first_root + '/' + relative);
            QCOMPARE(QDir::fromNativeSeparators(second->execution.network.artifacts[i].path),
                     second_root + '/' + relative);
            QCOMPARE(first->execution.network.artifacts[i].sha256, prototype.execution.network.artifacts[i].sha256);
        }
        QCOMPARE(QDir::fromNativeSeparators(first->execution.media.source_executable),
                 first_root + '/' + prototype.execution.media.source_executable);
        QCOMPARE(first->execution.media.source_sha256, prototype.execution.media.source_sha256);
        for (std::size_t i = 0; i < prototype.execution.youtube->runtime.artifacts.size(); ++i) {
            const auto& original = prototype.execution.youtube->runtime.artifacts[i];
            const auto& rebased = first->execution.youtube->runtime.artifacts[i];
            QCOMPARE(QDir::fromNativeSeparators(rebased.source_path), first_root + '/' + original.source_path);
            QCOMPARE(rebased.relative_path, original.relative_path);
            QCOMPARE(rebased.role, original.role);
            QCOMPARE(rebased.sha256, original.sha256);
        }
        QVERIFY(first->prepare_storage);
        const auto base = config + QStringLiteral("/downloads");
        QCOMPARE(first->checkpoint_directory, base + QStringLiteral("/state"));
        QCOMPARE(second->checkpoint_directory, first->checkpoint_directory);
        QCOMPARE(first->execution.journal_directory, first->checkpoint_directory);
        QCOMPARE(first->execution.network.runtime_parent, base + QStringLiteral("/network"));
        QCOMPARE(first->execution.media.runtime_parent, base + QStringLiteral("/media"));
        QCOMPARE(first->execution.youtube->runtime.runtime_parent, base + QStringLiteral("/youtube"));
        QCOMPARE(first->execution.job.workspace.staging_parent, base + QStringLiteral("/staging"));
        QCOMPARE(first->execution.job.workspace.publish_parent, base + QStringLiteral("/published"));
        QCOMPARE(first->execution.job.network.max_body_bytes, quint64{1234});
        QCOMPARE(first->execution.job.total_timeout_ms, qint64{5678});
        QCOMPARE(first->execution.job.converter.max_process_memory_bytes, quint64{123456});
        QCOMPARE(first->execution.job.converter.backend, prototype.execution.job.converter.backend);
        QCOMPARE(first->execution.youtube->limits.max_stdout_bytes, quint64{4567});
        QCOMPARE(first->options.job_namespace, prototype.options.job_namespace);
        QCOMPARE(first->options.binding.automatic_polling, false);
        compareProfile(first->options.native_ingress->principal, prototype.options.native_ingress->principal);
        compareProfile(first->options.native_ingress->youtube, prototype.options.native_ingress->youtube);
        QVERIFY(!QDir{config}.exists());
        QVERIFY(!QDir{first_root}.exists());
        QVERIFY(!QDir{second_root}.exists());
    }
    void signedRuntimeManifestReplacesTheCompiledInventory() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto application = temporary.filePath(QStringLiteral("app 日本"));
        test::SigningKey key;
        QVERIFY(!signedRuntimeManifest(application, key).isEmpty());
        auto prototype = packagedPrototype();
        prototype.execution.youtube->runtime.artifacts.clear();
        prototype.execution.youtube->signed_runtime = SignedYoutubeRuntimeTrust{key.publicKey(), 2};
        const auto result = makePackagedPlayerDownloadProfile(prototype, temporary.filePath(QStringLiteral("config")),
                                                              application, QStringLiteral("W:/Windows"));
        QVERIFY(result);
        QVERIFY(result->execution.youtube);
        const auto& artifacts = result->execution.youtube->runtime.artifacts;
        QCOMPARE(artifacts.size(), std::size_t{5});
        QCOMPARE(artifacts.front().role, WindowsYoutubeRuntimeRole::Extractor);
        QCOMPARE(QDir::fromNativeSeparators(artifacts.front().source_path),
                 application + QStringLiteral("/download-tools/youtube/extractor/yt-dlp.exe"));
        QCOMPARE(artifacts.back().relative_path, QStringLiteral("notices/NEW-NOTICE.txt"));
        QCOMPARE(artifacts.back().sha256, QByteArray(64, 'c'));
        QCOMPARE(result->execution.youtube->environment.value(QStringLiteral("PATH")), QString{});
        compareProfile(result->options.native_ingress->youtube, prototype.options.native_ingress->youtube);
    }
    void unusableSignedManifestTurnsOffOnlyYoutube_data() {
        QTest::addColumn<int>("defect");
        QTest::newRow("missing files") << 0;
        QTest::newRow("other key") << 1;
        QTest::newRow("tampered manifest") << 2;
        QTest::newRow("below minimum sequence") << 3;
    }
    void unusableSignedManifestTurnsOffOnlyYoutube() {
        QFETCH(int, defect);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto application = temporary.filePath(QStringLiteral("app"));
        test::SigningKey key;
        test::SigningKey other;
        if (defect != 0)
            QVERIFY(!signedRuntimeManifest(application, key, defect == 3 ? 1 : 2).isEmpty());
        if (defect == 2) {
            QFile json(application + QStringLiteral("/download-tools/youtube-runtime.json"));
            QVERIFY(json.open(QIODevice::Append));
            json.write(" ");
        }
        auto prototype = packagedPrototype();
        prototype.execution.youtube->runtime.artifacts.clear();
        prototype.execution.youtube->signed_runtime =
            SignedYoutubeRuntimeTrust{defect == 1 ? other.publicKey() : key.publicKey(), 2};
        const auto result = makePackagedPlayerDownloadProfile(prototype, temporary.filePath(QStringLiteral("config")),
                                                              application, QStringLiteral("W:/Windows"));
        QVERIFY(result);
        QVERIFY(!result->execution.youtube);
        QVERIFY(result->options.native_ingress);
        QVERIFY(result->options.native_ingress->youtube.allowed_origin.isEmpty());
        QVERIFY(result->options.native_ingress->youtube.endpoint_name.isEmpty());
        compareProfile(result->options.native_ingress->principal, prototype.options.native_ingress->principal);
        QCOMPARE(result->execution.network.artifacts.size(), prototype.execution.network.artifacts.size());
    }
    void signedTrustAndACompiledInventoryCannotBothApply() {
        test::SigningKey key;
        auto prototype = packagedPrototype();
        prototype.execution.youtube->signed_runtime = SignedYoutubeRuntimeTrust{key.publicKey(), 1};
        QVERIFY(!makePackagedPlayerDownloadProfile(prototype, QStringLiteral("C:/not-created/config"),
                                                   QStringLiteral("D:/not-created/app"), QStringLiteral("W:/Windows")));
    }
    void packagedRelativeSourcesRejectAmbiguityBeforeJoining_data() {
        QTest::addColumn<QString>("source");
        QTest::newRow("empty") << QString{};
        QTest::newRow("drive absolute") << QStringLiteral("C:/outside/file.exe");
        QTest::newRow("drive relative") << QStringLiteral("C:outside.exe");
        QTest::newRow("UNC") << QStringLiteral("\\\\server\\share\\file.exe");
        QTest::newRow("root relative") << QStringLiteral("/outside.exe");
        QTest::newRow("parent traversal") << QStringLiteral("tools/../outside.exe");
        QTest::newRow("dot segment") << QStringLiteral("tools/./file.exe");
        QTest::newRow("empty segment") << QStringLiteral("tools//file.exe");
        QTest::newRow("ADS") << QStringLiteral("tools/file.exe:stream");
        QTest::newRow("DOS device") << QStringLiteral("tools/NUL.txt");
        QTest::newRow("trailing dot") << QStringLiteral("tools/file.exe.");
        QTest::newRow("control") << (QStringLiteral("tools/file") + QChar{1} + QStringLiteral(".exe"));
        QTest::newRow("wildcard") << QStringLiteral("tools/file?.exe");
    }
    void packagedRelativeSourcesRejectAmbiguityBeforeJoining() {
        QFETCH(QString, source);
        for (int field = 0; field < 3; ++field) {
            auto profile = packagedPrototype();
            if (field == 0)
                profile.execution.network.artifacts.back().path = source;
            else if (field == 1)
                profile.execution.media.source_executable = source;
            else
                profile.execution.youtube->runtime.artifacts.back().source_path = source;
            QVERIFY(!makePackagedPlayerDownloadProfile(profile, QStringLiteral("C:/not-created/config"),
                                                       QStringLiteral("D:/not-created/app"),
                                                       QStringLiteral("W:/Windows")));
        }
    }
    void packagedSourcesRequireLowercaseSha256Pins_data() {
        QTest::addColumn<QByteArray>("pin");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("short") << QByteArray(63, 'a');
        QTest::newRow("long") << QByteArray(65, 'a');
        QTest::newRow("uppercase") << QByteArray(64, 'A');
        QTest::newRow("nonhex") << QByteArray(64, 'g');
        QTest::newRow("NUL") << QByteArray(64, '\0');
    }
    void packagedSourcesRequireLowercaseSha256Pins() {
        QFETCH(QByteArray, pin);
        for (int field = 0; field < 3; ++field) {
            auto profile = packagedPrototype();
            if (field == 0)
                profile.execution.network.artifacts.back().sha256 = pin;
            else if (field == 1)
                profile.execution.media.source_sha256 = pin;
            else
                profile.execution.youtube->runtime.artifacts.back().sha256 = pin;
            QVERIFY(!makePackagedPlayerDownloadProfile(profile, QStringLiteral("C:/not-created/config"),
                                                       QStringLiteral("D:/not-created/app"),
                                                       QStringLiteral("W:/Windows")));
        }
    }
    void packagedApplicationAndWindowsRootsAreLexicallyConstrained_data() {
        QTest::addColumn<QString>("root");
        QTest::newRow("empty") << QString{};
        QTest::newRow("drive relative") << QStringLiteral("C:relative");
        QTest::newRow("root relative") << QStringLiteral("\\relative");
        QTest::newRow("UNC") << QStringLiteral("\\\\server\\share");
        QTest::newRow("device namespace") << QStringLiteral("\\\\?\\C:\\app");
        QTest::newRow("parent traversal") << QStringLiteral("C:/app/../elsewhere");
        QTest::newRow("ADS") << QStringLiteral("C:/app:stream");
        QTest::newRow("device component") << QStringLiteral("C:/NUL/app");
    }
    void packagedApplicationAndWindowsRootsAreLexicallyConstrained() {
        QFETCH(QString, root);
        QVERIFY(!makePackagedPlayerDownloadProfile(packagedPrototype(), QStringLiteral("C:/not-created/config"), root,
                                                   QStringLiteral("W:/Windows")));
        QVERIFY(!makePackagedPlayerDownloadProfile(packagedPrototype(), QStringLiteral("C:/not-created/config"),
                                                   QStringLiteral("D:/not-created/app"), root));
    }
    void packagedProfileOwnsEveryRawPathAndPinBeforeReturning() {
        const auto expected = packagedPrototype();
        auto profile = expected;
        PackagedRawStorage raw;
        for (auto& artifact : profile.execution.network.artifacts) {
            artifact.path = raw.text(artifact.path);
            artifact.sha256 = raw.pin(artifact.sha256);
        }
        profile.execution.media.source_executable = raw.text(profile.execution.media.source_executable);
        profile.execution.media.source_sha256 = raw.pin(profile.execution.media.source_sha256);
        for (auto& artifact : profile.execution.youtube->runtime.artifacts) {
            artifact.source_path = raw.text(artifact.source_path);
            artifact.relative_path = raw.text(artifact.relative_path);
            artifact.sha256 = raw.pin(artifact.sha256);
        }
        for (auto* native : {&profile.options.native_ingress->principal, &profile.options.native_ingress->youtube}) {
            native->allowed_origin = raw.text(native->allowed_origin);
            native->endpoint_name = raw.text(native->endpoint_name);
            for (auto& image : native->expected_owner_images)
                image = raw.text(image);
            for (auto& image : native->expected_host_images)
                image = raw.text(image);
        }
        const auto config = raw.text(QStringLiteral("C:/unused user 日本"));
        const auto app = raw.text(QStringLiteral("D:/unused app espaço"));
        const auto windows = raw.text(QStringLiteral("W:/Windows"));
        const auto result = makePackagedPlayerDownloadProfile(profile, config, app, windows);
        QVERIFY(result);
        raw.overwrite();
        for (std::size_t i = 0; i < expected.execution.network.artifacts.size(); ++i) {
            QCOMPARE(QDir::fromNativeSeparators(result->execution.network.artifacts[i].path),
                     QStringLiteral("D:/unused app espaço/") + expected.execution.network.artifacts[i].path);
            QCOMPARE(result->execution.network.artifacts[i].sha256, expected.execution.network.artifacts[i].sha256);
        }
        QCOMPARE(QDir::fromNativeSeparators(result->execution.media.source_executable),
                 QStringLiteral("D:/unused app espaço/") + expected.execution.media.source_executable);
        QCOMPARE(result->execution.media.source_sha256, expected.execution.media.source_sha256);
        for (std::size_t i = 0; i < expected.execution.youtube->runtime.artifacts.size(); ++i) {
            const auto& source = result->execution.youtube->runtime.artifacts[i];
            const auto& previous = expected.execution.youtube->runtime.artifacts[i];
            QCOMPARE(QDir::fromNativeSeparators(source.source_path),
                     QStringLiteral("D:/unused app espaço/") + previous.source_path);
            QCOMPARE(source.relative_path, previous.relative_path);
            QCOMPARE(source.sha256, previous.sha256);
        }
        compareProfile(result->options.native_ingress->principal, expected.options.native_ingress->principal);
        compareProfile(result->options.native_ingress->youtube, expected.options.native_ingress->youtube);
        QCOMPARE(result->checkpoint_directory, QStringLiteral("C:/unused user 日本/downloads/state"));
        QCOMPARE(QDir::fromNativeSeparators(result->execution.youtube->environment.value(QStringLiteral("SystemRoot"))),
                 QStringLiteral("W:/Windows"));
    }
    void packagedProfileCreatesOnlyExplicitEnvironmentAndClockAndKeepsResolverOptional() {
        const auto before = QDateTime::currentSecsSinceEpoch();
        const auto result =
            makePackagedPlayerDownloadProfile(packagedPrototype(), QStringLiteral("C:/unused/user"),
                                              QStringLiteral("D:/unused/app"), QStringLiteral("W:/Windows"));
        QVERIFY(result);
        const auto& youtube = *result->execution.youtube;
        QCOMPARE(youtube.environment.keys().size(), 2);
        QVERIFY(youtube.environment.contains(QStringLiteral("PATH")));
        QVERIFY(youtube.environment.value(QStringLiteral("PATH")).isEmpty());
        QCOMPARE(QDir::fromNativeSeparators(youtube.environment.value(QStringLiteral("SystemRoot"))),
                 QStringLiteral("W:/Windows"));
        QVERIFY(youtube.epoch_clock);
        const auto observed = youtube.epoch_clock();
        QVERIFY(observed >= before && observed <= QDateTime::currentSecsSinceEpoch());
        auto direct = packagedPrototype();
        direct.execution.youtube.reset();
        direct.options.native_ingress.reset();
        const auto direct_result = makePackagedPlayerDownloadProfile(
            direct, QStringLiteral("C:/unused/user"), QStringLiteral("D:/unused/app"), QStringLiteral("W:/Windows"));
        QVERIFY(direct_result);
        QVERIFY(!direct_result->execution.youtube);
        QVERIFY(!direct_result->options.native_ingress);
    }
    void packagedDriveRootDirectoriesRemainAbsoluteWithoutDoubleSeparators() {
        const auto result = makePackagedPlayerDownloadProfile(packagedPrototype(), QStringLiteral("C:/unused/user"),
                                                              QStringLiteral("D:/"), QStringLiteral("W:/"));
        QVERIFY(result);
        QCOMPARE(QDir::fromNativeSeparators(result->execution.network.artifacts.front().path),
                 QStringLiteral("D:/") + packagedPrototype().execution.network.artifacts.front().path);
        QCOMPARE(QDir::fromNativeSeparators(result->execution.youtube->environment.value(QStringLiteral("SystemRoot"))),
                 QStringLiteral("W:/"));
    }
    void packagedActualPathsIgnoreWorkingDirectoryAndInheritedEnvironment() {
        QTemporaryDir unrelated;
        QVERIFY(unrelated.isValid());
        const auto previous_cwd = QDir::currentPath();
        const std::vector<QByteArray> names{"PATH", "SystemRoot", "NODE_OPTIONS", "PYTHONPATH"};
        std::vector<std::tuple<QByteArray, QByteArray, bool>> saved;
        for (const auto& name : names)
            saved.emplace_back(name, qgetenv(name.constData()), qEnvironmentVariableIsSet(name.constData()));
        const auto restore = qScopeGuard([&] {
            static_cast<void>(QDir::setCurrent(previous_cwd));
            for (const auto& [name, value, present] : saved) {
                if (present)
                    static_cast<void>(qputenv(name.constData(), value));
                else
                    static_cast<void>(qunsetenv(name.constData()));
            }
        });
        QVERIFY(QDir::setCurrent(unrelated.path()));
        for (const auto& name : names)
            QVERIFY(qputenv(name.constData(), "Z:/hostile inherited value"));
        const auto result = makePackagedPlayerDownloadProfile(packagedPrototype(), unrelated.path());
        QVERIFY(result);
        const auto application = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
        QCOMPARE(QDir::fromNativeSeparators(result->execution.network.artifacts.front().path),
                 application + '/' + packagedPrototype().execution.network.artifacts.front().path);
        wchar_t windows[32768]{};
        const auto count = GetWindowsDirectoryW(windows, 32768);
        QVERIFY(count > 0 && count < 32768);
        QCOMPARE(QDir::fromNativeSeparators(result->execution.youtube->environment.value(QStringLiteral("SystemRoot"))),
                 QDir::fromNativeSeparators(QString::fromWCharArray(windows, static_cast<qsizetype>(count))));
        QCOMPARE(result->execution.youtube->environment.keys().size(), 2);
        QVERIFY(result->execution.youtube->environment.value(QStringLiteral("PATH")).isEmpty());
        QVERIFY(!QDir{unrelated.filePath(QStringLiteral("downloads"))}.exists());
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
