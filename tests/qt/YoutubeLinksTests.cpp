#include "adapters/YoutubeLinks.hpp"

#include <QTest>

using namespace music_player::m0;
namespace {
using Kind = PastedLinkIssueKind;
const std::string first{"aB0-_Cd1EF2"}, second{"Zz9_-Yy8Xx7"};
const std::string page{"https://www.letras.mus.br/lia-campos/cancao-do-mar/"};
} // namespace

class YoutubeLinksTests final : public QObject {
    Q_OBJECT
  private slots:
    void commonYoutubeLinkFormsBecomeVideoIds_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("watch") << QStringLiteral("https://www.youtube.com/watch?v=aB0-_Cd1EF2");
        QTest::newRow("watch with list and time")
            << QStringLiteral("https://www.youtube.com/watch?v=aB0-_Cd1EF2&list=PL123&t=42s");
        QTest::newRow("no www") << QStringLiteral("https://youtube.com/watch?v=aB0-_Cd1EF2");
        QTest::newRow("mobile") << QStringLiteral("https://m.youtube.com/watch?v=aB0-_Cd1EF2");
        QTest::newRow("music") << QStringLiteral("https://music.youtube.com/watch?v=aB0-_Cd1EF2&si=x");
        QTest::newRow("short link") << QStringLiteral("https://youtu.be/aB0-_Cd1EF2?si=abc");
        QTest::newRow("shorts") << QStringLiteral("https://www.youtube.com/shorts/aB0-_Cd1EF2");
        QTest::newRow("live") << QStringLiteral("https://www.youtube.com/live/aB0-_Cd1EF2");
        QTest::newRow("embed") << QStringLiteral("https://www.youtube.com/embed/aB0-_Cd1EF2");
        QTest::newRow("http") << QStringLiteral("http://www.youtube.com/watch?v=aB0-_Cd1EF2");
        QTest::newRow("no scheme") << QStringLiteral("www.youtube.com/watch?v=aB0-_Cd1EF2");
        QTest::newRow("no scheme short") << QStringLiteral("youtu.be/aB0-_Cd1EF2");
        QTest::newRow("angle brackets and comma") << QStringLiteral("<https://youtu.be/aB0-_Cd1EF2>,");
        QTest::newRow("surrounding spaces") << QStringLiteral("   https://youtu.be/aB0-_Cd1EF2   ");
    }
    void commonYoutubeLinkFormsBecomeVideoIds() {
        QFETCH(QString, text);
        const auto parsed = parsePastedYoutubeLinks(text);
        QCOMPARE(parsed.links, (std::vector<PastedYoutubeLink>{{first, {}}}));
        QVERIFY(parsed.issues.empty());
    }
    void otherAddressesAreNotLinks_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("other site") << QStringLiteral("https://evil.example/watch?v=aB0-_Cd1EF2");
        QTest::newRow("lookalike host") << QStringLiteral("https://youtube.com.evil.example/watch?v=aB0-_Cd1EF2");
        QTest::newRow("short id") << QStringLiteral("https://youtu.be/abc");
        QTest::newRow("channel") << QStringLiteral("https://www.youtube.com/@liacampos");
        QTest::newRow("playlist only") << QStringLiteral("https://www.youtube.com/playlist?list=PL123");
        QTest::newRow("bare id") << QStringLiteral("aB0-_Cd1EF2");
        QTest::newRow("ftp") << QStringLiteral("ftp://youtu.be/aB0-_Cd1EF2");
        QTest::newRow("user info") << QStringLiteral("https://user@youtu.be/aB0-_Cd1EF2");
        QTest::newRow("words") << QStringLiteral("uma música qualquer");
    }
    void otherAddressesAreNotLinks() {
        QFETCH(QString, text);
        const auto parsed = parsePastedYoutubeLinks(text);
        QVERIFY(parsed.links.empty());
        QCOMPARE(parsed.issues, (std::vector<PastedLinkIssue>{{1, Kind::NotALink}}));
    }
    void aLetrasPageJoinsTheYoutubeLinkOnItsLineOrTheLineBefore() {
        const auto parsed = parsePastedYoutubeLinks(
            QStringLiteral("https://youtu.be/aB0-_Cd1EF2 https://www.letras.mus.br/lia-campos/cancao-do-mar/\n"
                           "\n"
                           "https://www.youtube.com/watch?v=Zz9_-Yy8Xx7\n"
                           "letras.mus.br/lia-campos/outra/?utm=x#letra\n"));
        QCOMPARE(parsed.links, (std::vector<PastedYoutubeLink>{
                                   {first, page}, {second, "https://www.letras.mus.br/lia-campos/outra/"}}));
        QVERIFY(parsed.issues.empty());
    }
    void aLetrasPageAloneOrTooFarAwayIsReported() {
        const auto parsed = parsePastedYoutubeLinks(QStringLiteral(
            "https://www.letras.mus.br/lia-campos/cancao-do-mar/\n"
            "https://youtu.be/aB0-_Cd1EF2\n"
            "nada aqui\n"
            "https://www.letras.mus.br/lia-campos/cancao-do-mar/\n"
            "https://youtu.be/Zz9_-Yy8Xx7 https://www.letras.mus.br/a/b/ https://www.letras.mus.br/c/d/\n"));
        QCOMPARE(parsed.links,
                 (std::vector<PastedYoutubeLink>{{first, {}}, {second, "https://www.letras.mus.br/a/b/"}}));
        QCOMPARE(parsed.issues, (std::vector<PastedLinkIssue>{{1, Kind::LetrasWithoutYoutube},
                                                              {3, Kind::NotALink},
                                                              {4, Kind::LetrasWithoutYoutube},
                                                              {5, Kind::LetrasWithoutYoutube}}));
    }
    void aRepeatedVideoIsReportedOnce() {
        const auto parsed = parsePastedYoutubeLinks(
            QStringLiteral("https://youtu.be/aB0-_Cd1EF2\nhttps://www.youtube.com/watch?v=aB0-_Cd1EF2 "
                           "https://www.letras.mus.br/a/b/\n"));
        QCOMPARE(parsed.links, (std::vector<PastedYoutubeLink>{{first, {}}}));
        QCOMPARE(parsed.issues, (std::vector<PastedLinkIssue>{{2, Kind::Duplicate}}));
    }
    void atMostTwentyLinksAreKept() {
        QString text;
        for (int index = 0; index < 22; ++index)
            text += QStringLiteral("https://youtu.be/%1\n").arg(index, 11, 10, QLatin1Char('0'));
        const auto parsed = parsePastedYoutubeLinks(text);
        QCOMPARE(parsed.links.size(), pasted_youtube_links_max);
        QCOMPARE(parsed.issues, (std::vector<PastedLinkIssue>{{21, Kind::TooMany}, {22, Kind::TooMany}}));
    }
    void blankTextHasNothingAndHugeTextIsRefused() {
        QVERIFY(parsePastedYoutubeLinks(QStringLiteral(" \n\t\n")).links.empty());
        QVERIFY(parsePastedYoutubeLinks(QStringLiteral(" \n\t\n")).issues.empty());
        const auto huge = parsePastedYoutubeLinks(QString(pasted_youtube_links_max_chars + 1, QLatin1Char('a')));
        QVERIFY(huge.links.empty());
        QCOMPARE(huge.issues, (std::vector<PastedLinkIssue>{{0, Kind::TextTooLong}}));
    }
};

QTEST_GUILESS_MAIN(YoutubeLinksTests)
#include "YoutubeLinksTests.moc"
