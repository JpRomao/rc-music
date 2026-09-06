#include "adapters/QtSystemAppearance.hpp"

#include <QAbstractEventDispatcher>
#include <QAccessibilityHints>
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

#include <array>
#include <memory>

namespace music_player::m0::test {
namespace {

[[nodiscard]] HighContrastPalette darkHighContrastPalette() {
    return {QColor(24, 31, 38), QColor(248, 248, 248), QColor(235, 217, 74), QColor(18, 22, 28), QColor(165, 176, 188)};
}

[[nodiscard]] HighContrastPalette lightHighContrastPalette() {
    return {QColor(249, 247, 241), QColor(17, 23, 31), QColor(38, 51, 137), QColor(253, 252, 250), QColor(81, 89, 102)};
}

bool emitContrastPreferenceChanged(Qt::ContrastPreference preference) {
    const auto* const accessibility = QGuiApplication::styleHints()->accessibility();
    if (accessibility == nullptr)
        return false;
    // Emit only the public notification, never change Qt's read-only preference or the OS.
    return QMetaObject::invokeMethod(const_cast<QAccessibilityHints*>(accessibility), "contrastPreferenceChanged",
                                     Qt::DirectConnection, Q_ARG(Qt::ContrastPreference, preference));
}

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
        QVERIFY(!defaults.high_contrast.has_value());
        QVERIFY(defaults == SystemAppearanceSnapshot{});
        QVERIFY(defaults != (SystemAppearanceSnapshot{true, true}));
        QVERIFY(defaults != (SystemAppearanceSnapshot{false, false}));
        QVERIFY(!(SystemAppearanceSnapshot{true, false}).high_contrast.has_value());
        QVERIFY(defaults != (SystemAppearanceSnapshot{false, true, darkHighContrastPalette()}));
    }

    void readsInitialStateBeforeAnyWindowExists() {
        QCOMPARE(QGuiApplication::topLevelWindows().size(), 0);
        int reads{0};
        const SystemAppearanceSnapshot expected{true, false, darkHighContrastPalette()};
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

    void highContrastAppearsChangesPaletteAndDisappearsWithoutChangingPreferences() {
        SystemAppearanceSnapshot next{true, false};
        QtSystemAppearance appearance([&] { return next; });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        QList<SystemAppearanceSnapshot> observed;
        connect(&appearance, &QtSystemAppearance::appearanceChanged, this,
                [&] { observed.append(appearance.snapshot()); });
        const QList<SystemAppearanceSnapshot> updates{
            {true, false, darkHighContrastPalette()}, {true, false, lightHighContrastPalette()}, {true, false}};

        for (const auto& update : updates) {
            next = update;
            appearance.refresh();
            QCOMPARE(appearance.snapshot(), update);
            appearance.refresh();
        }

        QCOMPARE(changed.size(), updates.size());
        QCOMPARE(observed, updates);
    }

    void changingOnlyOneHighContrastColorNotifies_data() {
        QTest::addColumn<int>("color_index");
        QTest::newRow("window") << 0;
        QTest::newRow("window text") << 1;
        QTest::newRow("highlight") << 2;
        QTest::newRow("highlighted text") << 3;
        QTest::newRow("disabled text") << 4;
    }

    void changingOnlyOneHighContrastColorNotifies() {
        QFETCH(int, color_index);
        const SystemAppearanceSnapshot initial{false, true, lightHighContrastPalette()};
        SystemAppearanceSnapshot next{initial};
        QtSystemAppearance appearance([&] { return next; });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        auto& palette = next.high_contrast.value();
        const std::array components{&palette.window, &palette.window_text, &palette.highlight,
                                    &palette.highlighted_text, &palette.disabled_text};
        *components.at(static_cast<std::size_t>(color_index)) = QColor(23, 67, 119);

        QCOMPARE(appearance.snapshot(), initial);
        QVERIFY(next != initial);
        appearance.refresh();

        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);
        QCOMPARE(appearance.snapshot().dark, initial.dark);
        QCOMPARE(appearance.snapshot().reduced_motion, initial.reduced_motion);
        appearance.refresh();
        QCOMPARE(changed.size(), 1);
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
        QCOMPARE(reads, 1);
        QCOMPARE(changed.size(), 0);
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);

        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QCOMPARE(recipient.received, 2);
        QCOMPARE(reads, 2);
        QCoreApplication::sendPostedEvents(&appearance);
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
        QCoreApplication::sendPostedEvents(&appearance);
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

        QCOMPARE(reads, 1);
        QCOMPARE(changed.size(), 0);
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);
    }

    void contrastPreferenceSignalRefreshesTheInjectedReader() {
        int reads{0};
        SystemAppearanceSnapshot next{false, true};
        QtSystemAppearance appearance([&] {
            ++reads;
            return next;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        next = {false, true, darkHighContrastPalette()};
        QVERIFY(emitContrastPreferenceChanged(Qt::ContrastPreference::HighContrast));

        QCOMPARE(reads, 1);
        QCOMPARE(changed.size(), 0);
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);

        next = {false, true};
        QVERIFY(emitContrastPreferenceChanged(Qt::ContrastPreference::NoPreference));
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(reads, 3);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(appearance.snapshot(), next);
    }

    void relatedNotificationsCoalesceUntilQtFinishesUpdatingAppearance() {
        int reads{0};
        const SystemAppearanceSnapshot initial{false, true, darkHighContrastPalette()};
        SystemAppearanceSnapshot next{initial};
        QtSystemAppearance appearance([&] {
            ++reads;
            return next;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        QList<SystemAppearanceSnapshot> observed;
        connect(&appearance, &QtSystemAppearance::appearanceChanged, this,
                [&] { observed.append(appearance.snapshot()); });
        EventRecipient recipient;

        // Qt signals can precede the corresponding palette update in the same event turn.
        next = {true, true, darkHighContrastPalette()};
        QVERIFY(emitContrastPreferenceChanged(Qt::ContrastPreference::HighContrast));
        QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Dark);
        QEvent theme_event(QEvent::ThemeChange);
        QVERIFY(QCoreApplication::sendEvent(&recipient, &theme_event));
#ifdef Q_OS_WIN
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        MSG message{};
        message.message = WM_SYSCOLORCHANGE;
        qintptr result{321};
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));
        QCOMPARE(result, 321);
#endif
        const SystemAppearanceSnapshot final{true, true, lightHighContrastPalette()};
        next = final;
        QEvent palette_event(QEvent::ApplicationPaletteChange);
        QVERIFY(QCoreApplication::sendEvent(&recipient, &palette_event));
        QVERIFY(QCoreApplication::sendEvent(&recipient, &palette_event));

        QCOMPARE(recipient.received, 3);
        QCOMPARE(reads, 1);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(appearance.snapshot(), initial);
        QCoreApplication::sendPostedEvents(&appearance);

        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), final);
        QCOMPARE(observed, QList<SystemAppearanceSnapshot>{final});
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(reads, 2);
    }

    void manualRefreshIsSynchronousEvenWhenAnEventRefreshIsPending() {
        int reads{0};
        SystemAppearanceSnapshot next{false, true};
        QtSystemAppearance appearance([&] {
            ++reads;
            return next;
        });
        QSignalSpy changed(&appearance, &QtSystemAppearance::appearanceChanged);
        EventRecipient recipient;
        QEvent event(QEvent::PaletteChange);
        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QCOMPARE(reads, 1);
        next = {false, true, lightHighContrastPalette()};

        appearance.refresh();

        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);
    }

    void destructionCancelsAPendingRefreshAndDisconnectsNotifications() {
        int reads{0};
        int notifications{0};
        auto appearance = std::make_unique<QtSystemAppearance>([&] {
            ++reads;
            return SystemAppearanceSnapshot{};
        });
        connect(appearance.get(), &QtSystemAppearance::appearanceChanged, this, [&] { ++notifications; });
        EventRecipient recipient;
        QEvent event(QEvent::ApplicationPaletteChange);
        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Unknown);
        QVERIFY(emitContrastPreferenceChanged(Qt::ContrastPreference::HighContrast));
        QCOMPARE(reads, 1);

        appearance.reset();
        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QVERIFY(emitContrastPreferenceChanged(Qt::ContrastPreference::HighContrast));
        QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Unknown);
        QCoreApplication::sendPostedEvents();

        QCOMPARE(recipient.received, 2);
        QCOMPARE(reads, 1);
        QCOMPARE(notifications, 0);
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
        QCoreApplication::sendPostedEvents(appearance.get());
        QCOMPARE(reads, 2);

        appearance.reset();
        QVERIFY(QCoreApplication::sendEvent(&recipient, &event));
        QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Unknown);
        QCoreApplication::sendPostedEvents();

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
        QTest::newRow("window colors") << QByteArray("windows_generic_MSG") << UINT(WM_SYSCOLORCHANGE);
        QTest::newRow("dispatcher colors") << QByteArray("windows_dispatcher_MSG") << UINT(WM_SYSCOLORCHANGE);
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
        next = {false, true, lightHighContrastPalette()};
        MSG message{};
        message.message = message_id;
        qintptr result{123};

        // Inject into Qt's filter chain, never SendMessage/PostMessage to Windows.
        QVERIFY(!dispatcher->filterNativeEvent(event_type, &message, &result));
        QCOMPARE(result, 123);
        QCOMPARE(reads, 1);
        QCOMPARE(changed.size(), 0);
        QCoreApplication::sendPostedEvents(&appearance);
        QCOMPARE(reads, 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(appearance.snapshot(), next);

        QVERIFY(!dispatcher->filterNativeEvent(event_type, &message, &result));
        QCOMPARE(result, 123);
        QCOMPARE(reads, 2);
        QCoreApplication::sendPostedEvents(&appearance);
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
        QCoreApplication::sendPostedEvents(&appearance);

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
        QCoreApplication::sendPostedEvents(appearance.get());
        QCOMPARE(reads, 2);

        appearance.reset();
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_generic_MSG"), &message, &result));
        QCoreApplication::sendPostedEvents();

        QCOMPARE(result, 789);
        QCOMPARE(reads, 2);
    }

    void destructionCancelsAPendingNativeRefresh() {
        int reads{0};
        auto appearance = std::make_unique<QtSystemAppearance>([&] {
            ++reads;
            return SystemAppearanceSnapshot{false, true, darkHighContrastPalette()};
        });
        auto* const dispatcher = QAbstractEventDispatcher::instance();
        QVERIFY(dispatcher != nullptr);
        MSG message{};
        message.message = WM_SYSCOLORCHANGE;
        qintptr result{987};
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_dispatcher_MSG"), &message, &result));
        QCOMPARE(reads, 1);

        appearance.reset();
        QCoreApplication::sendPostedEvents();
        QVERIFY(!dispatcher->filterNativeEvent(QByteArray("windows_dispatcher_MSG"), &message, &result));
        QCoreApplication::sendPostedEvents();

        QCOMPARE(result, 987);
        QCOMPARE(reads, 1);
    }
#endif
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::QtSystemAppearanceTests)

#include "QtSystemAppearanceTests.moc"
