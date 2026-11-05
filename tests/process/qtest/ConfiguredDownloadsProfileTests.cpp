#include "../support/ConfiguredDownloadsContractProfile.hpp"
#include "app/PlayerDownloadsBuildProfile.hpp"

#include <QTest>

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
