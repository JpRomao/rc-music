#include "DownloadNetworkPolicy.hpp"
#include "NativeDownloadCodec.hpp"
#include "YoutubeAudioResolution.hpp"
#include "support/YoutubeResolverTestSupport.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace music_player::m0;

class DownloadNetworkPolicyTests final : public QObject {
    Q_OBJECT

  private slots:
    void aResolvedYoutubeCapabilityPreservesExactSignedBytesWithoutChangingTheMainPolicy() {
        const auto url = QStringLiteral("https://r1---sn-test.googlevideo.com/"
                                        "videoplayback?expire=2000000000&mime=audio%2Fmp4&sig=%7e%41&x=1+2&x=3");
        const auto capability = test::resolvedYoutubeFixture("youtube_job", "aB0-_Cd1EF2", url);
        QVERIFY(capability);
        const auto target = parseResolvedYoutubeNetworkTarget(*capability);
        QVERIFY(target);
        QCOMPARE(target->url, url);
        QCOMPARE(target->host, "r1---sn-test.googlevideo.com");
        QCOMPARE(target->host_header, QByteArray("r1---sn-test.googlevideo.com"));
        QCOMPARE(target->request_target,
                 QByteArray("/videoplayback?expire=2000000000&mime=audio%2Fmp4&sig=%7e%41&x=1+2&x=3"));
        QVERIFY(target->https);
        QVERIFY(!parseDownloadNetworkTarget(url));
        QVERIFY(!resolveDownloadRedirect(*target, QByteArray("?sig=%41")));
    }
    void resolvedYoutubeDefaultPortKeepsDnsAndTlsHostSeparateFromTheSignedTarget() {
        const auto url = QStringLiteral("https://r1.googlevideo.com:443/videoplayback?expire=2000000000&sig=%2f%2F");
        const auto capability = test::resolvedYoutubeFixture("youtube_job", "aB0-_Cd1EF2", url);
        QVERIFY(capability);
        const auto target = parseResolvedYoutubeNetworkTarget(*capability);
        QVERIFY(target);
        QCOMPARE(target->url, url);
        QCOMPARE(target->host, "r1.googlevideo.com");
        QCOMPARE(target->host_header, QByteArray("r1.googlevideo.com"));
        QCOMPARE(target->request_target, QByteArray("/videoplayback?expire=2000000000&sig=%2f%2F"));
    }
    void youtubeRedirectsKeepOnlyTheApprovedHttpsCdnScope_data() {
        QTest::addColumn<QByteArray>("location");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("signed relative query") << QByteArray("?expire=2000000000&sig=%7e%41&x=1+2") << true;
        QTest::newRow("another CDN")
            << QByteArray("https://r2---sn-test.googlevideo.com/videoplayback?expire=2000000000&sig=%2f%2F") << true;
        QTest::newRow("same path") << QByteArray("/videoplayback?expire=2000000000&sig=next") << true;
        QTest::newRow("HTTPS downgrade") << QByteArray("http://r2.googlevideo.com/videoplayback?expire=2000000000&x=1")
                                         << false;
        QTest::newRow("unrelated host") << QByteArray("https://media.example.test/videoplayback?expire=2000000000")
                                        << false;
        QTest::newRow("lookalike host") << QByteArray(
                                               "https://r1.googlevideo.com.evil.test/videoplayback?expire=2000000000")
                                        << false;
        QTest::newRow("YouTube HTML") << QByteArray("https://www.youtube.com/watch?v=aB0-_Cd1EF2") << false;
        QTest::newRow("wrong CDN path") << QByteArray("/manifest.mpd?expire=2000000000") << false;
        QTest::newRow("credentials") << QByteArray("https://user@r1.googlevideo.com/videoplayback?expire=2000000000")
                                     << false;
        QTest::newRow("fragment") << QByteArray("/videoplayback?expire=2000000000&x=1#part") << false;
        QTest::newRow("custom port") << QByteArray("https://r1.googlevideo.com:8443/videoplayback?expire=2000000000")
                                     << false;
        QTest::newRow("encoded traversal") << QByteArray("/a/.%2e/videoplayback?expire=2000000000&x=1") << false;
    }
    void youtubeRedirectsKeepOnlyTheApprovedHttpsCdnScope() {
        QFETCH(QByteArray, location);
        QFETCH(bool, accepted);
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        const auto previous = parseResolvedYoutubeNetworkTarget(*capability);
        QVERIFY(previous);
        const auto next = resolveYoutubeAudioRedirect(*previous, location, *capability);
        QCOMPARE(next.has_value(), accepted);
        if (next) {
            QVERIFY(next->https);
            QVERIFY(!parseDownloadNetworkTarget(next->url));
            if (location.startsWith('?'))
                QCOMPARE(next->request_target, QByteArray("/videoplayback") + location);
        }
    }
    void acceptedUrlPreservesSignedRequestTargetAndHost_data() {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("host");
        QTest::addColumn<QByteArray>("target");
        QTest::addColumn<bool>("https");
        QTest::newRow("signed HTTPS") << QStringLiteral("https://media.example.test/a%2fb%2FB.mp4?sig=%7e%41&x=1+2&x=3")
                                      << QStringLiteral("media.example.test")
                                      << QByteArray{"/a%2fb%2FB.mp4?sig=%7e%41&x=1+2&x=3"} << true;
        QTest::newRow("HTTP") << QStringLiteral("http://media.example.test/a.mp3")
                              << QStringLiteral("media.example.test") << QByteArray{"/a.mp3"} << false;
        QTest::newRow("trailing DNS dot") << QStringLiteral("https://media.example.test./a?")
                                          << QStringLiteral("media.example.test.") << QByteArray{"/a?"} << true;
        QTest::newRow("escaped UTF-8") << QStringLiteral("https://xn--msica-4va.example/a%C3%BAsica.mp3")
                                       << QStringLiteral("xn--msica-4va.example") << QByteArray{"/a%C3%BAsica.mp3"}
                                       << true;
    }

    void acceptedUrlPreservesSignedRequestTargetAndHost() {
        QFETCH(QString, url);
        QFETCH(QString, host);
        QFETCH(QByteArray, target);
        QFETCH(bool, https);
        const auto parsed = parseDownloadNetworkTarget(url);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->url, url);
        QCOMPARE(parsed->host, host);
        QCOMPARE(parsed->host_header, host.toLatin1());
        QCOMPARE(parsed->request_target, target);
        QCOMPARE(parsed->https, https);
    }

    void unsafeOrNoncanonicalUrlsNeverBecomeConnectionTargets_data() {
        QTest::addColumn<QString>("url");
        for (const auto* value : {"",
                                  "https://youtube.com/a",
                                  "https://v.googlevideo.com/a",
                                  "https://youtube.com./a",
                                  "https://a.youtube-nocookie.com/a",
                                  "https://localhost/a",
                                  "https://a.internal/a",
                                  "https://printer.local/a",
                                  "https://10.0.0.1/a",
                                  "https://8.8.8.8/a",
                                  "https://0x7f.1/a",
                                  "https://[2606:4700::1111]/a",
                                  "https://u:p@media.example.test/a",
                                  "file:///C:/a",
                                  "ftp://media.example.test/a",
                                  "//media.example.test/a",
                                  "https://MEDIA.example.test/a",
                                  "https://media.example.test",
                                  "https://media.example.test:443/a",
                                  "https://media.example.test:8443/a",
                                  "https://media.example.test/a#fragment",
                                  "https://media.example.test/a/../b",
                                  "https://media.example.test/a/.%2e/b",
                                  "https://media.example.test/a%2",
                                  "https://media.example.test/a\\b",
                                  "https://media.example.test/a\r\nInjected:x"}) {
            QTest::newRow(value) << QString::fromLatin1(value);
        }
        QTest::newRow("oversized") << QStringLiteral("https://media.example.test/") + QString(8192, QLatin1Char('a'));
        QTest::newRow("raw Unicode") << QStringLiteral("https://media.example.test/música");
    }

    void unsafeOrNoncanonicalUrlsNeverBecomeConnectionTargets() {
        QFETCH(QString, url);
        QVERIFY(!parseDownloadNetworkTarget(url));
    }

    void URLAndResolvedTargetBothRespectAbsoluteSizeLimit() {
        const QString prefix = QStringLiteral("https://media.example.test/");
        const QString maximum = prefix + QString(8192 - prefix.size(), QLatin1Char('a'));
        QVERIFY(parseDownloadNetworkTarget(maximum).has_value());
        QVERIFY(!parseDownloadNetworkTarget(maximum + QLatin1Char('a')));
        const auto previous =
            parseDownloadNetworkTarget(prefix + QString(8000, QLatin1Char('a')) + QStringLiteral("/a"));
        QVERIFY(previous.has_value());
        QVERIFY(!resolveDownloadRedirect(*previous, QByteArray(300, 'b')));
    }

    void nativeAdmissionAndConnectionPolicyShareUrlDecisions() {
        for (const auto& url :
             {QStringLiteral("https://media.example.test/a%7e.mp3?sig=A%2fb"), QStringLiteral("https://youtube.com/a"),
              QStringLiteral("https://10.0.0.1/a"), QStringLiteral("https://media.example.test/a/.%2e/b")}) {
            const QJsonObject item{{QStringLiteral("url"), url},
                                   {QStringLiteral("title"), QStringLiteral("Audio")},
                                   {QStringLiteral("kind"), QStringLiteral("audio")}};
            const QJsonObject request{
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("type"), QStringLiteral("download.request")},
                {QStringLiteral("requestId"), QStringLiteral("01234567-89ab-4cde-8fab-0123456789ab")},
                {QStringLiteral("items"), QJsonArray{item}}};
            QCOMPARE(parseDownloadNetworkTarget(url).has_value(),
                     decodeNativeDownloadRequest(QJsonDocument{request}.toJson(QJsonDocument::Compact)).error ==
                         DownloadSubmissionError::None);
        }
    }

    void redirectsResolveRelativePathsWithoutRewritingSignedBytes_data() {
        QTest::addColumn<QByteArray>("location");
        QTest::addColumn<QString>("expected");
        QTest::newRow("relative") << QByteArray{"next%2f.mp3?sig=%7e%41&x=1+2&x=3"}
                                  << QStringLiteral("https://media.example.test/dir/next%2f.mp3?sig=%7e%41&x=1+2&x=3");
        QTest::newRow("parent") << QByteArray{"../next%2f.mp3?sig=A%2fb"}
                                << QStringLiteral("https://media.example.test/next%2f.mp3?sig=A%2fb");
        QTest::newRow("current") << QByteArray{"./next.mp3"}
                                 << QStringLiteral("https://media.example.test/dir/next.mp3");
        QTest::newRow("root") << QByteArray{"/x//a/../%41.mp3?x=1"}
                              << QStringLiteral("https://media.example.test/x//%41.mp3?x=1");
        QTest::newRow("query") << QByteArray{"?sig=%41&sig=%7e"}
                               << QStringLiteral("https://media.example.test/dir/current%2f.mp3?sig=%41&sig=%7e");
        QTest::newRow("empty query") << QByteArray{"?"}
                                     << QStringLiteral("https://media.example.test/dir/current%2f.mp3?");
        QTest::newRow("other host") << QByteArray{"//cdn.example.test/a%2f.mp3?x=%7e"}
                                    << QStringLiteral("https://cdn.example.test/a%2f.mp3?x=%7e");
        QTest::newRow("absolute") << QByteArray{"https://cdn.example.test/a%2f.mp3"}
                                  << QStringLiteral("https://cdn.example.test/a%2f.mp3");
        QTest::newRow("parent root clamps")
            << QByteArray{"../../../../a.mp3"} << QStringLiteral("https://media.example.test/a.mp3");
        QTest::newRow("trailing parent") << QByteArray{"sub/.."} << QStringLiteral("https://media.example.test/dir/");
    }

    void redirectsResolveRelativePathsWithoutRewritingSignedBytes() {
        QFETCH(QByteArray, location);
        QFETCH(QString, expected);
        const auto original =
            parseDownloadNetworkTarget(QStringLiteral("https://media.example.test/dir/current%2f.mp3?old=1"));
        QVERIFY(original.has_value());
        const auto redirected = resolveDownloadRedirect(*original, location);
        QVERIFY(redirected.has_value());
        QCOMPARE(redirected->url, expected);
        QCOMPARE(redirected->request_target, expected.mid(expected.indexOf(QLatin1Char('/'), 8)).toLatin1());
    }

    void everyRedirectIsRevalidated_data() {
        QTest::addColumn<QByteArray>("location");
        for (const auto* value :
             {"", "http://media.example.test/a", "https://youtube.com/a", "//cdn.googlevideo.com/a", "//a.local/a",
              "//192.168.1.1/a", "//[::1]/a", "//u:p@media.example.test/a", "//media.example.test:443/a",
              "file:///C:/a", "ftp://media.example.test/a", "data:audio/mp3,x", "a#fragment", "a\r\nInjected: value",
              "/.%2e/a", "/a%2", "/a\\b", " https://media.example.test/a", "https://media.example.test/a "}) {
            QTest::newRow(value) << QByteArray{value};
        }
        QTest::newRow("oversized") << QByteArray(8193, 'a');
        QTest::newRow("NUL") << QByteArray{"/a\0.mp3", 7};
        QTest::newRow("non-ASCII") << QByteArray{"/a\xff.mp3", 7};
    }

    void everyRedirectIsRevalidated() {
        QFETCH(QByteArray, location);
        const DownloadNetworkTarget original{QStringLiteral("https://media.example.test/dir/a?old=1"),
                                             QStringLiteral("media.example.test"), QByteArray{"media.example.test"},
                                             QByteArray{"/dir/a?old=1"}, true};
        QVERIFY(!resolveDownloadRedirect(original, location));
    }

    void redirectCannotTrustManuallyForgedPreviousFields() {
        const DownloadNetworkTarget original{QStringLiteral("https://media.example.test/a"),
                                             QStringLiteral("attacker.example.test"), QByteArray{"forged"},
                                             QByteArray{"http://localhost/"}, false};
        const auto next = resolveDownloadRedirect(original, QByteArray{"next.mp3"});
        QVERIFY(next.has_value());
        QCOMPARE(next->url, QStringLiteral("https://media.example.test/next.mp3"));
        QVERIFY(!resolveDownloadRedirect(original, QByteArray{"http://media.example.test/a"}));
        auto invalid = original;
        invalid.url = QStringLiteral("https://localhost/a");
        QVERIFY(!resolveDownloadRedirect(invalid, QByteArray{"https://media.example.test/a"}));
    }

    void httpMayUpgradeToHttps() {
        const auto previous = parseDownloadNetworkTarget(QStringLiteral("http://media.example.test/a"));
        QVERIFY(previous.has_value());
        const auto next = resolveDownloadRedirect(*previous, QByteArray{"https://media.example.test/a"});
        QVERIFY(next.has_value());
        QVERIFY(next->https);
    }

    void publicDestinationPolicyRejectsSpecialRanges_data() {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("allowed");
        for (const auto* value : {"0.0.0.0",
                                  "0.255.255.255",
                                  "10.0.0.0",
                                  "10.255.255.255",
                                  "100.64.0.0",
                                  "100.127.255.255",
                                  "127.0.0.1",
                                  "127.255.255.255",
                                  "169.254.0.0",
                                  "169.254.255.255",
                                  "172.16.0.0",
                                  "172.31.255.255",
                                  "192.0.0.1",
                                  "192.0.0.9",
                                  "192.0.2.255",
                                  "192.31.196.1",
                                  "192.52.193.1",
                                  "192.88.99.255",
                                  "192.168.255.255",
                                  "192.175.48.1",
                                  "198.18.0.0",
                                  "198.19.255.255",
                                  "198.51.100.1",
                                  "203.0.113.255",
                                  "224.0.0.0",
                                  "239.255.255.255",
                                  "240.0.0.0",
                                  "255.255.255.255",
                                  "::",
                                  "::1",
                                  "::8.8.8.8",
                                  "::ffff:127.0.0.1",
                                  "::ffff:8.8.8.8",
                                  "64:ff9b::808:808",
                                  "64:ff9b:1::1",
                                  "100::1",
                                  "100:0:0:1::1",
                                  "2001::1",
                                  "2001:1::1",
                                  "2001:1ff:ffff::1",
                                  "2001:db8::1",
                                  "2002:0808:0808::1",
                                  "2620:4f:8000::1",
                                  "3fff::1",
                                  "3fff:fff:ffff::1",
                                  "5f00::1",
                                  "fc00::1",
                                  "fdff:ffff::1",
                                  "fe80::1",
                                  "fec0::1",
                                  "ff02::1",
                                  "2606:4700::1111%4",
                                  "2000::1",
                                  "2001:1000::1",
                                  "2001:c000::1",
                                  "2003:4000::1",
                                  "2420::1",
                                  "2622::1",
                                  "2d00::1",
                                  "3000::1",
                                  "3ffe:ffff::1",
                                  "invalid"}) {
            QTest::newRow(value) << QString::fromLatin1(value) << false;
        }
        for (const auto* value : {"1.1.1.1",           "8.8.8.8",
                                  "9.255.255.255",     "11.0.0.0",
                                  "100.63.255.255",    "100.128.0.0",
                                  "172.15.255.255",    "172.32.0.0",
                                  "192.0.1.255",       "198.17.255.255",
                                  "198.20.0.0",        "223.255.255.255",
                                  "2001:200::1",       "2001:4860:4860::8888",
                                  "2003:3fff:ffff::1", "2400::1",
                                  "241f:ffff::1",      "2606:4700::1111",
                                  "2620:4f:8001::1",   "263f:ffff::1",
                                  "2a10::1",           "2c0f::1"}) {
            QTest::newRow(value) << QString::fromLatin1(value) << true;
        }
    }

    void publicDestinationPolicyRejectsSpecialRanges() {
        QFETCH(QString, address);
        QFETCH(bool, allowed);
        QCOMPARE(isPublicDownloadAddress(QHostAddress{address}), allowed);
    }

    void entireDnsAnswerSetMustPassBeforeAnyAddressIsUsable() {
        const QHostAddress first{QStringLiteral("8.8.8.8")};
        const QHostAddress second{QStringLiteral("2606:4700::1111")};
        const QList<QHostAddress> addresses{first, first, second, first};
        const auto approved = approveDownloadAddresses(addresses);
        QVERIFY(approved.has_value());
        QCOMPARE(*approved, (QList<QHostAddress>{first, second}));
        QVERIFY(!approveDownloadAddresses({}));
        QVERIFY(!approveDownloadAddresses({first, QHostAddress{QStringLiteral("127.0.0.1")}, second}));
        QVERIFY(!approveDownloadAddresses({QHostAddress{QStringLiteral("fd00::1")}, first}));
        QVERIFY(!approveDownloadAddresses({first, QHostAddress{}}));
    }

    void dnsAnswerCountIsBoundedBeforeDeduplication() {
        const QList<QHostAddress> maximum(32, QHostAddress{QStringLiteral("8.8.8.8")});
        const auto approved = approveDownloadAddresses(maximum);
        QVERIFY(approved.has_value());
        QCOMPARE(approved->size(), qsizetype{1});
        auto excessive = maximum;
        excessive.push_back(excessive.front());
        QVERIFY(!approveDownloadAddresses(excessive));
    }
};

QTEST_APPLESS_MAIN(DownloadNetworkPolicyTests)
#include "DownloadNetworkPolicyTests.moc"
