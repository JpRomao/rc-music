#include "../windows/sandbox/AppContainerRegistryNative.hpp"

#include <QTest>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <limits>

using Registry = music_player::sandbox_probe::AppContainerStorageRegistry;
using Inventory = Registry::Inventory;
using Security = music_player::sandbox_probe::AppContainerStorageSecurity;
using music_player::sandbox_probe::appContainerRegistryNativeCalls;

// Admission tests only: no invocation of open_software and no real owned key.
// All exercised handles must be rejected before any Registry/event/close API.
class AppContainerRegistryNativeTests final : public QObject {
    Q_OBJECT
  private slots:
    void factoryBindsEveryReadOnlyPort() {
        const auto calls = appContainerRegistryNativeCalls();
        QVERIFY(calls.open_software);
        QVERIFY(calls.open_child);
        QVERIFY(calls.query_info);
        QVERIFY(calls.enum_child);
        QVERIFY(calls.watch_once);
        QVERIFY(calls.poll_event);
        QVERIFY(calls.security.size_query);
        QVERIFY(calls.security.read);
        QVERIFY(calls.close_key);
        QVERIFY(calls.close_event);
    }
    void unownedHandlesAreRefused_data() {
        QTest::addColumn<qulonglong>("handle");
        QTest::newRow("zero") << qulonglong{0};
        QTest::newRow("invalid") << static_cast<qulonglong>(std::numeric_limits<Registry::Handle>::max());
        QTest::newRow("predefined-hkcu") << static_cast<qulonglong>(
            reinterpret_cast<Registry::Handle>(HKEY_CURRENT_USER));
        QTest::newRow("predefined-local-settings")
            << static_cast<qulonglong>(reinterpret_cast<Registry::Handle>(HKEY_CURRENT_USER_LOCAL_SETTINGS));
        QTest::newRow("unowned") << qulonglong{17};
    }
    void unownedHandlesAreRefused() {
        QFETCH(qulonglong, handle);
        const auto calls = appContainerRegistryNativeCalls();
        QVERIFY(calls.open_child && calls.query_info && calls.enum_child && calls.watch_once && calls.poll_event &&
                calls.close_key && calls.close_event && calls.security.size_query && calls.security.read);
        const auto raw = static_cast<Registry::Handle>(handle);
        const auto opened = calls.open_child(raw, QStringLiteral("Microsoft"));
        QCOMPARE(opened.state, Registry::OpenState::Unknown);
        QCOMPARE(opened.error, std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(opened.key, Registry::Handle{0});
        QCOMPARE(opened.owned_count, std::uint32_t{0});
        QCOMPARE(opened.owned_keys, (std::array<Registry::Handle, 2>{}));
        QCOMPARE(opened.ntstatus, Inventory::not_called);
        QCOMPARE(calls.query_info(raw, Registry::maximum_native_name_units).error, std::uint32_t{ERROR_INVALID_HANDLE});
        const auto child = calls.enum_child(raw, 0, Inventory::maximum_name_units);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QCOMPARE(child.error, std::uint32_t{ERROR_INVALID_HANDLE});
        QVERIFY(child.name.isEmpty());
        const auto watch = calls.watch_once(raw);
        QCOMPARE(watch.error, std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(watch.event, Registry::Handle{0});
        const auto poll = calls.poll_event(raw);
        QCOMPARE(poll.state, Registry::EventState::Unknown);
        QCOMPARE(poll.error, std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(calls.security.size_query(Security::Object::Key, raw).error, std::uint32_t{ERROR_INVALID_HANDLE});
        std::array<std::uint8_t, Security::minimum_sd_bytes> buffer{};
        QCOMPARE(calls.security.read(Security::Object::Key, raw, buffer).error, std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(calls.close_key(raw), std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(calls.close_key(raw), std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(calls.close_event(raw), std::uint32_t{ERROR_INVALID_HANDLE});
        QCOMPARE(calls.close_event(raw), std::uint32_t{ERROR_INVALID_HANDLE});
    }
    void invalidComponentsAreRefused_data() {
        QTest::addColumn<QString>("component");
        QTest::newRow("empty") << QString{};
        QTest::newRow("dot") << QStringLiteral(".");
        QTest::newRow("dot-dot") << QStringLiteral("..");
        QTest::newRow("backslash") << QStringLiteral("Microsoft\\Windows");
        QTest::newRow("slash") << QStringLiteral("Microsoft/Windows");
        QTest::newRow("nul") << (QStringLiteral("Microsoft") + QChar{0} + QStringLiteral("Windows"));
        QTest::newRow("control") << (QStringLiteral("Microsoft") + QChar{1});
        QTest::newRow("too-long") << QString(Inventory::maximum_name_units + 1, u'x');
        QTest::newRow("unpaired-surrogate") << QString(QChar{0xd800});
    }
    void invalidComponentsAreRefused() {
        QFETCH(QString, component);
        const auto calls = appContainerRegistryNativeCalls();
        QVERIFY(calls.open_child);
        const auto result = calls.open_child(17, component);
        QCOMPARE(result.state, Registry::OpenState::Unknown);
        QCOMPARE(result.error, std::uint32_t{ERROR_INVALID_NAME});
        QCOMPARE(result.key, Registry::Handle{0});
        QCOMPARE(result.owned_count, std::uint32_t{0});
        QCOMPARE(result.ntstatus, Inventory::not_called);
    }
    void nameBoundsAreCheckedBeforeAnyNativeCall() {
        const auto calls = appContainerRegistryNativeCalls();
        QVERIFY(calls.query_info && calls.enum_child);
        for (const auto units :
             {0U, Registry::maximum_native_name_units + 1, std::numeric_limits<std::uint32_t>::max()}) {
            const auto result = calls.query_info(17, units);
            QCOMPARE(result.error, std::uint32_t{ERROR_INVALID_PARAMETER});
            QVERIFY(result.native_name.isEmpty());
            QCOMPARE(result.reserved, Registry::ReservedValue::Unknown);
        }
        for (const auto units : {0U, Inventory::maximum_name_units + 1, std::numeric_limits<std::uint32_t>::max()}) {
            const auto result = calls.enum_child(17, 0, units);
            QCOMPARE(result.state, Inventory::ChildState::Unknown);
            QCOMPARE(result.error, std::uint32_t{ERROR_INVALID_PARAMETER});
            QVERIFY(result.name.isEmpty());
        }
    }
    void securityPortRefusesOtherObjectKinds() {
        const auto calls = appContainerRegistryNativeCalls();
        QVERIFY(calls.security.size_query && calls.security.read);
        for (const auto object : {Security::Object::File, static_cast<Security::Object>(99)}) {
            QCOMPARE(calls.security.size_query(object, 17).error, std::uint32_t{ERROR_INVALID_PARAMETER});
            QCOMPARE(calls.security.read(object, 17, {}).error, std::uint32_t{ERROR_INVALID_PARAMETER});
        }
    }
};
QTEST_GUILESS_MAIN(AppContainerRegistryNativeTests)
#include "AppContainerRegistryNativeTests.moc"
