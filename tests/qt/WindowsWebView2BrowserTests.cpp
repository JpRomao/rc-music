#include "adapters/WindowsWebView2Browser.hpp"
#include "adapters/WindowsWebView2Options.hpp"

#include <QTest>

#include <limits>

using namespace music_player::m0;

class WindowsWebView2BrowserTests final : public QObject {
    Q_OBJECT
  private slots:
    void realOptionsKeepPrivacyBoundariesWithoutCreatingAnEngine() {
        const auto options = makeWindowsWebView2Options();
        QVERIFY(options);
        BOOL sso = TRUE;
        QCOMPARE(options->get_AllowSingleSignOnUsingOSPrimaryAccount(&sso), S_OK);
        QCOMPARE(sso, FALSE);
        Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions2> exclusive;
        QVERIFY(SUCCEEDED(options.As(&exclusive)));
        BOOL exclusive_folder = FALSE;
        QCOMPARE(exclusive->get_ExclusiveUserDataFolderAccess(&exclusive_folder), S_OK);
        QCOMPARE(exclusive_folder, TRUE);
        Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions6> extensions;
        QVERIFY(SUCCEEDED(options.As(&extensions)));
        BOOL extensions_enabled = TRUE;
        QCOMPARE(extensions->get_AreBrowserExtensionsEnabled(&extensions_enabled), S_OK);
        QCOMPARE(extensions_enabled, FALSE);
    }
    void realOptionsSelectOnlyTheStableRuntimeChannel() {
        const auto options = makeWindowsWebView2Options();
        QVERIFY(options);
        Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions7> channels;
        QVERIFY(SUCCEEDED(options.As(&channels)));
        COREWEBVIEW2_RELEASE_CHANNELS selected{};
        QCOMPARE(channels->get_ReleaseChannels(&selected), S_OK);
        QCOMPARE(selected, COREWEBVIEW2_RELEASE_CHANNELS_STABLE);
    }
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
