#include "adapters/BrowserNavigationTracker.hpp"

#include <QTest>

using namespace music_player::m0;

class BrowserNavigationTrackerTests final : public QObject {
    Q_OBJECT
  private slots:
    void emptyTrackerRejectsCompletionAndZeroRemainsAValidIdentity() {
        BrowserNavigationTracker tracker;
        QVERIFY(!tracker.pending());
        QVERIFY(!tracker.complete(0));
        QVERIFY(!tracker.complete(1));
        tracker.begin(0);
        QVERIFY(tracker.pending());
        QVERIFY(tracker.complete(0));
        QVERIFY(!tracker.pending());
    }
    void matchingCompletionConsumesThePendingNavigationExactlyOnce() {
        BrowserNavigationTracker tracker;
        tracker.begin(17);
        QVERIFY(tracker.pending());
        QVERIFY(tracker.complete(17));
        QVERIFY(!tracker.pending());
        QVERIFY(!tracker.complete(17));
    }
    void earlierCompletionCannotFinishTheReplacementNavigation() {
        BrowserNavigationTracker tracker;
        tracker.begin(10);
        tracker.begin(20);
        QVERIFY(!tracker.complete(10));
        QVERIFY(tracker.pending());
        QVERIFY(tracker.complete(20));
        QVERIFY(!tracker.pending());
    }
    void hostCancellationMakesItsLaterCompletionInert() {
        BrowserNavigationTracker tracker;
        tracker.begin(7);
        tracker.cancel();
        tracker.cancel();
        QVERIFY(!tracker.pending());
        QVERIFY(!tracker.complete(7));
        QVERIFY(!tracker.pending());
    }
    void canceledEarlierNavigationCannotConsumeANewerNavigation() {
        BrowserNavigationTracker tracker;
        tracker.begin(7);
        tracker.cancel();
        tracker.begin(8);
        QVERIFY(!tracker.complete(7));
        QVERIFY(tracker.pending());
        QVERIFY(tracker.complete(8));
        QVERIFY(!tracker.pending());
    }
};

QTEST_GUILESS_MAIN(BrowserNavigationTrackerTests)
#include "BrowserNavigationTrackerTests.moc"
