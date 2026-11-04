#include "adapters/BrowserNavigationPolicy.hpp"

#include <QTest>

using namespace music_player::m0;

class BrowserNavigationPolicyTests final : public QObject {
    Q_OBJECT

  private slots:
    void acceptsExplicitHttpsNavigation_data() {
        QTest::addColumn<QString>("url");
        for (const auto* url :
             {"https://example.com", "HTTPS://Example.COM/path?x=1#part", "https://example.com:443/",
              "https://example.com:1/", "https://example.com:8443/", "https://example.com:65535/", "https://127.0.0.1/",
              "https://localhost/", "https://192.168.1.1/", "https://[::1]:8443/", "https://example.com/a%20b?q=x%40y",
              "https://xn--bcher-kva.de/"})
            QTest::newRow(url) << QString::fromLatin1(url);
        QTest::newRow("unicode") << QStringLiteral("https://bücher.de/áudio?nome=日本");
    }
    void acceptsExplicitHttpsNavigation() {
        QFETCH(QString, url);
        QVERIFY(browserNavigationAllowed(url));
        QVERIFY(browserNavigationAllowed(url, true));
    }
    void rejectsUnsafeOrAmbiguousNavigation_data() {
        QTest::addColumn<QString>("url");
        for (const auto* url : {"",
                                "example.com",
                                "//example.com",
                                "http://example.com/",
                                "file:///C:/secret.mp3",
                                "javascript:alert(1)",
                                "data:text/html,hello",
                                "ftp://example.com/",
                                "edge://settings/",
                                "https:/example.com",
                                "https:///example.com",
                                "https://",
                                "https://example.com:",
                                "https://example.com:0/",
                                "https://example.com:65536/",
                                "https://example.com:abc/",
                                "https://user@example.com/",
                                "https://user:secret@example.com/",
                                "https://@example.com/",
                                "https://example.com@evil.test/",
                                "https://%65xample.com/",
                                "https://[::1%25eth0]/",
                                " https://example.com/",
                                "https://example.com/ ",
                                "https://example.com/a b",
                                "https://example.com/\\other",
                                "https:\\example.com",
                                "https://example.com/%",
                                "https://example.com/%0",
                                "https://example.com/%GG",
                                "https://example.com/%00",
                                "https://example.com/%0a",
                                "https://example.com/?x=%7f"})
            QTest::newRow(url) << QString::fromLatin1(url);
        QTest::newRow("newline") << QStringLiteral("https://example.com/\nsecret");
        QTest::newRow("embedded-nul") << (QStringLiteral("https://example.com/") + QChar{0} + QStringLiteral("secret"));
        QTest::newRow("unicode-whitespace") << (QStringLiteral("https://example.com/") + QChar{0x00a0});
        QTest::newRow("invalid-utf16") << (QStringLiteral("https://example.com/") + QChar{0xd800});
    }
    void rejectsUnsafeOrAmbiguousNavigation() {
        QFETCH(QString, url);
        QVERIFY(!browserNavigationAllowed(url));
        QVERIFY(!browserNavigationAllowed(url, true));
    }
    void blankDocumentRequiresExactTrustedInitialization() {
        QVERIFY(!browserNavigationAllowed(QStringLiteral("about:blank")));
        QVERIFY(browserNavigationAllowed(QStringLiteral("about:blank"), true));
        for (const auto* url : {"ABOUT:blank", "about:blank#part", "about:blank?x=1", "about:blank/", "about:blank ",
                                "https://example.com@about:blank"})
            QVERIFY(!browserNavigationAllowed(QString::fromLatin1(url), true));
    }
};

QTEST_GUILESS_MAIN(BrowserNavigationPolicyTests)
#include "BrowserNavigationPolicyTests.moc"
