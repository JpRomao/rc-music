#include "adapters/QtLyricsService.hpp"
#include "presentation/LyricsBinding.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;
namespace {
void save(const QString& path, const QByteArray& text) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text), text.size());
}
} // namespace
class LyricsBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void confirmedPositionSelectsTheOriginalVerseAndSeeksBackwards() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:01.00]Original first\n[00:04.00]Original second\n[00:06.00]\n");
        QtLyricsService service;
        LyricsBinding binding(service);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", "Album", 8000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.title(), QStringLiteral("Song"));
        QCOMPARE(binding.artist(), QStringLiteral("Artist"));
        QVERIFY(binding.hasSyncedLyrics());
        QCOMPARE(binding.currentLine(), QString{});
        QCOMPARE(binding.nextLine(), QStringLiteral("Original first"));
        binding.setPosition(1000);
        QCOMPARE(binding.currentLine(), QStringLiteral("Original first"));
        QCOMPARE(binding.nextLine(), QStringLiteral("Original second"));
        QSignalSpy changed(&binding, &LyricsBinding::changed);
        binding.setPosition(1500);
        QCOMPARE(changed.count(), 0); // A pause/tick within the same verse is inert.
        binding.setPosition(4500);
        QCOMPARE(binding.previousLine(), QStringLiteral("Original first"));
        QCOMPARE(binding.currentLine(), QStringLiteral("Original second"));
        binding.setPosition(2000);
        QCOMPARE(binding.currentLine(), QStringLiteral("Original first"));
        binding.setPosition(6500);
        QCOMPARE(binding.currentLine(), QString{});
    }
    void changingTracksClearsTheOldLyricsAndUpdatesSearchMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("a.lrc"), "[00:00.00]A line");
        QtLyricsService service;
        LyricsBinding binding(service);
        binding.setTrack(temp.filePath("a.mp3"), "A", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.currentLine(), QStringLiteral("A line"));
        binding.setTrack(temp.filePath("b.mp3"), "B", "Other", {}, 1000);
        QCOMPARE(binding.currentLine(), QString{});
        QCOMPARE(binding.title(), QStringLiteral("B"));
        QCOMPARE(binding.artist(), QStringLiteral("Other"));
        QTRY_COMPARE(service.status(), QtLyricsService::Status::NotFound);
        QVERIFY(!binding.statusText().isEmpty());
        binding.setTrack({}, {}, {}, {}, 0);
        QVERIFY(!binding.canSearch());
    }
    void plainLyricsStayUnsynchronizedAndShutdownDrainsTheReader() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("plain.lrc"), "[Chorus]\nOriginal text & <markup>\n");
        QtLyricsService service;
        LyricsBinding binding(service);
        binding.setTrack(temp.filePath("plain.mp3"), "Plain", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QVERIFY(!binding.hasSyncedLyrics());
        QVERIFY(binding.plainText().contains("Original text & <markup>"));
        binding.beginShutdown();
        QTRY_VERIFY(binding.stopped());
        QVERIFY(!binding.search("Title", "Artist"));
    }
    void cancelledPlayerExitCanReloadLocalLyricsWithoutARequest() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:00.00]Local verse");
        QtLyricsService service;
        LyricsBinding binding(service);
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QVERIFY(!binding.prepareExit()); // First poll publishes an observable exit-preparation phase.
        QTRY_VERIFY_WITH_TIMEOUT(binding.prepareExit(), 1500);
        QVERIFY(!binding.canSearch());
        binding.setTrack(path, "Song", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.currentLine(), QStringLiteral("Local verse"));
        QVERIFY(binding.canSearch());
    }
};
QTEST_GUILESS_MAIN(LyricsBindingTests)
#include "LyricsBindingTests.moc"
