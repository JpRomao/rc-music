#include "adapters/WindowsPowerEvents.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QTest>
#include <QTimer>
#include <QWindow>

#include <qt_windows.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class FakeSuspendResumeNativeBoundary final : public SuspendResumeNativeBoundary {
  public:
    [[nodiscard]] bool bind(std::uintptr_t native_window_handle) noexcept override {
        ++bind_calls;
        last_handle = native_window_handle;
        return bind_succeeds;
    }

    void shutdown() noexcept override { ++shutdown_calls; }

    bool bind_succeeds{true};
    int bind_calls{0};
    int shutdown_calls{0};
    std::uintptr_t last_handle{0};
};

[[nodiscard]] MSG powerMessage(std::uintptr_t native_window_handle, WPARAM event_kind) {
    MSG message{};
    message.hwnd = reinterpret_cast<HWND>(native_window_handle);
    message.message = WM_POWERBROADCAST;
    message.wParam = event_kind;
    return message;
}

} // namespace

class WindowsPowerEventsTests final : public QObject {
    Q_OBJECT

  private slots:
    void startRegistersOneDedicatedReceiverAndIsIdempotent() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));

        QVERIFY(events.start());
        QVERIFY(events.available());
        QCOMPARE(native->bind_calls, 1);
        QVERIFY(native->last_handle != 0);
        QVERIFY(IsWindow(reinterpret_cast<HWND>(native->last_handle)));
        QVERIFY(!IsWindowVisible(reinterpret_cast<HWND>(native->last_handle)));

        QVERIFY(events.start());
        QCOMPARE(native->bind_calls, 1);
        QCOMPARE(native->shutdown_calls, 0);

        QWindow visible_window;
        static_cast<void>(visible_window.winId());
        QVERIFY(native->last_handle != static_cast<std::uintptr_t>(visible_window.winId()));
        QVERIFY(events.available());
    }

    void matchingMessagesDispatchSynchronouslyAndSuppressOnlyConsecutiveDuplicates() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        std::vector<SystemPowerEvent> observed;
        bool inside_filter_call{false};
        bool handled_synchronously{false};
        events.setEventHandler([&](SystemPowerEvent event) {
            handled_synchronously = inside_filter_call;
            observed.push_back(event);
        });
        qintptr result{4711};
        const auto send = [&](WPARAM event_kind) {
            auto message = powerMessage(native->last_handle, event_kind);
            inside_filter_call = true;
            const bool consumed = dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result);
            inside_filter_call = false;
            return consumed;
        };

        QVERIFY(send(PBT_APMSUSPEND));
        QVERIFY(handled_synchronously);
        QCOMPARE(observed, std::vector<SystemPowerEvent>{SystemPowerEvent::Suspending});
        QVERIFY(send(PBT_APMSUSPEND));
        QCOMPARE(observed.size(), std::size_t{1});

        QVERIFY(send(PBT_APMRESUMEAUTOMATIC));
        QCOMPARE(observed, (std::vector<SystemPowerEvent>{SystemPowerEvent::Suspending, SystemPowerEvent::Resumed}));
        QVERIFY(send(PBT_APMRESUMEAUTOMATIC));
        QCOMPARE(observed.size(), std::size_t{2});

        QVERIFY(send(PBT_APMSUSPEND));
        QCOMPARE(observed, (std::vector<SystemPowerEvent>{SystemPowerEvent::Suspending, SystemPowerEvent::Resumed,
                                                          SystemPowerEvent::Suspending}));
        QCOMPARE(result, qintptr{TRUE});
    }

    void automaticResumeIsDeliveredEvenWhenNoSuspendWasObserved() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        std::vector<SystemPowerEvent> observed;
        events.setEventHandler([&](SystemPowerEvent event) { observed.push_back(event); });
        auto message = powerMessage(native->last_handle, PBT_APMRESUMEAUTOMATIC);
        qintptr result{91};

        QVERIFY(dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));

        QCOMPARE(observed, std::vector<SystemPowerEvent>{SystemPowerEvent::Resumed});
        QCOMPARE(result, qintptr{TRUE});
    }

    void handlerExceptionsNeverEscapeTheNativeDispatcher() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        bool called{false};
        events.setEventHandler([&](SystemPowerEvent) {
            called = true;
            throw std::runtime_error{"handler failure"};
        });
        auto message = powerMessage(native->last_handle, PBT_APMSUSPEND);
        qintptr result{8128};

        QVERIFY(dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));

        QVERIFY(called);
        QCOMPARE(result, qintptr{TRUE});
    }

    void unrelatedMessagesWindowsAndResumeKindsAreIgnored() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QWindow other;
        QVERIFY(events.start());
        static_cast<void>(other.winId());
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        std::vector<SystemPowerEvent> observed;
        events.setEventHandler([&](SystemPowerEvent event) { observed.push_back(event); });
        qintptr result{321};

        auto suspend = powerMessage(native->last_handle, PBT_APMSUSPEND);
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_dispatcher_MSG"), &suspend, &result));
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), nullptr, &result));
        auto wrong_window = powerMessage(static_cast<std::uintptr_t>(other.winId()), PBT_APMSUSPEND);
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &wrong_window, &result));
        auto unrelated_message = powerMessage(native->last_handle, PBT_APMSUSPEND);
        unrelated_message.message = WM_SETTINGCHANGE;
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &unrelated_message, &result));
        auto user_resume = powerMessage(native->last_handle, PBT_APMRESUMESUSPEND);
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &user_resume, &result));
        auto critical_resume = powerMessage(native->last_handle, PBT_APMRESUMECRITICAL);
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &critical_resume, &result));

        QCOMPARE(observed.size(), std::size_t{0});
        QCOMPARE(result, qintptr{321});
    }

    void failedRegistrationRetriesAndRecoversAvailability() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        native->bind_succeeds = false;
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(!events.start());
        QVERIFY(!events.available());
        QCOMPARE(native->bind_calls, 1);
        QCOMPARE(native->shutdown_calls, 1);
        QTimer* const retry_timer = events.findChild<QTimer*>(QStringLiteral("systemPowerEventsRegistrationRetryTimer"),
                                                              Qt::FindDirectChildrenOnly);
        QVERIFY(retry_timer != nullptr);
        QVERIFY(retry_timer->isActive());

        QVERIFY(QMetaObject::invokeMethod(retry_timer, "timeout", Qt::DirectConnection));
        QVERIFY(!events.available());
        QCOMPARE(native->bind_calls, 2);
        QCOMPARE(native->shutdown_calls, 2);
        QVERIFY(retry_timer->isActive());

        native->bind_succeeds = true;
        QVERIFY(QMetaObject::invokeMethod(retry_timer, "timeout", Qt::DirectConnection));
        QVERIFY(events.available());
        QCOMPARE(native->bind_calls, 3);
        QVERIFY(native->last_handle != 0);
        QVERIFY(!retry_timer->isActive());
    }

    void retryBackoffStopsAfterItsBudgetAndApplicationStateCanRecover() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        native->bind_succeeds = false;
        WindowsPowerEvents events(std::move(boundary));

        QVERIFY(!events.start());
        QTimer* const retry_timer = events.findChild<QTimer*>(QStringLiteral("systemPowerEventsRegistrationRetryTimer"),
                                                              Qt::FindDirectChildrenOnly);
        QVERIFY(retry_timer != nullptr);
        QVERIFY(retry_timer->isActive());
        QCOMPARE(retry_timer->interval(), 1'000);

        QVERIFY(QMetaObject::invokeMethod(retry_timer, "timeout", Qt::DirectConnection));
        QCOMPARE(native->bind_calls, 2);
        QVERIFY(retry_timer->isActive());
        QCOMPARE(retry_timer->interval(), 5'000);

        QVERIFY(QMetaObject::invokeMethod(retry_timer, "timeout", Qt::DirectConnection));
        QCOMPARE(native->bind_calls, 3);
        QVERIFY(retry_timer->isActive());
        QCOMPARE(retry_timer->interval(), 30'000);

        QVERIFY(QMetaObject::invokeMethod(retry_timer, "timeout", Qt::DirectConnection));
        QCOMPARE(native->bind_calls, 4);
        QCOMPARE(native->shutdown_calls, 4);
        QVERIFY(!retry_timer->isActive());
        QVERIFY(!events.available());

        native->bind_succeeds = true;
        auto* const application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
        QVERIFY(application != nullptr);
        application->applicationStateChanged(application->applicationState());
        QVERIFY(events.available());
        QCOMPARE(native->bind_calls, 5);
        QVERIFY(native->last_handle != 0);
    }

    void pendingRegistrationRetryStopsAfterShutdown() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        native->bind_succeeds = false;
        WindowsPowerEvents events(std::move(boundary));

        QVERIFY(!events.start());
        QTimer* const retry_timer = events.findChild<QTimer*>(QStringLiteral("systemPowerEventsRegistrationRetryTimer"),
                                                              Qt::FindDirectChildrenOnly);
        QVERIFY(retry_timer != nullptr);
        QVERIFY(retry_timer->isActive());
        QCOMPARE(native->bind_calls, 1);

        events.shutdown();
        QVERIFY(!retry_timer->isActive());
        native->bind_succeeds = true;
        QVERIFY(QMetaObject::invokeMethod(retry_timer, "timeout", Qt::DirectConnection));
        QCOMPARE(native->bind_calls, 1);
        QVERIFY(!events.available());
    }

    void visibleWindowSurfaceLifecycleDoesNotInterruptTheDedicatedReceiver() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());
        QCOMPARE(native->bind_calls, 1);
        const std::uintptr_t receiver_handle = native->last_handle;
        QWindow visible_window;
        static_cast<void>(visible_window.winId());
        QVERIFY(receiver_handle != static_cast<std::uintptr_t>(visible_window.winId()));
        std::vector<SystemPowerEvent> observed;
        events.setEventHandler([&](const SystemPowerEvent event) { observed.push_back(event); });
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        auto suspend = powerMessage(receiver_handle, PBT_APMSUSPEND);
        qintptr result{42};
        QVERIFY(dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &suspend, &result));
        QCOMPARE(observed, std::vector<SystemPowerEvent>{SystemPowerEvent::Suspending});

        visible_window.destroy();
        QCoreApplication::processEvents();
        QVERIFY(events.available());
        QCOMPARE(native->bind_calls, 1);
        QCOMPARE(native->shutdown_calls, 0);

        visible_window.create();
        QCoreApplication::processEvents();
        QVERIFY(events.available());
        QCOMPARE(native->bind_calls, 1);
        QCOMPARE(native->last_handle, receiver_handle);

        auto resume = powerMessage(receiver_handle, PBT_APMRESUMEAUTOMATIC);
        QVERIFY(dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &resume, &result));
        QCOMPARE(observed, (std::vector<SystemPowerEvent>{SystemPowerEvent::Suspending, SystemPowerEvent::Resumed}));
    }

    void postedMessageToTheDedicatedReceiverTraversesTheQtDispatcher() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());
        std::vector<SystemPowerEvent> observed;
        events.setEventHandler([&](const SystemPowerEvent event) { observed.push_back(event); });

        QVERIFY(PostMessageW(reinterpret_cast<HWND>(native->last_handle), WM_POWERBROADCAST, PBT_APMSUSPEND, 0));

        QTRY_COMPARE(observed, std::vector<SystemPowerEvent>{SystemPowerEvent::Suspending});
    }

    void postedPowerMessageCanShutdownAndDestroyItsPrivateReceiverSafely() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());
        int observed{0};
        events.setEventHandler([&](SystemPowerEvent) {
            ++observed;
            events.shutdown();
        });

        QVERIFY(PostMessageW(reinterpret_cast<HWND>(native->last_handle), WM_POWERBROADCAST, PBT_APMSUSPEND, 0));

        QTRY_COMPARE(observed, 1);
        QVERIFY(!events.available());
        QCOMPARE(native->shutdown_calls, 1);
    }

    void postedPowerMessageCanDeleteTheAdapterDuringItsCallback() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        auto events = std::make_unique<WindowsPowerEvents>(std::move(boundary));
        QVERIFY(events->start());
        const QPointer<WindowsPowerEvents> guarded_events{events.get()};
        const auto receiver_handle = reinterpret_cast<HWND>(native->last_handle);
        events->setEventHandler([&](SystemPowerEvent) { events.reset(); });

        QVERIFY(PostMessageW(receiver_handle, WM_POWERBROADCAST, PBT_APMSUSPEND, 0));

        QTRY_VERIFY(guarded_events == nullptr);
    }

    void shutdownReleasesExactlyOnceAndMakesLateStartInert() {
        auto boundary = std::make_unique<FakeSuspendResumeNativeBoundary>();
        auto* const native = boundary.get();
        WindowsPowerEvents events(std::move(boundary));
        QVERIFY(events.start());

        events.shutdown();
        events.shutdown();
        QVERIFY(!events.available());
        QCOMPARE(native->shutdown_calls, 1);
        QVERIFY(!events.start());
        QCOMPARE(native->bind_calls, 1);
    }

    void defaultBoundaryRegistersARealWindowWithoutChangingSystemState() {
        WindowsPowerEvents events;

        QVERIFY(events.start());
        QVERIFY(events.available());

        events.shutdown();
        QVERIFY(!events.available());
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::WindowsPowerEventsTests)

#include "WindowsPowerEventsTests.moc"
