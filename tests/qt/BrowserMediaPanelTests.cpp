#include "presentation/BrowserMediaPanel.hpp"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTest>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace music_player::m0;

namespace {
const auto page = QStringLiteral("https://page.example/watch#one");

QByteArray snapshot(int count = 2, bool truncated = false, QString first_title = QStringLiteral("First song")) {
    QJsonArray candidates;
    for (int index = 0; index < count; ++index)
        candidates.append(QJsonObject{
            {QStringLiteral("url"), QStringLiteral("https://cdn.example/%1.mp3?token=private-value").arg(index)},
            {QStringLiteral("title"), index == 0 ? first_title : QStringLiteral("Song %1").arg(index)},
            {QStringLiteral("kind"), index == 1 ? QStringLiteral("video") : QStringLiteral("audio")}});
    return QJsonDocument{QJsonObject{{QStringLiteral("pageUrl"), page},
                                     {QStringLiteral("candidates"), candidates},
                                     {QStringLiteral("truncated"), truncated}}}
        .toJson(QJsonDocument::Compact);
}

struct Harness final {
    BrowserMediaPanel panel;
    std::vector<std::uint64_t> requests;

    Harness() {
        panel.setMaximumWidth(340);
        panel.resize(340, 360);
        QTest::ignoreMessage(QtWarningMsg, "This plugin does not support propagateSizeHints()");
        panel.show();
        panel.setProbeHandler([this](const std::uint64_t operation) { requests.push_back(operation); });
        panel.setPage(page);
    }
    QPushButton* detect() { return panel.findChild<QPushButton*>(QStringLiteral("browser-media-detect")); }
    QPushButton* confirm() { return panel.findChild<QPushButton*>(QStringLiteral("browser-media-confirm")); }
    QPushButton* download() { return panel.findChild<QPushButton*>(QStringLiteral("browser-media-download")); }
    QListWidget* list() { return panel.findChild<QListWidget*>(QStringLiteral("browser-media-list")); }
    QLabel* status() { return panel.findChild<QLabel*>(QStringLiteral("browser-media-status")); }
    QLabel* count() { return panel.findChild<QLabel*>(QStringLiteral("browser-media-count")); }
    bool load(const QByteArray& data = snapshot()) {
        if (!detect() || !list() || !status() || !count() || !confirm() || !download())
            return false;
        detect()->click();
        if (!panel.pendingProbe())
            return false;
        panel.completeProbe(*panel.pendingProbe(), data);
        QCoreApplication::processEvents();
        return true;
    }
    void select(int index, bool checked = true) {
        list()->item(index)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
};
} // namespace

class BrowserMediaPanelTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
    }
    void init() { QTest::failOnWarning(); }

    void constructionIsInertAndDownloadsHaveAnExplicitUnavailableReason() {
        BrowserMediaPanel panel;
        auto* detect = panel.findChild<QPushButton*>(QStringLiteral("browser-media-detect"));
        auto* confirm = panel.findChild<QPushButton*>(QStringLiteral("browser-media-confirm"));
        auto* download = panel.findChild<QPushButton*>(QStringLiteral("browser-media-download"));
        auto* scope = panel.findChild<QLabel*>(QStringLiteral("browser-media-scope"));
        auto* reason = panel.findChild<QLabel*>(QStringLiteral("browser-media-unavailable"));
        QVERIFY(detect && confirm && download && scope && reason);
        QVERIFY(!detect->isEnabled());
        QVERIFY(!confirm->isEnabled());
        QVERIFY(!download->isEnabled());
        QCOMPARE(reason->text(), QStringLiteral("Downloads indisponíveis nesta versão."));
        QCOMPARE(reason->textFormat(), Qt::PlainText);
        QCOMPARE(scope->textFormat(), Qt::PlainText);
        QVERIFY(scope->text().contains(QStringLiteral("Página principal e componentes acessíveis")));
        QVERIFY(!panel.pendingProbe());
        int calls = 0;
        panel.setProbeHandler([&](std::uint64_t) { ++calls; });
        QCOMPARE(calls, 0);
        panel.setPage(page);
        QCOMPARE(calls, 0);
        QVERIFY(detect->isEnabled());
    }
    void detectionIsExplicitAndRendersPlainTitlesKindsAndHostsWithoutQueryTokens() {
        Harness h;
        QVERIFY(h.detect() && h.list() && h.status());
        QCOMPARE(h.requests.size(), std::size_t{0});
        h.detect()->click();
        QCOMPARE(h.requests.size(), std::size_t{1});
        QVERIFY(h.panel.pendingProbe());
        QVERIFY(!h.detect()->isEnabled());
        h.detect()->click();
        QCOMPARE(h.requests.size(), std::size_t{1});
        h.panel.completeProbe(h.requests[0], snapshot(2, false, QStringLiteral("<img src=x onerror=alert(1)>")));
        QCOMPARE(h.list()->count(), 2);
        QVERIFY(h.list()->item(0)->text().contains(QStringLiteral("<img src=x onerror=alert(1)>")));
        QVERIFY(h.list()->item(0)->text().contains(QStringLiteral("Áudio")));
        QVERIFY(h.list()->item(1)->text().contains(QStringLiteral("Vídeo")));
        QVERIFY(h.list()->item(0)->text().contains(QStringLiteral("cdn.example")));
        for (int index = 0; index < h.list()->count(); ++index) {
            const auto* item = h.list()->item(index);
            QVERIFY(item->flags().testFlag(Qt::ItemIsUserCheckable));
            QVERIFY(!item->text().contains(QStringLiteral("private-value")));
            QVERIFY(!item->toolTip().contains(QStringLiteral("private-value")));
            QCOMPARE(item->checkState(), Qt::Unchecked);
        }
        QCOMPARE(h.status()->textFormat(), Qt::PlainText);
        QVERIFY(!h.download()->isEnabled());
        QVERIFY(!h.confirm()->isEnabled());
    }
    void checkboxesWorkWithMouseAndKeyboardAndKeepAVisibleSelectionCount() {
        Harness h;
        QVERIFY(h.load());
        const auto first = h.list()->visualItemRect(h.list()->item(0));
        QTest::mouseClick(h.list()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint{first.left() + 8, first.center().y()});
        QCOMPARE(h.list()->item(0)->checkState(), Qt::Checked);
        h.list()->setFocus();
        h.list()->setCurrentRow(1);
        QTest::keyClick(h.list(), Qt::Key_Space);
        QCOMPARE(h.list()->item(1)->checkState(), Qt::Checked);
        QVERIFY(h.count()->text().contains(QStringLiteral("2")));
        QVERIFY(h.confirm()->isEnabled());
        QVERIFY(!h.download()->isEnabled());
    }
    void onlyTwentyKnownItemsCanRemainChecked() {
        Harness h;
        QVERIFY(h.load(snapshot(21)));
        QCOMPARE(h.list()->count(), 21);
        for (int index = 0; index < 21; ++index)
            h.select(index);
        for (int index = 0; index < 20; ++index)
            QCOMPARE(h.list()->item(index)->checkState(), Qt::Checked);
        QCOMPARE(h.list()->item(20)->checkState(), Qt::Unchecked);
        QVERIFY(h.count()->text().contains(QStringLiteral("20")));
        QVERIFY(h.status()->text().contains(QStringLiteral("20")));
        h.select(0, false);
        h.select(20);
        QCOMPARE(h.list()->item(20)->checkState(), Qt::Checked);
        QCOMPARE(h.list()->item(0)->checkState(), Qt::Unchecked);
    }
    void confirmationRescansTheSelectedSourceAndSelectionChangesRevokeItsProof() {
        Harness h;
        QVERIFY(h.load());
        h.select(0);
        h.confirm()->click();
        QCOMPARE(h.requests.size(), std::size_t{2});
        QVERIFY(h.panel.pendingProbe());
        QVERIFY(!h.confirm()->isEnabled());
        QVERIFY(!h.download()->isEnabled());
        h.panel.completeProbe(h.requests[1], snapshot());
        QVERIFY(h.status()->text().contains(QStringLiteral("conferid")));
        h.select(1);
        QVERIFY(!h.status()->text().contains(QStringLiteral("conferid")));
        QVERIFY(h.confirm()->isEnabled());
        QVERIFY(!h.download()->isEnabled());
    }
    void aChangedSelectedSourceRevokesTheSelectionAndExplainsHowToRecover() {
        Harness h;
        QVERIFY(h.load());
        h.select(0);
        h.confirm()->click();
        h.panel.completeProbe(h.requests.back(), snapshot(2, false, QStringLiteral("Changed source")));
        QVERIFY(h.status()->text().contains(QStringLiteral("mud")));
        QVERIFY(!h.confirm()->isEnabled());
        QVERIFY(!h.download()->isEnabled());
        QVERIFY(h.detect()->isEnabled());
    }
    void samePageNavigationInvalidatesDataButKeepsThePhysicalProbeSlotUntilItsCallback() {
        Harness h;
        QVERIFY(h.detect());
        h.detect()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto first = *h.panel.pendingProbe();
        h.panel.setPage(page);
        QCOMPARE(h.list()->count(), 0);
        QVERIFY(!h.detect()->isEnabled());
        QCOMPARE(h.panel.pendingProbe(), std::optional{first});
        h.panel.completeProbe(first, snapshot());
        QVERIFY(!h.panel.pendingProbe());
        QCOMPARE(h.list()->count(), 0);
        QVERIFY(h.detect()->isEnabled());
        h.detect()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto second = *h.panel.pendingProbe();
        QVERIFY(second > first);
        h.panel.completeProbe(first, snapshot(3));
        QCOMPARE(h.panel.pendingProbe(), std::optional{second});
        h.panel.completeProbe(second, snapshot());
        QCOMPARE(h.list()->count(), 2);
    }
    void accessibleFrameScopeIsExplainedWithoutClaimingAllFrames() {
        BrowserMediaPanel panel;
        const auto* scope = panel.findChild<QLabel*>(QStringLiteral("browser-media-scope"));
        QVERIFY(scope);
        QVERIFY(scope->toolTip().contains(QStringLiteral("frames acessíveis")));
        QVERIFY(scope->toolTip().contains(QStringLiteral("inacessíveis")));
        QVERIFY(!scope->toolTip().contains(QStringLiteral("não são inspecionados")));
    }
    void frameNavigationWithTheSameTopUrlRevokesAnAlreadyConfirmedSelection() {
        Harness h;
        QVERIFY(h.load());
        h.select(0);
        h.confirm()->click();
        h.panel.completeProbe(h.requests.back(), snapshot());
        QVERIFY(h.status()->text().contains(QStringLiteral("conferid")));
        const auto count = h.requests.size();
        h.panel.setPage(page);
        QCOMPARE(h.list()->count(), 0);
        QVERIFY(!h.confirm()->isEnabled());
        QVERIFY(!h.status()->text().contains(QStringLiteral("conferid")));
        QCOMPARE(h.requests.size(), count);
        QVERIFY(h.detect()->isEnabled());
        QVERIFY(!h.download()->isEnabled());
    }
    void frameNavigationDuringConfirmationRetainsPhysicalSlotAndRejectsTheOldResult() {
        Harness h;
        QVERIFY(h.load());
        h.select(0);
        h.confirm()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto operation = *h.panel.pendingProbe();
        h.panel.setPage(page);
        QCOMPARE(h.panel.pendingProbe(), std::optional{operation});
        QVERIFY(!h.detect()->isEnabled());
        QVERIFY(!h.confirm()->isEnabled());
        h.panel.completeProbe(operation, snapshot());
        QVERIFY(!h.panel.pendingProbe());
        QCOMPARE(h.list()->count(), 0);
        QVERIFY(!h.status()->text().contains(QStringLiteral("conferid")));
        QVERIFY(h.detect()->isEnabled());
        h.detect()->click();
        const auto next = *h.panel.pendingProbe();
        QVERIFY(next > operation);
        h.panel.completeProbe(operation, snapshot());
        QCOMPARE(h.panel.pendingProbe(), std::optional{next});
        h.panel.completeProbe(next, snapshot());
        QCOMPARE(h.list()->item(0)->checkState(), Qt::Unchecked);
        QVERIFY(!h.confirm()->isEnabled());
    }
    void timeoutNeverFreesAStillOutstandingScriptCallAndItsLateResultIsInert() {
        Harness h;
        QVERIFY(h.detect());
        h.detect()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto first = *h.panel.pendingProbe();
        h.panel.expireProbe(first);
        QCOMPARE(h.panel.pendingProbe(), std::optional{first});
        QVERIFY(!h.detect()->isEnabled());
        QVERIFY(!h.confirm()->isEnabled());
        QVERIFY(h.status()->text().contains(QStringLiteral("tempo")));
        h.detect()->click();
        QCOMPARE(h.requests.size(), std::size_t{1});
        h.panel.completeProbe(first, snapshot());
        QVERIFY(!h.panel.pendingProbe());
        QCOMPARE(h.list()->count(), 0);
        QVERIFY(h.detect()->isEnabled());
    }
    void resetRetiresTheEngineWithoutReusingTokensOrAcceptingItsOldCallbacks() {
        Harness h;
        QVERIFY(h.detect());
        h.detect()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto first = *h.panel.pendingProbe();
        h.panel.resetEngine();
        QVERIFY(!h.panel.pendingProbe());
        QVERIFY(!h.detect()->isEnabled());
        h.panel.setPage(page);
        h.detect()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto second = *h.panel.pendingProbe();
        QVERIFY(second > first);
        h.panel.completeProbe(first, snapshot(3));
        h.panel.failProbe(first);
        h.panel.expireProbe(first);
        QCOMPARE(h.panel.pendingProbe(), std::optional{second});
        h.panel.completeProbe(second, snapshot());
        QCOMPARE(h.list()->count(), 2);
    }
    void navigationAfterTimeoutKeepsItsRecoveryActionUntilThePhysicalCallDrains() {
        Harness h;
        QVERIFY(h.detect() && h.status());
        h.detect()->click();
        QVERIFY(h.panel.pendingProbe());
        const auto operation = *h.panel.pendingProbe();
        h.panel.expireProbe(operation);
        QVERIFY(h.status()->text().contains(QStringLiteral("Reabra")));
        h.panel.setPage(page);
        QCOMPARE(h.panel.pendingProbe(), std::optional{operation});
        QVERIFY(!h.detect()->isEnabled());
        QVERIFY(h.status()->text().contains(QStringLiteral("Reabra")));
        h.panel.completeProbe(operation, snapshot());
        QVERIFY(!h.panel.pendingProbe());
        QVERIFY(h.detect()->isEnabled());
        QVERIFY(!h.status()->text().contains(QStringLiteral("Reabra")));
        QCOMPARE(h.list()->count(), 0);
    }
    void synchronousProbeCompletionDoesNotAllowNestedDetectionBeforeTheHandlerReturns() {
        Harness h;
        QVERIFY(h.detect());
        h.panel.setProbeHandler([&](const std::uint64_t operation) {
            h.requests.push_back(operation);
            h.panel.completeProbe(operation, snapshot());
            h.detect()->click();
        });
        h.detect()->click();
        QCOMPARE(h.requests.size(), std::size_t{1});
        QCOMPARE(h.list()->count(), 2);
        QVERIFY(h.detect()->isEnabled());
        QVERIFY(!h.panel.pendingProbe());
    }
    void aProbeHandlerMayDestroyItsPanelWithoutBeingDestroyedDuringItsOwnCall() {
        auto panel = std::make_unique<BrowserMediaPanel>();
        auto* detect = panel->findChild<QPushButton*>(QStringLiteral("browser-media-detect"));
        QVERIFY(detect);
        QPointer<BrowserMediaPanel> guard{panel.get()};
        int calls = 0;
        panel->setProbeHandler([&](std::uint64_t) {
            panel.reset();
            ++calls;
        });
        panel->setPage(page);
        detect->click();
        QVERIFY(!guard);
        QCOMPARE(calls, 1);
    }
    void aThrowingProbeNeverExposesExceptionDetailsOrStartsAnAutomaticRetry() {
        Harness h;
        QVERIFY(h.detect() && h.status());
        int calls = 0;
        h.panel.setProbeHandler([&](std::uint64_t) {
            ++calls;
            throw std::runtime_error("C:\\private\\secret https://private.example/?token=secret");
        });
        h.detect()->click();
        QCOMPARE(calls, 1);
        QVERIFY(!h.status()->text().contains(QStringLiteral("secret")));
        QVERIFY(!h.status()->text().contains(QStringLiteral("C:")));
        QVERIFY(!h.download()->isEnabled());
        h.detect()->click();
        QCOMPARE(calls, 1);
        h.panel.resetEngine();
        QVERIFY(!h.panel.pendingProbe());
    }
    void emptyAndTruncatedListsHaveActionableSeparateMessages_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<bool>("truncated");
        QTest::addColumn<QString>("message");
        QTest::newRow("empty") << 0 << false << QStringLiteral("Nenhuma");
        QTest::newRow("truncated") << 2 << true << QStringLiteral("parcial");
    }
    void emptyAndTruncatedListsHaveActionableSeparateMessages() {
        QFETCH(int, count);
        QFETCH(bool, truncated);
        QFETCH(QString, message);
        Harness h;
        QVERIFY(h.load(snapshot(count, truncated)));
        QCOMPARE(h.list()->count(), count);
        QVERIFY(h.status()->text().contains(message, Qt::CaseInsensitive));
        QVERIFY(h.detect()->isEnabled());
        QVERIFY(!h.download()->isEnabled());
    }
    void minimumPanelKeepsControlsInsideAndKeyboardFocusMovesThroughItsLiveActions() {
        Harness h;
        QVERIFY(h.load());
        QCOMPARE(h.panel.size(), QSize(340, 360));
        h.select(0);
        h.detect()->setFocus();
        QTest::keyClick(h.detect(), Qt::Key_Tab);
        QVERIFY(h.list()->hasFocus());
        QTest::keyClick(h.list(), Qt::Key_Tab);
        QVERIFY(h.confirm()->hasFocus());
        QTest::keyClick(h.confirm(), Qt::Key_Backtab);
        QVERIFY(h.list()->hasFocus());
        for (const auto* control : std::vector<QWidget*>{h.detect(), h.list(), h.confirm(), h.download()}) {
            const auto origin = control->mapTo(&h.panel, QPoint{});
            QVERIFY(origin.x() >= 0 && origin.y() >= 0);
            QVERIFY(origin.x() + control->width() <= h.panel.width());
            QVERIFY(origin.y() + control->height() <= h.panel.height());
        }
    }
};

QTEST_MAIN(BrowserMediaPanelTests)
#include "BrowserMediaPanelTests.moc"
