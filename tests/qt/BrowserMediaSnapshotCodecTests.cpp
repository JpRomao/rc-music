#include "adapters/BrowserMediaSnapshotCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace music_player::m0;

namespace {
QJsonObject item(const QString& url = QStringLiteral("https://cdn.example/song.mp3"),
                 const QString& title = QStringLiteral("Uma música"), const QString& kind = QStringLiteral("audio")) {
    return {{QStringLiteral("url"), url}, {QStringLiteral("title"), title}, {QStringLiteral("kind"), kind}};
}
QJsonObject snapshot(const QJsonArray& candidates = {item()}) {
    return {{QStringLiteral("pageUrl"), QStringLiteral("https://page.example/list")},
            {QStringLiteral("candidates"), candidates},
            {QStringLiteral("truncated"), false}};
}
QByteArray bytes(const QJsonObject& object) { return QJsonDocument{object}.toJson(QJsonDocument::Compact); }
} // namespace

class BrowserMediaSnapshotCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void pageEligibilityPreservesAValidFragmentWithoutGrantingARequest() {
        QVERIFY(browserMediaPageAllowed(QStringLiteral("https://page.example/")));
        QVERIFY(browserMediaPageAllowed(QStringLiteral("https://page.example/list?q=%2f#part-2")));
        QVERIFY(!browserMediaPageAllowed(QStringLiteral("https://page.example/list#bad%escape")));
        QVERIFY(!browserMediaPageAllowed(QStringLiteral("https://www.youtube.com/watch?v=x#part")));
        QVERIFY(!browserMediaPageAllowed(QStringLiteral("https://localhost/x#part")));
    }
    void emptySnapshotIsDataWithoutAuthority() {
        const auto decoded = decodeBrowserMediaSnapshot(bytes(snapshot({})));
        QVERIFY(decoded);
        QCOMPARE(decoded->page_url, QStringLiteral("https://page.example/list"));
        QVERIFY(decoded->candidates.empty());
        QVERIFY(!decoded->truncated);
    }
    void signedUrlsUnicodeTitlesAndKindsRemainExact() {
        const QString signed_url = QStringLiteral("https://cdn.example/video.mp4?sig=A%2fb%2Fz&x=1&x=2&plus=+%20");
        const QString title = QString::fromUtf8("ação 日本 ☕ 🎵");
        auto raw = snapshot({item(signed_url, title, QStringLiteral("video"))});
        raw.insert(QStringLiteral("pageUrl"), QStringLiteral("https://page.example/list?q=%2f#part-2"));
        raw.insert(QStringLiteral("truncated"), true);
        const auto decoded = decodeBrowserMediaSnapshot(bytes(raw));
        QVERIFY(decoded);
        QCOMPARE(decoded->page_url, raw.value(QStringLiteral("pageUrl")).toString());
        QCOMPARE(decoded->candidates.size(), std::size_t{1});
        QCOMPARE(decoded->candidates[0],
                 (DownloadItem{signed_url.toStdString(), title.toStdString(), DownloadMediaKind::Video}));
        QVERIFY(decoded->truncated);
    }
    void ineligibleSourcesAreFilteredWithoutLosingEligibleMedia_data() {
        QTest::addColumn<QString>("url");
        for (const auto& url : {"https://www.youtube.com/watch?v=x", "https://r1.googlevideo.com/videoplayback",
                                "https://youtube-nocookie.com/embed/x", "https://youtu.be/x", "https://localhost/x.mp3",
                                "https://device.internal/x.mp3", "https://192.168.1.1/x.mp3", "https://0x7f.1/x.mp3",
                                "https://[::1]/x.mp3", "https://site.example:8443/x.mp3", "file:///c:/song.mp3",
                                "blob:https://site.example/id", "data:audio/mp3;base64,x", "javascript:alert(1)",
                                "https://user:password@site.example/x.mp3", "https://example.com/a/../x.mp3"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void ineligibleSourcesAreFilteredWithoutLosingEligibleMedia() {
        QFETCH(QString, url);
        const auto decoded = decodeBrowserMediaSnapshot(bytes(snapshot({item(url), item()})));
        QVERIFY(decoded);
        QCOMPARE(decoded->candidates.size(), std::size_t{1});
        QCOMPARE(decoded->candidates.front().url, std::string{"https://cdn.example/song.mp3"});
    }
    void httpCandidatesRemainEligibleOnAnHttpsPage() {
        const auto decoded =
            decodeBrowserMediaSnapshot(bytes(snapshot({item(QStringLiteral("http://cdn.example/song.mp3"))})));
        QVERIFY(decoded);
        QCOMPARE(decoded->candidates[0].url, std::string{"http://cdn.example/song.mp3"});
    }
    void pageCannotClaimAnUnsupportedContext_data() {
        QTest::addColumn<QString>("page");
        for (const auto& page :
             {"http://page.example/list", "https://www.youtube.com/watch?v=x", "https://page.local/list",
              "https://127.0.0.1/list", "https://[::1]/list", "https://user@page.example/list",
              "https://page.example:444/list", "file:///c:/page.html", "https://page.example/no%percent",
              "https://page.example/list#bad%escape", "https://page.example/list#bad\nfragment"})
            QTest::newRow(page) << QString::fromLatin1(page);
    }
    void pageCannotClaimAnUnsupportedContext() {
        QFETCH(QString, page);
        auto raw = snapshot();
        raw.insert(QStringLiteral("pageUrl"), page);
        QVERIFY(!decodeBrowserMediaSnapshot(bytes(raw)));
    }
    void titleSanitizationMatchesTheMainExtension() {
        auto payload = bytes(snapshot({item(QStringLiteral("https://cdn.example/a.mp3"),
                                            QString::fromUtf8("  A\x01") + QChar{0x202e} + QStringLiteral(" B  ")),
                                       item(QStringLiteral("https://cdn.example/v.mp4"),
                                            QString{QChar{0xfeff}} + QChar{0x2066}, QStringLiteral("video")),
                                       item(QStringLiteral("https://cdn.example/empty.mp3"), QStringLiteral(""))}));
        // The shared lexical boundary rejects raw leading string BOMs because
        // Qt may strip them. The escaped scalar preserves the intended value.
        payload.replace(QByteArray{"\xef\xbb\xbf"}, QByteArray{"\\uFEFF"});
        const auto decoded = decodeBrowserMediaSnapshot(payload);
        QVERIFY(decoded);
        QCOMPARE(decoded->candidates[0].title, std::string{"A B"});
        QCOMPARE(decoded->candidates[1].title, QString::fromUtf8("Vídeo").toStdString());
        QCOMPARE(decoded->candidates[2].title, QString::fromUtf8("Áudio").toStdString());
    }
    void duplicateEligibleUrlIsAmbiguousEvenWithDifferentTitles() {
        QVERIFY(!decodeBrowserMediaSnapshot(bytes(
            snapshot({item(), item(QStringLiteral("https://cdn.example/song.mp3"), QStringLiteral("Different"))}))));
    }
    void candidateAndTextLimitsAreInclusive() {
        QJsonArray candidates;
        for (int index = 0; index < 100; ++index)
            candidates.append(item(QStringLiteral("https://cdn.example/%1.mp3").arg(index)));
        const auto full = decodeBrowserMediaSnapshot(bytes(snapshot(candidates)));
        QVERIFY(full);
        QCOMPARE(full->candidates.size(), std::size_t{100});
        candidates.append(item(QStringLiteral("https://cdn.example/overflow.mp3")));
        QVERIFY(!decodeBrowserMediaSnapshot(bytes(snapshot(candidates))));
        const QString prefix = QStringLiteral("https://cdn.example/");
        const QString url = prefix + QString(8192 - prefix.size(), QLatin1Char('a'));
        const QString title(200, QLatin1Char('t'));
        QVERIFY(decodeBrowserMediaSnapshot(bytes(snapshot({item(url, title)}))));
        QVERIFY(!decodeBrowserMediaSnapshot(bytes(snapshot({item(url + QLatin1Char('a'), title)}))));
        QVERIFY(!decodeBrowserMediaSnapshot(bytes(snapshot({item(prefix, title + QLatin1Char('t'))}))));
        auto raw = snapshot();
        raw.insert(QStringLiteral("pageUrl"), url);
        QVERIFY(decodeBrowserMediaSnapshot(bytes(raw)));
        raw.insert(QStringLiteral("pageUrl"), url + QLatin1Char('a'));
        QVERIFY(!decodeBrowserMediaSnapshot(bytes(raw)));
    }
    void absolutePayloadLimitIsInclusive() {
        auto payload = bytes(snapshot());
        payload.append(QByteArray(1024 * 1024 - payload.size(), ' '));
        QVERIFY(decodeBrowserMediaSnapshot(payload));
        payload.append(' ');
        QVERIFY(!decodeBrowserMediaSnapshot(payload));
    }
    void onlyTheClosedSnapshotDtoIsAccepted_data() {
        QTest::addColumn<QByteArray>("payload");
        const auto base = snapshot();
        for (const auto& name : {"pageUrl", "candidates", "truncated"}) {
            auto raw = base;
            raw.remove(QString::fromLatin1(name));
            QTest::newRow((QByteArray{"missing-"} + name).constData()) << bytes(raw);
        }
        auto raw = base;
        raw.insert(QStringLiteral("requestId"), QStringLiteral("page-cannot-supply-an-id"));
        QTest::newRow("extra-root") << bytes(raw);
        for (const auto& key : {"pageUrl", "candidates", "truncated"}) {
            raw = base;
            raw.insert(QString::fromLatin1(key), QJsonValue{});
            QTest::newRow((QByteArray{"wrong-type-"} + key).constData()) << bytes(raw);
        }
        for (const auto& key : {"url", "title", "kind"}) {
            auto entry = item();
            entry.insert(QString::fromLatin1(key), 1);
            QTest::newRow((QByteArray{"candidate-type-"} + key).constData()) << bytes(snapshot({entry}));
            entry = item();
            entry.remove(QString::fromLatin1(key));
            QTest::newRow((QByteArray{"candidate-missing-"} + key).constData()) << bytes(snapshot({entry}));
        }
        auto entry = item();
        entry.insert(QStringLiteral("sourceId"), QStringLiteral("page-owned-identity"));
        QTest::newRow("candidate-extra") << bytes(snapshot({entry}));
        QTest::newRow("candidate-nonobject") << bytes(snapshot({true}));
        QTest::newRow("unsupported-kind") << bytes(
            snapshot({item(QStringLiteral("https://cdn.example/x"), QStringLiteral("X"), QStringLiteral("command"))}));
        QTest::newRow("json-string-wrapper") << QByteArray{"\"{\\\"pageUrl\\\":\\\"https://page.example/\\\"}\""};
        QTest::newRow("array") << QByteArray{"[]"};
        QTest::newRow("null") << QByteArray{"null"};
    }
    void onlyTheClosedSnapshotDtoIsAccepted() {
        QFETCH(QByteArray, payload);
        QVERIFY(!decodeBrowserMediaSnapshot(payload));
    }
    void malformedLexemesCannotBeCollapsedByQt_data() {
        QTest::addColumn<QByteArray>("payload");
        const auto original = bytes(snapshot());
        auto duplicate = original;
        duplicate.insert(1, "\"pageUrl\":\"https://page.example/list\",");
        QTest::newRow("duplicate-key") << duplicate;
        duplicate = original;
        duplicate.insert(1, "\"page\\u0055rl\":\"https://page.example/list\",");
        QTest::newRow("escaped-duplicate-key") << duplicate;
        duplicate = original;
        duplicate.replace("\"url\":", "\"url\":\"https://cdn.example/other.mp3\",\"url\":");
        QTest::newRow("duplicate-candidate-key") << duplicate;
        auto invalid = original;
        invalid.replace("Uma", "\\uD800");
        QTest::newRow("lone-high-surrogate") << invalid;
        invalid = original;
        invalid.replace("Uma", "\\uDC00");
        QTest::newRow("lone-low-surrogate") << invalid;
        invalid = original;
        invalid.replace("Uma", "\\x41");
        QTest::newRow("unknown-escape") << invalid;
        invalid = original;
        invalid.replace("Uma", QByteArray{"\xc0\x80", 2});
        QTest::newRow("overlong-utf8") << invalid;
        invalid = original;
        invalid.replace("Uma", QByteArray{"\xef\xbb\xbf"} + "Uma");
        QTest::newRow("leading-string-bom") << invalid;
        invalid = original;
        invalid.replace("false", "+1");
        QTest::newRow("invalid-number") << invalid;
        QTest::newRow("leading-bom") << QByteArray{"\xef\xbb\xbf"} + original;
        QTest::newRow("trailing-document") << original + original;
        QTest::newRow("empty") << QByteArray{};
    }
    void malformedLexemesCannotBeCollapsedByQt() {
        QFETCH(QByteArray, payload);
        QVERIFY(!decodeBrowserMediaSnapshot(payload));
    }
};

QTEST_GUILESS_MAIN(BrowserMediaSnapshotCodecTests)
#include "BrowserMediaSnapshotCodecTests.moc"
