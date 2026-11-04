#include "adapters/WindowsNativeMediaProfile.hpp"

#include <QTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <functional>
#include <stdexcept>
#include <utility>

using namespace music_player::m0;
namespace {
using Native = WindowsNativeMediaProfile;
using CreateStatus = WindowsMediaProfileCreateStatus;
using Absence = WindowsMediaProfileAbsence;
const QString first_name = QStringLiteral("RCMusic.Media.abcdefab-cdef-4abc-8def-000000000001");
const QString second_name = QStringLiteral("RCMusic.Media.abcdefab-cdef-4abc-8def-000000000002");

QByteArray canonicalSid() {
    QByteArray bytes(40, '\0');
    bytes[0] = 1;
    bytes[1] = 8;
    bytes[7] = 15;
    bytes[8] = 2;
    for (qsizetype index = 1; index < 8; ++index)
        bytes[8 + index * 4] = static_cast<char>(index);
    return bytes;
}
qint32 code(const HRESULT value) { return static_cast<qint32>(value); }
QByteArray creatorSid() { return QByteArray::fromHex("0102000000000005150000002a000000"); }
Native::StorageLocation location() {
    const auto package = QStringLiteral("C:/owned/AppData/Local/Packages/") + first_name;
    const auto base = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
    const auto sid = QStringLiteral("S-1-15-2-1-2-3-4-5-6-7");
    return {first_name,
            canonicalSid(),
            creatorSid(),
            package,
            package + QStringLiteral("/AC"),
            base + QStringLiteral("Mappings\\") + sid,
            base + QStringLiteral("Storage\\") + sid};
}

struct Harness final {
    Native::Result create_result{code(S_OK), canonicalSid(), true};
    qint32 remove_result{code(S_OK)};
    Absence absence{Absence::Absent};
    int creates{0}, removes{0}, observations{0};
    QString created_name{}, removed_name{}, observed_name{};
    QByteArray observed_sid{};
    bool throw_create{false}, throw_remove{false}, throw_observe{false};
    std::function<void()> on_create{}, on_remove{}, on_observe{};
    std::optional<Native::StorageLocation> location_result{location()};
    int locations{};
    bool throw_location{false};
    QString location_name{};
    QByteArray location_sid{};
    std::function<void()> on_location{};
    Native::Calls calls() {
        return {.create =
                    [this](const QString& name) {
                        ++creates;
                        created_name = name;
                        if (on_create)
                            on_create();
                        if (throw_create)
                            throw std::runtime_error("uncertain native create");
                        return create_result;
                    },
                .remove =
                    [this](const QString& name) {
                        ++removes;
                        removed_name = name;
                        if (on_remove)
                            on_remove();
                        if (throw_remove)
                            throw std::runtime_error("uncertain native removal");
                        return remove_result;
                    },
                .observe =
                    [this](const QString& name, const QByteArray& sid) {
                        ++observations;
                        observed_name = name;
                        observed_sid = sid;
                        if (on_observe)
                            on_observe();
                        if (throw_observe)
                            throw std::runtime_error("uncertain absence");
                        return absence;
                    },
                .location =
                    [this](const QString& name, const QByteArray& sid) {
                        ++locations;
                        location_name = name;
                        location_sid = sid;
                        if (on_location)
                            on_location();
                        if (throw_location)
                            throw std::runtime_error("captured location unavailable");
                        return location_result;
                    }};
    }
};
} // namespace

class WindowsNativeMediaProfileTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionReportAndDestructionNeverInvokeCallbacks() {
        Harness h;
        {
            Native native(h.calls());
            const auto report = native.report();
            QVERIFY(!report.create_attempted && !report.created && !report.remove_attempted);
            QVERIFY(report.name.isEmpty() && report.sid.isEmpty());
            QVERIFY(!report.create_hresult && !report.remove_hresult);
            QVERIFY(report.sid_released);
            QCOMPARE(report.absence, Absence::Unknown);
        }
        QCOMPARE(h.creates, 0);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void systemFactoryIsInertUntilExplicitUse() {
        auto native = Native::system();
        QVERIFY(native);
        const auto report = native->report();
        QVERIFY(!report.create_attempted && !report.created && !report.remove_attempted);
        QVERIFY(report.name.isEmpty() && report.sid.isEmpty());
        QVERIFY(!report.create_hresult && !report.remove_hresult);
        QCOMPARE(report.absence, Absence::Unknown);
    }
    void incompleteCallbacksNeverFallBackToNativeCalls_data() {
        QTest::addColumn<int>("missing");
        QTest::newRow("create") << 0;
        QTest::newRow("remove") << 1;
        QTest::newRow("observe") << 2;
    }
    void incompleteCallbacksNeverFallBackToNativeCalls() {
        QFETCH(int, missing);
        Harness h;
        auto calls = h.calls();
        if (missing == 0)
            calls.create = {};
        else if (missing == 1)
            calls.remove = {};
        else
            calls.observe = {};
        Native native(std::move(calls));
        QCOMPARE(native.create(first_name).status, CreateStatus::Failed);
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.creates, 0);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void namesOutsideTheInternalCanonicalNamespaceAreRefused_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("empty") << QString{};
        QTest::newRow("other-prefix") << QStringLiteral("RCMusic.Test.abcdefab-cdef-4abc-8def-000000000001");
        QTest::newRow("uppercase") << QStringLiteral("RCMusic.Media.ABCDEFAB-CDEF-4ABC-8DEF-000000000001");
        QTest::newRow("wrong-version") << QStringLiteral("RCMusic.Media.abcdefab-cdef-1abc-8def-000000000001");
        QTest::newRow("wrong-variant") << QStringLiteral("RCMusic.Media.abcdefab-cdef-4abc-7def-000000000001");
        QTest::newRow("path") << first_name + QStringLiteral("/../other");
        QTest::newRow("embedded-nul") << first_name + QChar{} + QStringLiteral("suffix");
    }
    void namesOutsideTheInternalCanonicalNamespaceAreRefused() {
        QFETCH(QString, name);
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(name).status, CreateStatus::Failed);
        QCOMPARE(native.create(first_name).status, CreateStatus::Failed);
        QCOMPARE(h.creates, 0);
        QVERIFY(!native.report().create_attempted);
        QVERIFY(native.report().name.isEmpty());
        QVERIFY(!native.remove(name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void successfulCreationAndIndependentlyConfirmedRemovalKeepTheExactIdentity() {
        Harness h;
        Native native(h.calls());
        const auto created = native.create(first_name);
        QCOMPARE(created.status, CreateStatus::Created);
        QCOMPARE(created.sid, canonicalSid());
        QCOMPARE(h.creates, 1);
        QCOMPARE(h.created_name, first_name);
        const auto before = native.report();
        QVERIFY(before.create_attempted && before.created && before.sid_released);
        QVERIFY(!before.remove_attempted);
        QCOMPARE(before.name, first_name);
        QCOMPARE(before.sid, canonicalSid());
        QVERIFY(before.create_hresult == code(S_OK));
        const auto removed = native.remove(first_name, created.sid);
        QVERIFY(removed.api_succeeded);
        QCOMPARE(removed.absence, Absence::Absent);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, 1);
        QCOMPARE(h.removed_name, first_name);
        QCOMPARE(h.observed_name, first_name);
        QCOMPARE(h.observed_sid, canonicalSid());
        const auto after = native.report();
        QVERIFY(after.remove_attempted && after.created);
        QVERIFY(after.remove_hresult == code(S_OK));
        QCOMPARE(after.absence, Absence::Absent);
    }
    void collisionFailureAndNonSokSuccessNeverAdoptOrRemove_data() {
        QTest::addColumn<int>("hresult");
        QTest::addColumn<int>("status");
        QTest::newRow("collision") << int(code(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)))
                                   << int(CreateStatus::AlreadyExists);
        QTest::newRow("access-denied") << int(code(E_ACCESSDENIED)) << int(CreateStatus::Failed);
        QTest::newRow("failure") << int(code(E_FAIL)) << int(CreateStatus::Failed);
        QTest::newRow("s-false") << int(code(S_FALSE)) << int(CreateStatus::Failed);
    }
    void collisionFailureAndNonSokSuccessNeverAdoptOrRemove() {
        QFETCH(int, hresult);
        QFETCH(int, status);
        Harness h;
        h.create_result.hresult = hresult;
        Native native(h.calls());
        const auto result = native.create(first_name);
        QCOMPARE(int(result.status), status);
        QVERIFY(result.sid.isEmpty());
        QCOMPARE(h.creates, 1);
        const auto report = native.report();
        QVERIFY(report.create_attempted && !report.created);
        QCOMPARE(report.name, first_name);
        QVERIFY(report.sid.isEmpty());
        QVERIFY(report.create_hresult == hresult);
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void sokWithInvalidSidRemainsARecordedCreationWithoutRemovalAuthority_data() {
        QTest::addColumn<QByteArray>("sid");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("short") << canonicalSid().first(39);
        auto wrong = canonicalSid();
        wrong[7] = 16;
        QTest::newRow("wrong-authority") << wrong;
    }
    void sokWithInvalidSidRemainsARecordedCreationWithoutRemovalAuthority() {
        QFETCH(QByteArray, sid);
        Harness h;
        h.create_result.sid = sid;
        Native native(h.calls());
        const auto result = native.create(first_name);
        QCOMPARE(result.status, CreateStatus::Failed);
        QVERIFY(result.sid.isEmpty());
        const auto report = native.report();
        QVERIFY(report.create_attempted && report.created);
        QVERIFY(report.create_hresult == code(S_OK));
        QVERIFY(report.sid.isEmpty());
        QVERIFY(!native.remove(first_name, sid).api_succeeded);
        QCOMPARE(h.removes, 0);
    }
    void unacknowledgedSidReleaseCannotAuthorizeUseOrRemoval() {
        Harness h;
        h.create_result.sid_released = false;
        Native native(h.calls());
        const auto result = native.create(first_name);
        QCOMPARE(result.status, CreateStatus::Failed);
        QVERIFY(result.sid.isEmpty());
        const auto report = native.report();
        QVERIFY(report.create_attempted && report.created && !report.sid_released);
        QVERIFY(report.create_hresult == code(S_OK));
        QVERIFY(report.sid.isEmpty());
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void creationIsNeverRetriedOrRebound_data() {
        QTest::addColumn<int>("hresult");
        QTest::newRow("created") << int(code(S_OK));
        QTest::newRow("collision") << int(code(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)));
        QTest::newRow("failure") << int(code(E_FAIL));
    }
    void creationIsNeverRetriedOrRebound() {
        QFETCH(int, hresult);
        Harness h;
        h.create_result.hresult = hresult;
        Native native(h.calls());
        static_cast<void>(native.create(first_name));
        const auto report = native.report();
        QCOMPARE(native.create(first_name).status, CreateStatus::Failed);
        QCOMPARE(native.create(second_name).status, CreateStatus::Failed);
        QCOMPARE(h.creates, 1);
        QCOMPARE(native.report().name, report.name);
        QCOMPARE(native.report().sid, report.sid);
        QCOMPARE(native.report().create_hresult, report.create_hresult);
    }
    void createExceptionRemainsUnconfirmedWithoutRetryOrDestructorCleanup() {
        Harness h;
        h.throw_create = true;
        {
            Native native(h.calls());
            QCOMPARE(native.create(first_name).status, CreateStatus::Failed);
            const auto report = native.report();
            QVERIFY(report.create_attempted && !report.created);
            QCOMPARE(report.name, first_name);
            QVERIFY(!report.create_hresult && report.sid.isEmpty());
            QCOMPARE(native.create(second_name).status, CreateStatus::Failed);
            QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        }
        QCOMPARE(h.creates, 1);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void prematureOrMismatchedRemovalDoesNotConsumeTheLegitimateAttempt() {
        Harness h;
        Native native(h.calls());
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        auto other_sid = canonicalSid();
        other_sid[12] = 99;
        QVERIFY(!native.remove(second_name, canonicalSid()).api_succeeded);
        QVERIFY(!native.remove(first_name, other_sid).api_succeeded);
        QVERIFY(!native.remove(first_name, {}).api_succeeded);
        QVERIFY(!native.report().remove_attempted);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
        QVERIFY(native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, 1);
    }
    void removalNeedsExactSokAndIndependentAbsence_data() {
        QTest::addColumn<int>("hresult");
        QTest::addColumn<int>("absence");
        QTest::newRow("absent") << int(code(S_OK)) << int(Absence::Absent);
        QTest::newRow("present") << int(code(S_OK)) << int(Absence::Present);
        QTest::newRow("unknown") << int(code(S_OK)) << int(Absence::Unknown);
        QTest::newRow("s-false") << int(code(S_FALSE)) << int(Absence::Absent);
        QTest::newRow("access-denied") << int(code(E_ACCESSDENIED)) << int(Absence::Absent);
    }
    void removalNeedsExactSokAndIndependentAbsence() {
        QFETCH(int, hresult);
        QFETCH(int, absence);
        Harness h;
        h.remove_result = hresult;
        h.absence = static_cast<Absence>(absence);
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        const auto result = native.remove(first_name, canonicalSid());
        QCOMPARE(result.api_succeeded, hresult == code(S_OK));
        QCOMPARE(result.absence, hresult == code(S_OK) ? h.absence : Absence::Unknown);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, hresult == code(S_OK) ? 1 : 0);
        const auto report = native.report();
        QVERIFY(report.remove_attempted && report.created);
        QVERIFY(report.remove_hresult == hresult);
        QCOMPARE(report.absence, result.absence);
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, hresult == code(S_OK) ? 1 : 0);
    }
    void removalExceptionIsNeverRetriedAndDoesNotProveAbsence() {
        Harness h;
        h.throw_remove = true;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        const auto result = native.remove(first_name, canonicalSid());
        QVERIFY(!result.api_succeeded);
        QCOMPARE(result.absence, Absence::Unknown);
        QVERIFY(native.report().remove_attempted);
        QVERIFY(!native.report().remove_hresult);
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, 0);
    }
    void observationExceptionKeepsSokUnconfirmedWithoutRepeatingRemoval() {
        Harness h;
        h.throw_observe = true;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        const auto result = native.remove(first_name, canonicalSid());
        QVERIFY(result.api_succeeded);
        QCOMPARE(result.absence, Absence::Unknown);
        QVERIFY(native.report().remove_hresult == code(S_OK));
        QCOMPARE(native.report().absence, Absence::Unknown);
        QVERIFY(!native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, 1);
    }
    void reentrantCreateCannotRepeatCreationOrAcquireEarlyRemovalAuthority() {
        Harness h;
        Native native(h.calls());
        WindowsMediaProfileCreateResult nested_create;
        WindowsMediaProfileRemovalResult nested_remove;
        Native::Report during;
        h.on_create = [&] {
            during = native.report();
            nested_create = native.create(second_name);
            nested_remove = native.remove(first_name, canonicalSid());
        };
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QCOMPARE(nested_create.status, CreateStatus::Failed);
        QVERIFY(!nested_remove.api_succeeded);
        QVERIFY(during.create_attempted && !during.created && !during.create_hresult);
        QCOMPARE(during.name, first_name);
        QCOMPARE(h.creates, 1);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void reentrantRemovalAndObservationCannotRepeatTheNativeAttempt() {
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        WindowsMediaProfileRemovalResult during_remove, during_observe;
        Native::Report report;
        h.on_remove = [&] {
            report = native.report();
            during_remove = native.remove(first_name, canonicalSid());
        };
        h.on_observe = [&] { during_observe = native.remove(first_name, canonicalSid()); };
        QVERIFY(native.remove(first_name, canonicalSid()).api_succeeded);
        QVERIFY(report.remove_attempted && !report.remove_hresult);
        QVERIFY(!during_remove.api_succeeded && !during_observe.api_succeeded);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.observations, 1);
    }
    void destructionNeverRemovesAnUnclosedCreatedProfile() {
        Harness h;
        {
            Native native(h.calls());
            QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        }
        QCOMPARE(h.creates, 1);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void capturedLocationIsPassiveAndUnavailableBeforeCreation() {
        Harness h;
        Native native(h.calls());
        QVERIFY(!native.capturedStorageLocation());
        QCOMPARE(h.locations, 0);
        QCOMPARE(h.creates, 0);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
        auto system = Native::system();
        QVERIFY(!system->capturedStorageLocation());
        QVERIFY(!system->report().create_attempted);
    }
    void capturedLocationReturnsTheExactLivePairWithoutAnotherProfileOperation() {
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        const auto captured = native.capturedStorageLocation();
        QVERIFY(captured);
        QCOMPARE(captured->name, first_name);
        QCOMPARE(captured->sid, canonicalSid());
        QCOMPARE(captured->creator_user_sid, creatorSid());
        QCOMPARE(captured->package_root, location().package_root);
        QCOMPARE(captured->ac_path, location().ac_path);
        QCOMPARE(captured->mappings_key, location().mappings_key);
        QCOMPARE(captured->storage_key, location().storage_key);
        QCOMPARE(h.location_name, first_name);
        QCOMPARE(h.location_sid, canonicalSid());
        QCOMPARE(h.locations, 1);
        QCOMPARE(h.creates, 1);
        QCOMPARE(h.removes, 0);
        QCOMPARE(h.observations, 0);
    }
    void absentLocationPortDoesNotChangeExistingCreateAndRemoveBehavior() {
        Harness h;
        auto calls = h.calls();
        calls.location = {};
        Native native(std::move(calls));
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QVERIFY(!native.capturedStorageLocation());
        QCOMPARE(h.locations, 0);
        QVERIFY(native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 1);
    }
    void failedUnconfirmedOrCollidingCreationCannotQueryCapturedStorage_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 6; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void failedUnconfirmedOrCollidingCreationCannotQueryCapturedStorage() {
        QFETCH(int, kind);
        Harness h;
        if (kind == 0)
            h.create_result.hresult = code(E_FAIL);
        if (kind == 1)
            h.create_result.hresult = code(S_FALSE);
        if (kind == 2)
            h.create_result.hresult = code(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        if (kind == 3)
            h.create_result.sid.clear();
        if (kind == 4)
            h.create_result.sid_released = false;
        if (kind == 5)
            h.throw_create = true;
        Native native(h.calls());
        QVERIFY(native.create(first_name).status != CreateStatus::Created);
        QVERIFY(!native.capturedStorageLocation());
        QCOMPARE(h.locations, 0);
    }
    void locationCannotBeQueriedFromInsideTheCreationCallback() {
        Harness h;
        Native native(h.calls());
        bool refused = false;
        h.on_create = [&] { refused = !native.capturedStorageLocation(); };
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QVERIFY(refused);
        QCOMPARE(h.locations, 0);
        QVERIFY(native.capturedStorageLocation());
        QCOMPARE(h.locations, 1);
    }
    void missingOrThrowingLocationRepliesNeverFallBackOrRepeatCreation_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("missing") << false;
        QTest::newRow("throws") << true;
    }
    void missingOrThrowingLocationRepliesNeverFallBackOrRepeatCreation() {
        QFETCH(bool, throws);
        Harness h;
        if (throws)
            h.throw_location = true;
        else
            h.location_result.reset();
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QVERIFY(!native.capturedStorageLocation());
        QCOMPARE(h.locations, 1);
        QCOMPARE(h.creates, 1);
        QCOMPARE(h.removes, 0);
    }
    void removalAttemptImmediatelyRevokesLocationEvenWhenRemovalFails_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 3; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void removalAttemptImmediatelyRevokesLocationEvenWhenRemovalFails() {
        QFETCH(int, kind);
        Harness h;
        if (kind == 1)
            h.remove_result = code(E_FAIL);
        if (kind == 2)
            h.throw_remove = true;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QVERIFY(native.capturedStorageLocation());
        bool refusedDuring = false;
        h.on_remove = [&] { refusedDuring = !native.capturedStorageLocation(); };
        static_cast<void>(native.remove(first_name, canonicalSid()));
        QVERIFY(refusedDuring);
        QVERIFY(!native.capturedStorageLocation());
        QCOMPARE(h.locations, 1);
    }
    void mismatchedRemovalDoesNotRevokeTheCapturedLivePair() {
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QVERIFY(!native.remove(second_name, canonicalSid()).api_succeeded);
        QVERIFY(native.capturedStorageLocation());
        QVERIFY(!native.report().remove_attempted);
    }
    void malformedOrContradictoryLocationFieldsAreRefused_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 25; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void malformedOrContradictoryLocationFieldsAreRefused() {
        QFETCH(int, kind);
        Harness h;
        auto& captured = *h.location_result;
        if (kind == 0)
            captured.name = second_name;
        if (kind == 1)
            captured.sid[12] = 99;
        if (kind == 2)
            captured.creator_user_sid.clear();
        if (kind == 3)
            captured.creator_user_sid = QByteArray(7, '\0');
        if (kind == 4)
            captured.creator_user_sid[0] = 2;
        if (kind == 5) {
            captured.creator_user_sid.resize(72);
            captured.creator_user_sid[1] = 16;
        }
        if (kind == 6) {
            captured.package_root = QStringLiteral("relative/Packages/") + first_name;
            captured.ac_path = captured.package_root + QStringLiteral("/AC");
        }
        if (kind == 7)
            captured.package_root += QChar{0};
        if (kind == 8)
            captured.package_root += QChar{0xd800};
        if (kind == 9) {
            captured.package_root = QStringLiteral("C:/owned/../Packages/") + first_name;
            captured.ac_path = captured.package_root + QStringLiteral("/AC");
        }
        if (kind == 10) {
            captured.package_root = QStringLiteral("C:/owned/Packages/") + second_name;
            captured.ac_path = captured.package_root + QStringLiteral("/AC");
        }
        if (kind == 11)
            captured.ac_path = QStringLiteral("relative/AC");
        if (kind == 12)
            captured.ac_path = QStringLiteral("C:/other/AC");
        if (kind == 13)
            captured.ac_path += QStringLiteral("/child");
        if (kind == 14)
            captured.ac_path += QChar{0};
        if (kind == 15)
            captured.mappings_key.chop(1);
        if (kind == 16)
            captured.storage_key += QStringLiteral("\\child");
        if (kind == 17)
            captured.mappings_key.prepend(QStringLiteral("HKCU\\"));
        if (kind == 18)
            captured.storage_key.prepend(QStringLiteral("..\\"));
        if (kind == 19) {
            captured.package_root = QStringLiteral("C:/") + QString(32768, 'x');
            captured.ac_path = captured.package_root + QStringLiteral("/AC");
        }
        if (kind == 20)
            captured.mappings_key += QChar{0};
        if (kind == 21)
            captured.storage_key += QChar{0};
        if (kind == 22)
            captured.ac_path.clear();
        if (kind == 23)
            captured.package_root.clear();
        if (kind == 24)
            captured.sid = canonicalSid().first(39);
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        QVERIFY(!native.capturedStorageLocation());
        QCOMPARE(h.locations, 1);
        QCOMPARE(h.creates, 1);
        QVERIFY(!native.report().remove_attempted);
    }
    void returnedLocationOwnsBorrowedQtBytesAndPaths() {
        Harness h;
        auto sid = canonicalSid(), user = creatorSid();
        auto expected = location();
        QString name = expected.name, package = expected.package_root, ac = expected.ac_path,
                mappings = expected.mappings_key, storage = expected.storage_key;
        h.location_result = Native::StorageLocation{QString::fromRawData(name.constData(), name.size()),
                                                    QByteArray::fromRawData(sid.constData(), sid.size()),
                                                    QByteArray::fromRawData(user.constData(), user.size()),
                                                    QString::fromRawData(package.constData(), package.size()),
                                                    QString::fromRawData(ac.constData(), ac.size()),
                                                    QString::fromRawData(mappings.constData(), mappings.size()),
                                                    QString::fromRawData(storage.constData(), storage.size())};
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        const auto captured = native.capturedStorageLocation();
        QVERIFY(captured);
        sid[12] = 99;
        user[12] = 99;
        name[0] = QChar{'X'};
        package[0] = QChar{'X'};
        ac[0] = QChar{'X'};
        mappings[0] = QChar{'X'};
        storage[0] = QChar{'X'};
        QCOMPARE(captured->name, expected.name);
        QCOMPARE(captured->sid, expected.sid);
        QCOMPARE(captured->creator_user_sid, expected.creator_user_sid);
        QCOMPARE(captured->package_root, expected.package_root);
        QCOMPARE(captured->ac_path, expected.ac_path);
        QCOMPARE(captured->mappings_key, expected.mappings_key);
        QCOMPARE(captured->storage_key, expected.storage_key);
        QCOMPARE(native.report().sid, canonicalSid());
    }
    void reentrantLocationQueryDoesNotReinvokeTheLocationPort() {
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        bool entered = false;
        std::optional<Native::StorageLocation> nested;
        h.on_location = [&] {
            if (entered)
                return;
            entered = true;
            nested = native.capturedStorageLocation();
        };
        QVERIFY(native.capturedStorageLocation());
        QVERIFY(entered && !nested);
        QCOMPARE(h.locations, 1);
    }
    void locationCallbackCannotReturnAUsableSnapshotAfterReentrantRemoval() {
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        h.on_location = [&] { static_cast<void>(native.remove(first_name, canonicalSid())); };
        QVERIFY(!native.capturedStorageLocation());
        QVERIFY(native.report().remove_attempted);
        QCOMPARE(h.removes, 1);
        QCOMPARE(h.locations, 1);
    }
    void modifyingAReportSnapshotCannotRebindPrivateRemovalAuthority() {
        Harness h;
        Native native(h.calls());
        QCOMPARE(native.create(first_name).status, CreateStatus::Created);
        auto report = native.report();
        report.name = second_name;
        report.sid[12] = 99;
        report.created = false;
        QVERIFY(!native.remove(report.name, report.sid).api_succeeded);
        QCOMPARE(native.report().name, first_name);
        QCOMPARE(native.report().sid, canonicalSid());
        QVERIFY(native.report().created);
        QVERIFY(native.remove(first_name, canonicalSid()).api_succeeded);
        QCOMPARE(h.removes, 1);
    }
};
QTEST_GUILESS_MAIN(WindowsNativeMediaProfileTests)
#include "WindowsNativeMediaProfileTests.moc"
