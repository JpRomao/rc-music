#include "adapters/QtLyricsRomanizer.hpp"
#include "m0/domain/Lyrics.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;

class QtLyricsRomanizerTests final : public QObject {
    Q_OBJECT

  private slots:
    void reportsUnavailableWithoutPreparedRuntime() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        QVERIFY(!makeQtLyricsRomanizer(config.path()));
        QVERIFY(!makeQtLyricsRomanizer(QStringLiteral("relative/config")));
    }

    void preparedPykakasiConvertsSyntheticLines() {
        const auto root = qEnvironmentVariable("RC_MUSIC_TEST_ROMANIZER_CONFIG_ROOT");
        if (root.isEmpty())
            QSKIP("Set RC_MUSIC_TEST_ROMANIZER_CONFIG_ROOT to the isolated prepared runtime config root");
        const auto convert = makeQtLyricsRomanizer(root);
        QVERIFY(convert);
        const auto result = convert({"日本語の歌", "", "Hello! Café", "こんばんは。"});
        QVERIFY(result);
        QCOMPARE(result->size(), std::size_t{4});
        QCOMPARE(result->at(0), std::string{"nihongo no uta"});
        QVERIFY(result->at(1).empty());
        QCOMPARE(result->at(2), std::string{"Hello! Café"});
        QVERIFY(result->at(3).starts_with("konbanha"));
        QVERIFY(!convert({"line\nbreak"}));
        QVERIFY(!convert({std::string(lyrics_max_input_bytes + 1, 'x')}));
    }
};

QTEST_GUILESS_MAIN(QtLyricsRomanizerTests)
#include "QtLyricsRomanizerTests.moc"
