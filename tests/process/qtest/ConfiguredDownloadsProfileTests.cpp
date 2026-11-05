#include "app/PlayerDownloadsBuildProfile.hpp"

#include <QTest>

using namespace music_player::m0;
namespace {
std::optional<WindowsPlayerDownloadProfile> next_profile;
QString observed_root;
int calls{};
} // namespace
namespace music_player::m0 {
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
        next_profile->options.native_ingress->youtube.allowed_origin = QStringLiteral("youtube");
        const auto result = playerDownloadsBuildProfile(QStringLiteral("C:/trusted/config"));
        QVERIFY(result);
        QVERIFY(result->execution.youtube);
        QCOMPARE(result->options.native_ingress->youtube.allowed_origin, QStringLiteral("youtube"));
        QCOMPARE(calls, 1);
    }
};

QTEST_GUILESS_MAIN(ConfiguredDownloadsProfileTests)
#include "ConfiguredDownloadsProfileTests.moc"
