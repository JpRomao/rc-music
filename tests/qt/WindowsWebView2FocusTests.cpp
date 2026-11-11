#include "adapters/WindowsWebView2Surface.hpp"
#include "presentation/BrowserMediaPanel.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QStyleHints>
#include <QTest>

#include <vector>

using namespace music_player::m0;

namespace {
struct BrowserFocusHarness final {
    std::vector<Qt::FocusReason> entries;
    QWidget window;
    QLineEdit address{&window};
    QPushButton media_button{QStringLiteral("Mídia"), &window};
    WindowsWebView2Surface surface{&window};
    QPushButton hidden{&window};
    QPushButton disabled{&window};
    BrowserMediaPanel panel{&window};

    BrowserFocusHarness() {
        surface.setFocusPolicy(Qt::StrongFocus);
        surface.focused = [this](Qt::FocusReason reason) { entries.push_back(reason); };
        hidden.hide();
        disabled.setEnabled(false);
        panel.setProbeHandler([](std::uint64_t) {});
        panel.setPage(QStringLiteral("https://media.example/"));
        window.show();
        window.activateWindow();
        QCoreApplication::processEvents();
        address.setFocus();
    }

    QPushButton* detect() { return panel.findChild<QPushButton*>(QStringLiteral("browser-media-detect")); }
};
} // namespace

class WindowsWebView2FocusTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
        QApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }

    void entersPageFromBothDirectionsAndKeepsProgrammaticEntry() {
        BrowserFocusHarness browser;
        auto* detect = browser.detect();
        QVERIFY(detect);
        browser.media_button.setFocus();
        QTest::keyClick(&browser.media_button, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), &browser.surface);
        QCOMPARE(browser.entries, std::vector<Qt::FocusReason>{Qt::TabFocusReason});

        detect->setFocus();
        QTest::keyClick(detect, Qt::Key_Tab, Qt::ShiftModifier);
        QCOMPARE(QApplication::focusWidget(), &browser.surface);
        QCOMPARE(browser.entries.back(), Qt::BacktabFocusReason);

        browser.address.setFocus();
        browser.surface.setFocus(Qt::MouseFocusReason);
        QCOMPARE(browser.entries.back(), Qt::OtherFocusReason);
    }

    void pageCanTabToMediaPanelAndBacktabToToolbar() {
        BrowserFocusHarness browser;
        auto* detect = browser.detect();
        QVERIFY(detect && detect->isEnabled());
        browser.surface.setFocus();
        QVERIFY(browser.surface.moveFocusOut(Qt::TabFocusReason));
        QCOMPARE(QApplication::focusWidget(), detect);

        // Clicking the native child need not update the Qt widget focus.
        browser.address.setFocus();
        QVERIFY(browser.surface.moveFocusOut(Qt::BacktabFocusReason));
        QCOMPARE(QApplication::focusWidget(), &browser.media_button);
    }

    void skipsHiddenPanelAndDisabledControlsAndWrapsToToolbar() {
        BrowserFocusHarness browser;
        browser.panel.hide();
        browser.surface.setFocus();
        QVERIFY(browser.surface.moveFocusOut(Qt::TabFocusReason));
        QCOMPARE(QApplication::focusWidget(), &browser.address);
    }

    void declinesUnsupportedReasonOrMissingDestination() {
        BrowserFocusHarness browser;
        browser.surface.setFocus();
        QVERIFY(!browser.surface.moveFocusOut(Qt::OtherFocusReason));
        QCOMPARE(QApplication::focusWidget(), &browser.surface);
        browser.address.setEnabled(false);
        browser.media_button.setEnabled(false);
        browser.panel.hide();
        QVERIFY(!browser.surface.moveFocusOut(Qt::TabFocusReason));
        QVERIFY(!browser.surface.moveFocusOut(Qt::BacktabFocusReason));
        QCOMPARE(QApplication::focusWidget(), &browser.surface);
    }
};

QTEST_MAIN(WindowsWebView2FocusTests)
#include "WindowsWebView2FocusTests.moc"
