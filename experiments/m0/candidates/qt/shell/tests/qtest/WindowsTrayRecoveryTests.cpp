#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPixmap>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTest>

#include <qt_windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>

namespace music_player::m0::test {
namespace {

struct TrayWindows final {
    std::array<HWND, 16> handles{};
    std::size_t count{0};
    bool succeeded{false};

    [[nodiscard]] bool contains(HWND handle) const noexcept {
        return std::find(handles.begin(), handles.begin() + static_cast<std::ptrdiff_t>(count), handle) !=
               handles.begin() + static_cast<std::ptrdiff_t>(count);
    }
};

BOOL CALLBACK collectOwnTrayWindow(HWND handle, LPARAM context) {
    auto& windows = *reinterpret_cast<TrayWindows*>(context);
    DWORD process_id{0};
    GetWindowThreadProcessId(handle, &process_id);
    if (process_id != GetCurrentProcessId())
        return TRUE;

    std::array<wchar_t, 256> title{};
    std::array<wchar_t, 256> class_name{};
    if (GetWindowTextW(handle, title.data(), static_cast<int>(title.size())) <= 0 ||
        GetClassNameW(handle, class_name.data(), static_cast<int>(class_name.size())) <= 0) {
        return TRUE;
    }

    // Test-only coupling to the authenticated Qt 6.11.2 Windows plugin. The production adapter
    // must not discover or operate Qt's private tray HWND or its private icon identifier.
    constexpr wchar_t expected_class_prefix[] = L"Qt6112TrayIconMessageWindowClass";
    if (std::wcscmp(title.data(), L"QTrayIconMessageWindow") != 0 ||
        std::wcsncmp(class_name.data(), expected_class_prefix, std::size(expected_class_prefix) - 1) != 0) {
        return TRUE;
    }
    if (windows.count == windows.handles.size())
        return FALSE;
    windows.handles[windows.count++] = handle;
    return TRUE;
}

[[nodiscard]] TrayWindows ownTrayWindows() {
    TrayWindows windows;
    windows.succeeded =
        EnumThreadWindows(GetCurrentThreadId(), collectOwnTrayWindow, reinterpret_cast<LPARAM>(&windows)) != FALSE;
    return windows;
}

struct NativeIconLocation final {
    HRESULT result{E_FAIL};
    RECT bounds{};

    [[nodiscard]] bool locationCompatibleWithRegistration() const noexcept {
        // S_FALSE can describe an overflow icon with an all-zero rectangle on Windows 11.
        // This result alone is not our registration proof: require a native NIM_MODIFY ACK too.
        return result == S_FALSE || (result == S_OK && bounds.right > bounds.left && bounds.bottom > bounds.top);
    }

    [[nodiscard]] QString diagnostic() const {
        return QStringLiteral("Shell_NotifyIconGetRect HRESULT=0x%1, rect=(%2,%3)-(%4,%5)")
            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'))
            .arg(bounds.left)
            .arg(bounds.top)
            .arg(bounds.right)
            .arg(bounds.bottom);
    }
};

[[nodiscard]] NativeIconLocation iconLocation(HWND handle) {
    NOTIFYICONIDENTIFIER identifier{};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = handle;
    identifier.uID = 0;
    NativeIconLocation location;
    location.result = Shell_NotifyIconGetRect(&identifier, &location.bounds);
    return location;
}

[[nodiscard]] bool registeredIconAcknowledgesNoOp(HWND handle) {
    NOTIFYICONDATAW existing{};
    existing.cbSize = sizeof(existing);
    existing.hWnd = handle;
    existing.uID = 0;
    // No flags means no icon content, state, tooltip or callback is modified. NIM_MODIFY cannot
    // create a missing icon; the test also verifies its FALSE result in the removed interval.
    return Shell_NotifyIconW(NIM_MODIFY, &existing) != FALSE;
}

} // namespace

class WindowsTrayRecoveryTests final : public QObject {
    Q_OBJECT

  private slots:
    void recreatesOnlyItsOwnRemovedIconAndPreservesItsActions() {
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native tray characterization requires the Windows QPA plugin.");
        if (!GetShellWindow() || !FindWindowW(L"Shell_TrayWnd", nullptr))
            QSKIP("Native tray characterization requires a running Windows shell and taskbar.");
        QCOMPARE(QString::fromLatin1(qVersion()), QStringLiteral("6.11.2"));
        QApplication::setQuitOnLastWindowClosed(false);

        const TrayWindows before = ownTrayWindows();
        QVERIFY(before.succeeded);
        QMenu menu;
        auto* const restore_action = menu.addAction(QStringLiteral("Mostrar player de teste"));
        QSignalSpy action_triggered(restore_action, &QAction::triggered);
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::blue);
        const QIcon icon(pixmap);
        const QString title = QStringLiteral("Music Player — recuperação de bandeja de teste");
        QSystemTrayIcon tray(icon);
        tray.setContextMenu(&menu);
        tray.setToolTip(title);
        QSignalSpy activated(&tray, &QSystemTrayIcon::activated);
        tray.show();

        const TrayWindows installed = ownTrayWindows();
        QVERIFY(installed.succeeded);
        QCOMPARE(installed.count, before.count + 1);
        HWND own_handle{nullptr};
        for (std::size_t index = 0; index < installed.count; ++index) {
            if (!before.contains(installed.handles[index])) {
                QVERIFY(own_handle == nullptr);
                own_handle = installed.handles[index];
            }
        }
        QVERIFY(own_handle != nullptr);
        DWORD process_id{0};
        QCOMPARE(GetWindowThreadProcessId(own_handle, &process_id), GetCurrentThreadId());
        QCOMPARE(process_id, GetCurrentProcessId());
        QVERIFY(!IsWindowVisible(own_handle));
        QTRY_VERIFY2_WITH_TIMEOUT(iconLocation(own_handle).locationCompatibleWithRegistration(),
                                  qPrintable(iconLocation(own_handle).diagnostic()), 5000);
        QVERIFY(registeredIconAcknowledgesNoOp(own_handle));

        NOTIFYICONDATAW removed{};
        removed.cbSize = sizeof(removed);
        removed.hWnd = own_handle;
        removed.uID = 0;
        // Delete exactly this test's verified HWND/icon pair, never another process's icon.
        QVERIFY(Shell_NotifyIconW(NIM_DELETE, &removed));
        QTRY_VERIFY_WITH_TIMEOUT(FAILED(iconLocation(own_handle).result), 5000);
        QVERIFY(!registeredIconAcknowledgesNoOp(own_handle));
        QVERIFY(tray.isVisible());

        const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        QVERIFY(taskbar_created != 0);
        // No Explorer restart and no broadcast: only the test-owned receiver gets this message.
        SendMessageW(own_handle, taskbar_created, 0, 0);
        QTRY_VERIFY2_WITH_TIMEOUT(iconLocation(own_handle).locationCompatibleWithRegistration(),
                                  qPrintable(iconLocation(own_handle).diagnostic()), 5000);
        QVERIFY(registeredIconAcknowledgesNoOp(own_handle));
        SendMessageW(own_handle, taskbar_created, 0, 0);
        QTRY_VERIFY2_WITH_TIMEOUT(iconLocation(own_handle).locationCompatibleWithRegistration(),
                                  qPrintable(iconLocation(own_handle).diagnostic()), 5000);
        QVERIFY(registeredIconAcknowledgesNoOp(own_handle));

        const TrayWindows recovered = ownTrayWindows();
        QVERIFY(recovered.succeeded);
        QCOMPARE(recovered.count, installed.count);
        for (std::size_t index = 0; index < installed.count; ++index)
            QVERIFY(recovered.contains(installed.handles[index]));
        QCOMPARE(tray.contextMenu(), &menu);
        QCOMPARE(menu.actions(), QList<QAction*>{restore_action});
        QCOMPARE(tray.toolTip(), title);
        QCOMPARE(tray.icon().cacheKey(), icon.cacheKey());
        QVERIFY(tray.isVisible());
        QCOMPARE(activated.count(), 0);

        // Qt 6.11.2 uses WM_APP+101 and icon ID zero for this callback. Exercise its real
        // keyboard activation path after re-registration without showing a popup or moving focus.
        SendMessageW(own_handle, WM_APP + 101, 0, MAKELPARAM(NIN_KEYSELECT, 0));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.at(0).at(0).value<QSystemTrayIcon::ActivationReason>(), QSystemTrayIcon::Trigger);
        restore_action->trigger();
        QCOMPARE(action_triggered.count(), 1);

        // QSystemTrayIcon also performs this cleanup on every early assertion return (RAII).
        tray.hide();
        QVERIFY(!IsWindow(own_handle));
        QVERIFY(FAILED(iconLocation(own_handle).result));
        QVERIFY(!registeredIconAcknowledgesNoOp(own_handle));
        const TrayWindows cleaned = ownTrayWindows();
        QVERIFY(cleaned.succeeded);
        QCOMPARE(cleaned.count, before.count);
        for (std::size_t index = 0; index < before.count; ++index)
            QVERIFY(cleaned.contains(before.handles[index]));
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::WindowsTrayRecoveryTests)
#include "WindowsTrayRecoveryTests.moc"
