#include "adapters/QtSystemAppearance.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QList>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <memory>

namespace music_player::m0::test {
namespace {

class EventRecipient final : public QObject {
  public:
    int received{0};

  protected:
    bool event(QEvent*) override {
        ++received;
        return true;
    }
};

} // namespace

class QtSystemAppearanceTests final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsAreLightAndReducedMotion() {
        const SystemAppearanceSnapshot defaults;
        QVERIFY(!defaults.dark);
        QVERIFY(defaults.reduced_motion);
        QVERIFY(defaults == SystemAppearanceSnapshot{});
        QVERIFY(defaults != (SystemAppearanceSnapshot{true, true}));
        QVERIFY(defaults != (SystemAppearanceSnapshot{false, false}));
    }

    void readsInitialStateBeforeAnyWindowExists() {
        QCOMPARE(QGuiApplication::topLevelWindows().size(), 0);
        int reads{0};
        const SystemAppearanceSnapshot expected{true, false};
        QtSystemAppearance appearance([&] {
            ++reads;
            return expected;
        });

        QCOMPARE(reads, 1);
        QCOMPARE(appearance.snapshot(), expected);
        QCOMPARE(appearance.snapshot(), expected);
        QCOMPARE(reads, 1);
        QCOMPARE(QGuiApplication::topLevelWindows().size(), 0);
    }

    void refreshIsIdempotentForTheSameSnapshot() {
        int reads{0};
        const SystemAppearanceSnapshot expected{false, true};
        QtSystemAppearance appearance([&] {
            ++reads;
            return expected;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);

        appearance.refresh();
        appearance.refresh();

        QCOMPARE(reads, 3);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(appearance.snapshot(), expected);
    }

    void publishesEachChangedSnapshotBeforeNotifyingExactlyOnce() {
        SystemAppearanceSnapshot next{false, true};
        QtSystemAppearance appearance([&] { return next; });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        QList<SystemAppearanceSnapshot> observed;
        connect(&appearance, &QtSystemAppearance::appearanceChanged, this,
                [&] { observed.append(appearance.snapshot()); });

        const QList<SystemAppearanceSnapshot> updates{{true, true}, {true, false}, {false, true}};
        for (const auto update : updates) {
            next = update;
            appearance.refresh();
            QCOMPARE(appearance.snapshot(), update);
            appearance.refresh();
        }

        QCOMPARE(changed.size(), updates.size());
        QCOMPARE(observed, updates);
    }

    void qtAppearanceEventsRefreshWithoutBeingConsumed_data() {
        QTest::addColumn<int>("event_type");
        QTest::newRow("application activation") << static_cast<int>(QEvent::ApplicationStateChange);
        QTest::newRow("window activation") << static_cast<int>(QEvent::WindowActivate);
        QTest::newRow("application palette") << static_cast<int>(QEvent::ApplicationPaletteChange);
        QTest::newRow("palette") << static_cast<int>(QEvent::PaletteChange);
        QTest::newRow("theme") << static_cast<int>(QEvent::ThemeChange);
    }

    void qtAppearanceEventsRefreshWithoutBeingConsumed() {
        QFETCH(int, event_type);
        int reads{0};
        SystemAppearanceSnapshot next{false, true};
        QtSystemAppearance appearance([&] {
            ++reads;
            return next;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        EventRecipient recipient;
        next = {true, false};
        QEvent event(static_cast<QEvent::Type>(event_type));

        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QCOMPARE(recipient.received, 1);
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);

        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QCOMPARE(recipient.received, 2);
        QCOMPARE(reads, 3);
        QCOMPARE(changed.size(), 1);
    }

    void unrelatedQtEventsDoNotReadTheSystem() {
        int reads{0};
        QtSystemAppearance appearance([&] {
            ++reads;
            return SystemAppearanceSnapshot{};
        });
        EventRecipient recipient;
        QEvent event(QEvent::User);

        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QCOMPARE(recipient.received, 1);
        QCOMPARE(reads, 1);
    }

    void colorSchemeSignalRefreshesTheInjectedReader() {
        int reads{0};
        SystemAppearanceSnapshot next{false, true};
        QtSystemAppearance appearance([&] {
            ++reads;
            return next;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        next = {true, false};

        // Emit the notification only: neither Qt's setting nor the OS is changed.
        QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Unknown);

        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);
    }

    void destructionDisconnectsQtEventsAndColorSchemeSignals() {
        int reads{0};
        auto appearance = std::make_unique<QtSystemAppearance>([&] {
            ++reads;
            return SystemAppearanceSnapshot{};
        });
        EventRecipient recipient;
        QEvent event(QEvent::ApplicationPaletteChange);
        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QCOMPARE(reads, 2);

        appearance.reset();
        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Unknown);

        QCOMPARE(recipient.received, 2);
        QCOMPARE(reads, 2);
    }

#ifdef Q_OS_WIN
    void windowsAppearanceMessagesRefreshWithoutBeingConsumed_data() {
        QTest::addColumn<QByteArray>("event_type");
        QTest::addColumn<unsigned int>("message_id");
        QTest::newRow("window setting") << QByteArray("windows_generic_MSG") << UINT(WM_SETTINGCHANGE);
        QTest::newRow("dispatcher setting") << QByteArray("windows_dispatcher_MSG") << UINT(WM_SETTINGCHANGE);
        QTest::newRow("window theme") << QByteArray("windows_generic_MSG") << UINT(WM_THEMECHANGED);
        QTest::newRow("dispatcher theme") << QByteArray("windows_dispatcher_MSG") << UINT(WM_THEMECHANGED);
    }

    void windowsAppearanceMessagesRefreshWithoutBeingConsumed() {
        QFETCH(QByteArray, event_type);
        QFETCH(unsigned int, message_id);
        int reads{0};
        SystemAppearanceSnapshot next{false, true};
        QtSystemAppearance appearance([&] {
            ++reads;
            return next;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        next = {true, false};
        MSG message{};
        message.message = message_id;
        qintptr result{123};

        // Inject into Qt's filter chain, never SendMessage/PostMessage to Windows.
        QVERIFY(!dispatcher->filterNativeEvent(event_type, &message, &result));
        QCOMPARE(result, 123);
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);

        QVERIFY(!dispatcher->filterNativeEvent(event_type, &message, &result));
        QCOMPARE(result, 123);
        QCOMPARE(reads, 3);
        QCOMPARE(changed.size(), 1);
    }

    void unrelatedOrNullNativeMessagesDoNotReadTheSystem() {
        int reads{0};
        QtSystemAppearance appearance([&] {
            ++reads;
            return SystemAppearanceSnapshot{};
        });
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        MSG message{};
        message.message = WM_SETTINGCHANGE;
        qintptr result{456};

        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("xcb_generic_event_t"), &message, &result));
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), nullptr, &result));
        message.message = WM_MOUSEMOVE;
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_dispatcher_MSG"), &message, &result));

        QCOMPARE(result, 456);
        QCOMPARE(reads, 1);
    }

    void destructionRemovesTheNativeEventFilter() {
        int reads{0};
        auto appearance = std::make_unique<QtSystemAppearance>([&] {
            ++reads;
            return SystemAppearanceSnapshot{};
        });
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        MSG message{};
        message.message = WM_SETTINGCHANGE;
        qintptr result{789};
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));
        QCOMPARE(reads, 2);

        appearance.reset();
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));

        QCOMPARE(result, 789);
        QCOMPARE(reads, 2);
    }
#endif
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::QtSystemAppearanceTests)

#include "QtSystemAppearanceTests.moc"
