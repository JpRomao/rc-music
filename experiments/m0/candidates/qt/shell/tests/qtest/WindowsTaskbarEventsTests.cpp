#include "adapters/WindowsTaskbarEvents.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>

#include <qt_windows.h>

#include <memory>
#include <new>

namespace music_player::m0::test {
namespace {

[[nodiscard]] QWindow* notificationWindow() {
    for (QWindow* const window : QGuiApplication::topLevelWindows()) {
        if (window->objectName() == QStringLiteral("taskbarEventsNotificationWindow"))
            return window;
    }
    return nullptr;
}

[[nodiscard]] UINT taskbarCreatedMessage() { return RegisterWindowMessageW(L"TaskbarCreated"); }

[[nodiscard]] MSG taskbarMessage(QWindow& window) {
    MSG message{};
    message.hwnd = reinterpret_cast<HWND>(window.winId());
    message.message = taskbarCreatedMessage();
    return message;
}

} // namespace

class WindowsTaskbarEventsTests final : public QObject {
    Q_OBJECT

  private slots:
    void startCreatesOneInvisibleTopLevelReceiverAndIsIdempotent() {
        WindowsTaskbarEvents events;

        QVERIFY(events.start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        const HWND handle = reinterpret_cast<HWND>(receiver->winId());
        QVERIFY(IsWindow(handle));
        QVERIFY(!IsWindowVisible(handle));
        QCOMPARE(GetParent(handle), HWND{nullptr});
        QCOMPARE(GetAncestor(handle, GA_ROOT), handle);
        QVERIFY(!receiver->isVisible());

        QVERIFY(events.start());
        QCOMPARE(notificationWindow(), receiver);
        QCOMPARE(reinterpret_cast<HWND>(receiver->winId()), handle);
    }

    void sentMessageToItsOwnReceiverIsDeliveredOnlyAfterNativeDispatchReturns() {
        WindowsTaskbarEvents events;
        QVERIFY(events.start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        QSignalSpy observed(&events, &WindowsTaskbarEvents::taskbarCreated);
        const UINT message_id = taskbarCreatedMessage();
        QVERIFY(message_id >= 0xC000U);
        const HWND handle = reinterpret_cast<HWND>(receiver->winId());

        SendMessageW(handle, message_id, 0, 0);

        QCOMPARE(observed.count(), 0);
        QTRY_COMPARE(observed.count(), 1);
        SendMessageW(handle, message_id, 0, 0);
        QCOMPARE(observed.count(), 1);
        QTRY_COMPARE(observed.count(), 2);
    }

    void unrelatedMessagesAndWindowsNeverScheduleDelivery() {
        WindowsTaskbarEvents events;
        QVERIFY(events.start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        QSignalSpy observed(&events, &WindowsTaskbarEvents::taskbarCreated);
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        qintptr result{71};
        auto message = taskbarMessage(*receiver);

        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_dispatcher_MSG"), &message, &result));
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), nullptr, &result));
        QWindow other;
        auto other_message = taskbarMessage(other);
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &other_message, &result));
        message.message = WM_SETTINGCHANGE;
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));
        QCoreApplication::processEvents();
        QCOMPARE(observed.count(), 0);
        QCOMPARE(result, qintptr{71});
    }

    void recognizedMessageIsConsumedWithZeroResultBeforeQueuedDelivery() {
        WindowsTaskbarEvents events;
        QVERIFY(events.start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        QSignalSpy observed(&events, &WindowsTaskbarEvents::taskbarCreated);
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        qintptr result{71};
        auto message = taskbarMessage(*receiver);

        QVERIFY(dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));
        QCOMPARE(result, qintptr{0});
        QCOMPARE(observed.count(), 0);
        QTRY_COMPARE(observed.count(), 1);
    }

    void shutdownCancelsQueuedDeliveryReleasesTheReceiverAndDisarmsLateCalls() {
        WindowsTaskbarEvents events;
        QVERIFY(events.start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        const HWND handle = reinterpret_cast<HWND>(receiver->winId());
        auto message = taskbarMessage(*receiver);
        QSignalSpy observed(&events, &WindowsTaskbarEvents::taskbarCreated);
        SendMessageW(handle, taskbarCreatedMessage(), 0, 0);
        events.shutdown();
        events.shutdown();

        QVERIFY(!IsWindow(handle));
        QVERIFY(notificationWindow() == nullptr);
        QVERIFY(!events.start());
        qintptr result{41};
        QVERIFY(!QAbstractEventDispatcher::instance()->filterNativeEvent(QByteArray("windows_generic_MSG"), &message,
                                                                         &result));
        QCOMPARE(result, qintptr{41});
        QCoreApplication::processEvents();
        QCOMPARE(observed.count(), 0);
    }

    void destructionCancelsQueuedDelivery() {
        int observed{0};
        auto events = std::make_unique<WindowsTaskbarEvents>();
        QVERIFY(events->start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        connect(events.get(), &WindowsTaskbarEvents::taskbarCreated, this, [&] { ++observed; });
        SendMessageW(reinterpret_cast<HWND>(receiver->winId()), taskbarCreatedMessage(), 0, 0);

        events.reset();

        QCoreApplication::processEvents();
        QCOMPARE(observed, 0);
        QVERIFY(notificationWindow() == nullptr);
    }

    void queuedReceiverCanDeleteTheAdapterSafely() {
        auto events = std::make_unique<WindowsTaskbarEvents>();
        QVERIFY(events->start());
        QWindow* const receiver = notificationWindow();
        QVERIFY(receiver != nullptr);
        const QPointer<WindowsTaskbarEvents> guarded{events.get()};
        connect(events.get(), &WindowsTaskbarEvents::taskbarCreated, this, [&] { events.reset(); });
        SendMessageW(reinterpret_cast<HWND>(receiver->winId()), taskbarCreatedMessage(), 0, 0);

        QVERIFY(guarded != nullptr);
        QTRY_VERIFY(guarded == nullptr);
        QVERIFY(notificationWindow() == nullptr);
    }

    void invalidMessageRegistrationFailsClosedAndExplicitStartCanRetry() {
        std::uint32_t registered_message{0};
        int window_creations{0};
        WindowsTaskbarNativeOperations operations;
        operations.register_message = [&] { return registered_message; };
        operations.create_window = [&] {
            ++window_creations;
            return std::make_unique<QWindow>();
        };
        WindowsTaskbarEvents events(std::move(operations));

        QVERIFY(!events.start());
        QCOMPARE(window_creations, 0);
        registered_message = WM_SETTINGCHANGE;
        QVERIFY(!events.start());
        QCOMPARE(window_creations, 0);
        registered_message = taskbarCreatedMessage();
        QVERIFY(events.start());
        QCOMPARE(window_creations, 1);
        QVERIFY(notificationWindow() != nullptr);
    }

    void receiverAllocationFailureFailsClosedAndExplicitStartCanRetry() {
        int window_creations{0};
        WindowsTaskbarNativeOperations operations;
        operations.create_window = [&]() -> std::unique_ptr<QWindow> {
            ++window_creations;
            if (window_creations == 1)
                throw std::bad_alloc{};
            if (window_creations == 2)
                return {};
            return std::make_unique<QWindow>();
        };
        WindowsTaskbarEvents events(std::move(operations));

        QVERIFY(!events.start());
        QVERIFY(notificationWindow() == nullptr);
        QVERIFY(!events.start());
        QVERIFY(notificationWindow() == nullptr);
        QVERIFY(events.start());
        QCOMPARE(window_creations, 3);
        QVERIFY(notificationWindow() != nullptr);
    }

    void shellAvailabilityMatchesTheNativePresenceProbeWithoutMutatingTheShell() {
        const bool expected = GetShellWindow() != nullptr && FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;

        QCOMPARE(WindowsTaskbarEvents::shellAvailable(), expected);
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::WindowsTaskbarEventsTests)

#include "WindowsTaskbarEventsTests.moc"
