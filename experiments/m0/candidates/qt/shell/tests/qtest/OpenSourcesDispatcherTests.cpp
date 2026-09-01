#include "OpenSourcesDispatcher.hpp"

#include <QTest>

#include <utility>

using music_player::m0::OpenSourcesCommand;
using music_player::m0::OpenSourcesDispatcher;

class OpenSourcesDispatcherTests final : public QObject {
    Q_OBJECT

  private slots:
    void activatesWithoutStartingAnImportForAnEmptyCommand() {
        int activations = 0;
        int imports = 0;
        OpenSourcesDispatcher dispatcher{[&] {
                                             ++activations;
                                             return true;
                                         },
                                         [] { return false; },
                                         [&](const QStringList&) {
                                             ++imports;
                                             return true;
                                         }};

        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{}));
        QCOMPARE(activations, 1);
        QCOMPARE(imports, 0);
        QCOMPARE(dispatcher.pendingCommandCount(), 0);
    }

    void serializesConcurrentImportsWithoutLosingOrderOrDuplicates() {
        bool busy = false;
        int activations = 0;
        QList<QStringList> started;
        OpenSourcesDispatcher dispatcher{[&] {
                                             ++activations;
                                             return true;
                                         },
                                         [&] { return busy; },
                                         [&](const QStringList& paths) {
                                             started.push_back(paths);
                                             busy = true;
                                             return true;
                                         }};
        const QString duplicate = QStringLiteral("C:/Música/faixa & 01.mp3");

        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{{duplicate, duplicate}}));
        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/next.wav")}}));
        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/third.flac")}}));
        QCOMPARE(activations, 3);
        QCOMPARE(started, QList<QStringList>({{duplicate, duplicate}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 2);

        busy = false;
        dispatcher.importerStateChanged();
        QCOMPARE(started, QList<QStringList>({{duplicate, duplicate}, {QStringLiteral("C:/next.wav")}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 1);

        busy = false;
        dispatcher.importerStateChanged();
        QCOMPARE(started,
                 QList<QStringList>(
                     {{duplicate, duplicate}, {QStringLiteral("C:/next.wav")}, {QStringLiteral("C:/third.flac")}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 0);
    }

    void retainsAnAcceptedCommandWhenTheImporterTemporarilyRejectsIt() {
        bool start_allowed = false;
        QList<QStringList> started;
        OpenSourcesDispatcher dispatcher{[] { return true; }, [] { return false; },
                                         [&](const QStringList& paths) {
                                             if (!start_allowed)
                                                 return false;
                                             started.push_back(paths);
                                             return true;
                                         }};

        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/later.mp3")}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 1);
        QVERIFY(started.isEmpty());

        start_allowed = true;
        dispatcher.importerStateChanged();
        QCOMPARE(started, QList<QStringList>({{QStringLiteral("C:/later.mp3")}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 0);
    }

    void rejectsNewCommandsWhenTheBoundedPendingQueueIsFull() {
        OpenSourcesDispatcher::Limits limits;
        limits.max_pending_commands = 1;
        limits.max_pending_paths = 1;
        limits.max_pending_payload_bytes = 1'024;
        int activations = 0;
        OpenSourcesDispatcher dispatcher{[&] {
                                             ++activations;
                                             return true;
                                         },
                                         [] { return true; }, [](const QStringList&) { return true; }, limits};

        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/first.mp3")}}));
        QVERIFY(!dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/second.mp3")}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 1);
        QCOMPARE(activations, 1);

        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{}));
        QCOMPARE(activations, 2);
    }

    void rejectsNewCommandsAfterShutdownWithoutDroppingAcceptedWork() {
        OpenSourcesDispatcher dispatcher{[] { return true; }, [] { return true; },
                                         [](const QStringList&) { return true; }};
        QVERIFY(dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/accepted.mp3")}}));
        dispatcher.stopAccepting();

        QVERIFY(!dispatcher.enqueue(OpenSourcesCommand{}));
        QVERIFY(!dispatcher.enqueue(OpenSourcesCommand{{QStringLiteral("C:/rejected.mp3")}}));
        QCOMPARE(dispatcher.pendingCommandCount(), 1);
    }
};

QTEST_GUILESS_MAIN(OpenSourcesDispatcherTests)

#include "OpenSourcesDispatcherTests.moc"
