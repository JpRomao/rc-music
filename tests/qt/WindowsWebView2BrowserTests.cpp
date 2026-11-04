#include "adapters/WindowsWebView2Browser.hpp"

#include <QTest>

#include <limits>

using namespace music_player::m0;

class WindowsWebView2BrowserTests final : public QObject {
    Q_OBJECT
  private slots:
    void factoryIsInertAndShutdownIsIdempotentWithoutAGuiApplication() {
        auto browser = makeWindowsWebView2Browser();
        QVERIFY(browser);
        browser->shutdown();
        browser->shutdown();
    }
    void invalidOwnerIsRejectedBeforeCreatingAnyGuiOrEngine_data() {
        QTest::addColumn<qulonglong>("owner");
        QTest::newRow("null") << qulonglong{0};
        QTest::newRow("invalid") << static_cast<qulonglong>(std::numeric_limits<std::uintptr_t>::max());
    }
    void invalidOwnerIsRejectedBeforeCreatingAnyGuiOrEngine() {
        QFETCH(qulonglong, owner);
        auto browser = makeWindowsWebView2Browser();
        QVERIFY(!browser->show(static_cast<std::uintptr_t>(owner)));
        browser->shutdown();
    }
};

QTEST_GUILESS_MAIN(WindowsWebView2BrowserTests)
#include "WindowsWebView2BrowserTests.moc"
