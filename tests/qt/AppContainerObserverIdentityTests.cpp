#include "../windows/sandbox/AppContainerObserverIdentity.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <QTest>

#include <algorithm>
#include <memory>

using Identity = music_player::sandbox_probe::AppContainerObserverIdentity;
namespace {
Identity::Bytes userSid() { return {1, 2, 0, 0, 0, 0, 0, 5, 21, 0, 0, 0, 42, 0, 0, 0}; }
Identity::Snapshot normal() {
    Identity::Snapshot value;
    value.complete = true;
    value.error = 0;
    value.user_sid = userSid();
    value.session_id = 7;
    value.elevation_type = Identity::ElevationType::Limited;
    value.integrity_rid = Identity::medium_integrity_rid;
    value.administrators_group_present = true;
    value.administrators_group_attributes = 0x10; // SE_GROUP_USE_FOR_DENY_ONLY, not SE_GROUP_ENABLED.
    value.enabled_privileges.push_back({L"SeChangeNotifyPrivilege", 2});
    return value;
}
Identity::Snapshot elevated() {
    auto value = normal();
    value.user_sid.back() = 43; // Over-the-shoulder elevation may use a different administrator.
    value.elevated = true;
    value.elevation_type = Identity::ElevationType::Full;
    value.integrity_rid = Identity::high_integrity_rid;
    value.administrators_group_attributes = 4;
    return value;
}
bool validSid(const Identity::Bytes& sid) {
    return sid.size() >= 8 && sid[0] == 1 && sid[1] <= 15 && sid.size() == 8 + 4 * std::size_t{sid[1]};
}
} // namespace

class AppContainerObserverIdentityTests final : public QObject {
    Q_OBJECT
  private slots:
    void defaultSnapshotGrantsNeitherRoleNorCallerIdentity() {
        Identity::Snapshot value;
        QVERIFY(!value.complete);
        QCOMPARE(value.error, Identity::not_called);
        QVERIFY(!Identity::normalDriver(value));
        QVERIFY(!Identity::elevatedObserver(value));
        QVERIFY(!Identity::matchesCaller(value, userSid(), 7));
    }
    void normalDriverAcceptsFilteredAndStandardInteractiveTokens_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 4; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void normalDriverAcceptsFilteredAndStandardInteractiveTokens() {
        QFETCH(int, kind);
        auto value = normal();
        if (kind == 1)
            value.elevation_type = Identity::ElevationType::Default;
        if (kind == 2)
            value.administrators_group_present = false;
        if (kind == 3)
            value.administrators_group_attributes = 0;
        // Diagnostic privileges are not an invented allowlist for caller admission.
        value.enabled_privileges.push_back({L"SeIncreaseWorkingSetPrivilege", 2});
        QVERIFY(Identity::normalDriver(value));
        QVERIFY(!Identity::elevatedObserver(value));
    }
    void normalDriverRejectsIncompleteElevatedNoninteractiveOrConflictingTokens_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 16; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void normalDriverRejectsIncompleteElevatedNoninteractiveOrConflictingTokens() {
        QFETCH(int, kind);
        auto value = normal();
        switch (kind) {
        case 0:
            value.complete = false;
            break;
        case 1:
            value.error = 5;
            break;
        case 2:
            value.user_sid.clear();
            break;
        case 3:
            value.user_sid[0] = 2;
            break;
        case 4:
            value.user_sid[1] = 15;
            break;
        case 5:
            value.session_id = 0;
            break;
        case 6:
            value.elevated = true;
            break;
        case 7:
            value.elevation_type = Identity::ElevationType::Unknown;
            break;
        case 8:
            value.elevation_type = Identity::ElevationType::Full;
            break;
        case 9:
            value.integrity_rid = 0x1000;
            break;
        case 10:
            value.integrity_rid = 0x2100;
            break;
        case 11:
            value.integrity_rid = Identity::high_integrity_rid;
            break;
        case 12:
            value.app_container = true;
            break;
        case 13:
            value.ui_access = true;
            break;
        case 14:
            value.administrators_group_attributes = 4;
            break;
        case 15:
            value.administrators_group_attributes = 0x14;
            break;
        }
        QVERIFY(!Identity::normalDriver(value));
    }
    void elevatedObserverAcceptsHighOrHigherWithoutRequiringTheDriverUser_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 4; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void elevatedObserverAcceptsHighOrHigherWithoutRequiringTheDriverUser() {
        QFETCH(int, kind);
        auto value = elevated();
        if (kind == 1)
            value.elevation_type = Identity::ElevationType::Default;
        if (kind == 2)
            value.integrity_rid = 0x4000;
        if (kind == 3)
            value.integrity_rid = 0x6000;
        QVERIFY(value.user_sid != normal().user_sid);
        QVERIFY(Identity::elevatedObserver(value));
        QVERIFY(!Identity::normalDriver(value));
    }
    void elevatedObserverRejectsIncompleteNonElevatedOrSandboxedTokens_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 12; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void elevatedObserverRejectsIncompleteNonElevatedOrSandboxedTokens() {
        QFETCH(int, kind);
        auto value = elevated();
        switch (kind) {
        case 0:
            value.complete = false;
            break;
        case 1:
            value.error = 5;
            break;
        case 2:
            value.user_sid.clear();
            break;
        case 3:
            value.user_sid[0] = 2;
            break;
        case 4:
            value.session_id = 0;
            break;
        case 5:
            value.elevated = false;
            break;
        case 6:
            value.elevation_type = Identity::ElevationType::Unknown;
            break;
        case 7:
            value.elevation_type = Identity::ElevationType::Limited;
            break;
        case 8:
            value.integrity_rid = Identity::medium_integrity_rid;
            break;
        case 9:
            value.integrity_rid = Identity::high_integrity_rid - 1;
            break;
        case 10:
            value.app_container = true;
            break;
        case 11:
            value.ui_access = true;
            break;
        }
        QVERIFY(!Identity::elevatedObserver(value));
    }
    void matchingCallerBindsExactDriverSidAndSessionWithoutImposingAnotherRole() {
        auto value = normal();
        QVERIFY(Identity::matchesCaller(value, userSid(), 7));
        auto different = userSid();
        different.back() ^= 1;
        QVERIFY(!Identity::matchesCaller(value, different, 7));
        QVERIFY(!Identity::matchesCaller(value, userSid(), 8));
        value.elevated = true;
        value.elevation_type = Identity::ElevationType::Full;
        value.integrity_rid = Identity::high_integrity_rid;
        QVERIFY(Identity::matchesCaller(value, userSid(), 7));
        QVERIFY(!Identity::normalDriver(value));
    }
    void matchingCallerRejectsIncompleteOrInvalidSidSnapshots_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 6; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void matchingCallerRejectsIncompleteOrInvalidSidSnapshots() {
        QFETCH(int, kind);
        auto value = normal();
        auto claimed = userSid();
        if (kind == 0)
            value.complete = false;
        if (kind == 1)
            value.error = 5;
        if (kind == 2)
            value.user_sid.clear();
        if (kind == 3)
            claimed.clear();
        if (kind == 4) {
            claimed[0] = 2;
            value.user_sid = claimed;
        }
        if (kind == 5) {
            claimed[1] = 16;
            claimed.resize(72, 0);
            value.user_sid = claimed;
        }
        QVERIFY(!Identity::matchesCaller(value, claimed, value.session_id));
    }
    void nullProcessCaptureFailsClosedWithRawInvalidHandle() {
        const auto value = Identity::captureProcessToken(0);
        QVERIFY(!value.complete);
        QCOMPARE(value.error, std::uint32_t{ERROR_INVALID_HANDLE});
        QVERIFY(!Identity::normalDriver(value));
        QVERIFY(!Identity::elevatedObserver(value));
    }
    void currentProcessCaptureReadsOnlyItsTokenAndPreservesTheBorrowedProcessHandle() {
        const auto current = Identity::captureCurrentToken();
        QVERIFY(current.complete);
        QCOMPARE(current.error, 0U);
        QCOMPARE(current.failed_information_class, 0U);
        QVERIFY(validSid(current.user_sid));
        DWORD session{};
        QVERIFY(ProcessIdToSessionId(GetCurrentProcessId(), &session));
        QCOMPARE(current.session_id, std::uint32_t{session});
        std::unique_ptr<void, decltype(&CloseHandle)> process{
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId()), &CloseHandle};
        QVERIFY(process);
        const auto borrowed = Identity::captureProcessToken(reinterpret_cast<Identity::Handle>(process.get()));
        QVERIFY(borrowed.complete);
        QCOMPARE(GetProcessId(process.get()), GetCurrentProcessId());
        QVERIFY(borrowed.user_sid == current.user_sid);
        QCOMPARE(borrowed.session_id, current.session_id);
        QCOMPARE(borrowed.elevated, current.elevated);
        QCOMPARE(borrowed.elevation_type, current.elevation_type);
        QCOMPARE(borrowed.integrity_rid, current.integrity_rid);
        QCOMPARE(borrowed.ui_access, current.ui_access);
        QCOMPARE(borrowed.app_container, current.app_container);
        QCOMPARE(borrowed.capability_count, current.capability_count);
        QVERIFY(current.user_sid.size() <= Identity::maximum_sid_bytes);
        QVERIFY(current.restricted_sids.size() <= Identity::maximum_groups);
        QVERIFY(current.enabled_privileges.size() <= Identity::maximum_privileges);
        for (const auto& group : current.restricted_sids)
            QVERIFY(validSid(group.sid));
        for (const auto& privilege : current.enabled_privileges) {
            QVERIFY(!privilege.name.empty() && privilege.name.size() <= Identity::maximum_privilege_name_units);
            QVERIFY((privilege.attributes & SE_PRIVILEGE_ENABLED) != 0);
            QVERIFY(std::find(privilege.name.begin(), privilege.name.end(), L'\0') == privilege.name.end());
        }
        QVERIFY(Identity::matchesCaller(current, current.user_sid, current.session_id));
    }
};
QTEST_GUILESS_MAIN(AppContainerObserverIdentityTests)
#include "AppContainerObserverIdentityTests.moc"
