#include "adapters/WindowsSourceVolumeEvents.hpp"

#include <QAbstractEventDispatcher>
#include <QPointer>
#include <QTest>
#include <QWindow>

#include <qt_windows.h>

#include <dbt.h>
#include <vector>

using namespace music_player::m0;

namespace {
constexpr std::uintptr_t receiver_handle = 0x1000;
constexpr std::uint32_t drives = (1U << 0) | (1U << 25);

WindowsSourceVolumeNativeOperations fakeReceiver() {
    return {.receiver_handle = [](QWindow&) { return receiver_handle; }};
}

DEV_BROADCAST_VOLUME volume(const WORD flags = 0) {
    return {sizeof(DEV_BROADCAST_VOLUME), DBT_DEVTYP_VOLUME, 0, drives, flags};
}

MSG removal(const DEV_BROADCAST_VOLUME* payload) {
    MSG message{};
    message.hwnd = reinterpret_cast<HWND>(receiver_handle);
    message.message = WM_DEVICECHANGE;
    message.wParam = DBT_DEVICEREMOVECOMPLETE;
    message.lParam = reinterpret_cast<LPARAM>(payload);
    return message;
}
} // namespace

class WindowsSourceVolumeEventsTests final : public QObject {
    Q_OBJECT

  private slots:
    void failedHiddenReceiverCanBeRetriedAndSuccessfulStartIsIdempotent() {
        int attempts = 0;
        QPointer<QWindow> receiver;
        const WindowsSourceVolumeNativeOperations operations{
            .receiver_handle = [&](QWindow& window) -> std::uintptr_t {
                receiver = &window;
                return ++attempts == 1 ? 0 : receiver_handle;
            },
        };
        {
            WindowsSourceVolumeEvents events(operations);
            QCOMPARE(attempts, 0);
            QVERIFY(!events.start());
            QVERIFY(receiver.isNull());
            QVERIFY(events.start());
            QVERIFY(receiver);
            QCOMPARE(receiver->objectName(), QStringLiteral("sourceVolumeEventsNotificationWindow"));
            QVERIFY(receiver->isTopLevel());
            QVERIFY(!receiver->isVisible());
            QVERIFY(events.start());
            QCOMPARE(attempts, 2);
        }
        QVERIFY(receiver.isNull());
    }

    void copiesEachLocalMaskSynchronouslyFromOnlyItsPrivateReceiver() {
        auto* dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher);
        std::vector<std::uint32_t> delivered;
        auto payload = volume();
        auto message = removal(&payload);
        qintptr result = -1;
        {
            WindowsSourceVolumeEvents events(fakeReceiver());
            connect(&events, &WindowsSourceVolumeEvents::volumeRemoved, this,
                    [&](const std::uint32_t mask) { delivered.push_back(mask); });
            QVERIFY(!dispatcher->filterNativeEvent("windows_generic_MSG", &message, &result));
            QVERIFY(events.start());
            QVERIFY(dispatcher->filterNativeEvent("windows_generic_MSG", &message, &result));
            QCOMPARE(result, qintptr{TRUE});
            QCOMPARE(delivered, std::vector<std::uint32_t>{drives});

            payload = volume(DBTF_MEDIA);
            QVERIFY(dispatcher->filterNativeEvent("windows_generic_MSG", &message, nullptr));
            QCOMPARE(delivered, (std::vector<std::uint32_t>{drives, drives}));
        }
        QVERIFY(!dispatcher->filterNativeEvent("windows_generic_MSG", &message, &result));
        QCOMPARE(delivered.size(), std::size_t{2});
    }

    void ignoresMalformedAndForeignMessages_data() {
        QTest::addColumn<QByteArray>("event_type");
        QTest::addColumn<quintptr>("window");
        QTest::addColumn<quint32>("message_id");
        QTest::addColumn<quintptr>("event");
        QTest::addColumn<quint32>("size");
        QTest::addColumn<quint32>("device_type");
        QTest::addColumn<quint32>("mask");
        QTest::addColumn<quint16>("flags");

        constexpr auto own = quintptr{receiver_handle};
        constexpr auto device_change = quint32{WM_DEVICECHANGE};
        constexpr auto removed = quintptr{DBT_DEVICEREMOVECOMPLETE};
        constexpr auto size = quint32{sizeof(DEV_BROADCAST_VOLUME)};
        constexpr auto volume_type = quint32{DBT_DEVTYP_VOLUME};
        const QByteArray generic{"windows_generic_MSG"};
        QTest::newRow("dispatcher") << QByteArray{"windows_dispatcher_MSG"} << own << device_change << removed << size
                                    << volume_type << quint32{drives} << quint16{0};
        QTest::newRow("foreign-window") << generic << quintptr{receiver_handle + 1} << device_change << removed << size
                                        << volume_type << quint32{drives} << quint16{0};
        QTest::newRow("other-message") << generic << own << quint32{WM_SETTINGCHANGE} << removed << size << volume_type
                                       << quint32{drives} << quint16{0};
        QTest::newRow("pending-removal") << generic << own << device_change << quintptr{DBT_DEVICEREMOVEPENDING} << size
                                         << volume_type << quint32{drives} << quint16{0};
        QTest::newRow("truncated-payload")
            << generic << own << device_change << removed << size - 1 << volume_type << quint32{drives} << quint16{0};
        QTest::newRow("foreign-device-type") << generic << own << device_change << removed << size
                                             << quint32{DBT_DEVTYP_DEVICEINTERFACE} << quint32{drives} << quint16{0};
        QTest::newRow("empty-mask") << generic << own << device_change << removed << size << volume_type << quint32{0}
                                    << quint16{0};
        QTest::newRow("outside-A-Z") << generic << own << device_change << removed << size << volume_type
                                     << quint32{drives | (1U << 26)} << quint16{0};
        QTest::newRow("network-volume") << generic << own << device_change << removed << size << volume_type
                                        << quint32{drives} << quint16{DBTF_NET};
        QTest::newRow("mixed-network-flag") << generic << own << device_change << removed << size << volume_type
                                            << quint32{drives} << quint16{DBTF_MEDIA | DBTF_NET};
        QTest::newRow("unknown-flag") << generic << own << device_change << removed << size << volume_type
                                      << quint32{drives} << quint16{4};
    }

    void ignoresMalformedAndForeignMessages() {
        QFETCH(QByteArray, event_type);
        QFETCH(quintptr, window);
        QFETCH(quint32, message_id);
        QFETCH(quintptr, event);
        QFETCH(quint32, size);
        QFETCH(quint32, device_type);
        QFETCH(quint32, mask);
        QFETCH(quint16, flags);
        auto* dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher);
        WindowsSourceVolumeEvents events(fakeReceiver());
        int deliveries = 0;
        connect(&events, &WindowsSourceVolumeEvents::volumeRemoved, this, [&] { ++deliveries; });
        QVERIFY(events.start());
        const DEV_BROADCAST_VOLUME payload{size, device_type, 0, mask, flags};
        auto message = removal(&payload);
        message.hwnd = reinterpret_cast<HWND>(window);
        message.message = message_id;
        message.wParam = event;
        qintptr result = -1;
        QVERIFY(!dispatcher->filterNativeEvent(event_type, &message, &result));
        QCOMPARE(result, qintptr{-1});
        QCOMPARE(deliveries, 0);
    }

    void ignoresMissingOrUnreadablePayload() {
        auto* dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher);
        WindowsSourceVolumeEvents events(fakeReceiver());
        int deliveries = 0;
        connect(&events, &WindowsSourceVolumeEvents::volumeRemoved, this, [&] { ++deliveries; });
        QVERIFY(events.start());
        auto message = removal(nullptr);
        qintptr result = -1;
        QVERIFY(!dispatcher->filterNativeEvent("windows_generic_MSG", nullptr, &result));
        QVERIFY(!dispatcher->filterNativeEvent("windows_generic_MSG", &message, &result));
        message.lParam = 1;
        QVERIFY(!dispatcher->filterNativeEvent("windows_generic_MSG", &message, &result));
        QCOMPARE(result, qintptr{-1});
        QCOMPARE(deliveries, 0);
    }
};

QTEST_MAIN(WindowsSourceVolumeEventsTests)
#include "WindowsSourceVolumeEventsTests.moc"
