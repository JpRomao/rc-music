#include "adapters/WindowsNativeDownloadLease.hpp"

#include <QCryptographicHash>
#include <QScopeGuard>
#include <QTest>
#include <QUuid>

#include <qt_windows.h>

#include <AclAPI.h>
#include <Sddl.h>

#include <array>
#include <type_traits>
#include <utility>
#include <vector>

using namespace music_player::m0;

namespace {
static_assert(!std::is_copy_constructible_v<WindowsNativeDownloadLease>);
static_assert(!std::is_copy_assignable_v<WindowsNativeDownloadLease>);
static_assert(noexcept(std::declval<WindowsNativeDownloadLease&>().reset()));

class Handle final {
  public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    void reset() {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
        value_ = nullptr;
    }

  private:
    HANDLE value_;
};

QString endpoint() { return QStringLiteral("RcMusic-Lease-Test_") + QUuid::createUuid().toString(QUuid::Id128); }

// Independent oracle: no production name getter, friend, configuration helper,
// or private HANDLE is used to inspect the externally observable kernel object.
QByteArray currentProcessUserSid() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
        return {};
    const Handle token{raw_token};
    DWORD bytes = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes));
    if (bytes == 0 || bytes > 64 * 1024)
        return {};
    std::vector<unsigned char> storage(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, storage.data(), bytes, &bytes))
        return {};
    const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    if (!IsValidSid(user->User.Sid))
        return {};
    wchar_t* text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &text))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text).toUtf8();
}

QString markerName(const QString& value) {
    const auto user = currentProcessUserSid();
    if (user.isEmpty())
        return {};
    const auto identity = user + QByteArray(1, '\0') + value.toLower().toUtf8();
    const auto digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("Global\\RCMusic.NativeDownloadOwner.v1.") + QString::fromLatin1(digest);
}

bool markerMissing(const QString& name) {
    const auto native_name = name.toStdWString();
    const HANDLE observed = OpenMutexW(READ_CONTROL, FALSE, native_name.c_str());
    const DWORD error = GetLastError();
    if (observed) {
        CloseHandle(observed);
        return false;
    }
    return error == ERROR_FILE_NOT_FOUND;
}
} // namespace

class WindowsNativeDownloadLeaseTests final : public QObject {
    Q_OBJECT

  private slots:
    void createsExpectedPrivateProtectedUserAndSystemMarker() {
        const auto value = endpoint();
        const auto name = markerName(value);
        QVERIFY(!name.isEmpty());
        QVERIFY(name.size() < MAX_PATH);
        QVERIFY(markerMissing(name));
        WindowsNativeDownloadLease lease;
        QVERIFY(lease.acquire(value));
        const Handle observed{OpenMutexW(READ_CONTROL, FALSE, name.toStdWString().c_str())};
        QVERIFY(observed.get() != nullptr);
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        QCOMPARE(GetSecurityInfo(observed.get(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                                 nullptr, &security),
                 DWORD{ERROR_SUCCESS});
        const auto release_security = qScopeGuard([&] { LocalFree(security); });
        QVERIFY(dacl != nullptr);
        QVERIFY(IsValidAcl(dacl));
        QCOMPARE(dacl->AceCount, WORD{2});
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision = 0;
        QVERIFY(GetSecurityDescriptorControl(security, &control, &revision));
        QVERIFY(control & SE_DACL_PROTECTED);
        PSID user = nullptr;
        const auto user_text = QString::fromUtf8(currentProcessUserSid()).toStdWString();
        QVERIFY(ConvertStringSidToSidW(user_text.c_str(), &user));
        const auto release_user = qScopeGuard([&] { LocalFree(user); });
        std::array<unsigned char, SECURITY_MAX_SID_SIZE> system{};
        DWORD system_bytes = static_cast<DWORD>(system.size());
        QVERIFY(CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &system_bytes));
        int user_aces = 0, system_aces = 0;
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* raw = nullptr;
            QVERIFY(GetAce(dacl, i, &raw));
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
            QCOMPARE(ace->Header.AceType, BYTE{ACCESS_ALLOWED_ACE_TYPE});
            QCOMPARE(ace->Header.AceFlags, BYTE{0});
            DWORD mask = ace->Mask;
            GENERIC_MAPPING mapping{READ_CONTROL, READ_CONTROL, READ_CONTROL | SYNCHRONIZE, MUTEX_ALL_ACCESS};
            MapGenericMask(&mask, &mapping);
            QCOMPARE(mask, DWORD{MUTEX_ALL_ACCESS});
            auto* sid = const_cast<DWORD*>(&ace->SidStart);
            QVERIFY(IsValidSid(sid));
            if (EqualSid(sid, user))
                ++user_aces;
            else if (EqualSid(sid, system.data()))
                ++system_aces;
            else
                QFAIL("Lease marker grants rights to an unexpected principal");
        }
        QCOMPARE(user_aces, 1);
        QCOMPARE(system_aces, 1);
    }

    void sameThreadCannotAcquireTheSameEndpointTwice() {
        const auto value = endpoint();
        WindowsNativeDownloadLease owner, contender;
        QVERIFY(owner.acquire(value));
        QVERIFY(!owner.acquire(value));
        QVERIFY(!contender.acquire(value));
        contender.reset();
        QVERIFY(!contender.acquire(value));
        owner.reset();
        QVERIFY(contender.acquire(value));
    }

    void endpointCaseAliasesCannotElectAnotherOwner() {
        const auto value = endpoint();
        QCOMPARE(markerName(value), markerName(value.toUpper()));
        WindowsNativeDownloadLease owner, contender;
        QVERIFY(owner.acquire(value));
        QVERIFY(!contender.acquire(value.toUpper()));
        QVERIFY(!contender.acquire(value.toLower()));
        owner.reset();
        QVERIFY(contender.acquire(value.toUpper()));
    }

    void distinctEndpointsCanCoexistAndResetIndependently() {
        const auto first = endpoint(), second = endpoint();
        WindowsNativeDownloadLease owner, independent, contender;
        QVERIFY(owner.acquire(first));
        QVERIFY(independent.acquire(second));
        owner.reset();
        QVERIFY(contender.acquire(first));
        WindowsNativeDownloadLease second_contender;
        QVERIFY(!second_contender.acquire(second));
        independent.reset();
        QVERIFY(second_contender.acquire(second));
    }

    void destructorReleasesOnlyItsOwnMarker() {
        const auto value = endpoint(), unrelated = endpoint();
        WindowsNativeDownloadLease other;
        QVERIFY(other.acquire(unrelated));
        {
            WindowsNativeDownloadLease owner;
            QVERIFY(owner.acquire(value));
            WindowsNativeDownloadLease contender;
            QVERIFY(!contender.acquire(value));
        }
        QVERIFY(markerMissing(markerName(value)));
        WindowsNativeDownloadLease replacement, unrelated_contender;
        QVERIFY(replacement.acquire(value));
        QVERIFY(!unrelated_contender.acquire(unrelated));
    }

    void preexistingFreeMutexStillRefusesOwnership() {
        const auto value = endpoint();
        const auto name = markerName(value).toStdWString();
        const Handle preexisting{CreateMutexExW(nullptr, name.c_str(), 0, MUTEX_ALL_ACCESS)};
        const DWORD create_error = GetLastError();
        QVERIFY(preexisting.get() != nullptr);
        QVERIFY(create_error != ERROR_ALREADY_EXISTS);
        WindowsNativeDownloadLease lease;
        QVERIFY(!lease.acquire(value));
        QVERIFY(!markerMissing(QString::fromStdWString(name)));
    }

    void differentKernelObjectTypeCollisionFailsClosed() {
        const auto value = endpoint();
        const auto name = markerName(value).toStdWString();
        Handle preexisting{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
        const DWORD create_error = GetLastError();
        QVERIFY(preexisting.get() != nullptr);
        QVERIFY(create_error != ERROR_ALREADY_EXISTS);
        WindowsNativeDownloadLease lease;
        QVERIFY(!lease.acquire(value));
        QVERIFY(SetEvent(preexisting.get()));
        preexisting.reset();
        QVERIFY(lease.acquire(value));
    }

    void retainedObserverHandleDelaysReacquisitionUntilLastHandleCloses() {
        const auto value = endpoint();
        WindowsNativeDownloadLease owner, replacement;
        QVERIFY(owner.acquire(value));
        Handle observed{OpenMutexW(READ_CONTROL, FALSE, markerName(value).toStdWString().c_str())};
        QVERIFY(observed.get() != nullptr);
        owner.reset();
        QVERIFY(!replacement.acquire(value));
        observed.reset();
        QVERIFY(replacement.acquire(value));
    }

    void resetWithoutAcquisitionIsIdempotent() {
        WindowsNativeDownloadLease lease;
        lease.reset();
        lease.reset();
        const auto value = endpoint();
        QVERIFY(lease.acquire(value));
        lease.reset();
        lease.reset();
        QVERIFY(markerMissing(markerName(value)));
        QVERIFY(lease.acquire(value));
    }

    void acceptsMaximumNinetySixCharacterAsciiEndpoint() {
        const auto value = endpoint().leftJustified(96, QLatin1Char('A'));
        QCOMPARE(value.size(), qsizetype{96});
        const auto name = markerName(value);
        QVERIFY(name.size() < MAX_PATH);
        WindowsNativeDownloadLease lease, contender;
        QVERIFY(lease.acquire(value));
        QVERIFY(!markerMissing(name));
        QVERIFY(!contender.acquire(value.toLower()));
        lease.reset();
        QVERIFY(markerMissing(name));
    }

    void invalidEndpointsCreateNoMarker_data() {
        QTest::addColumn<QString>("value");
        QTest::newRow("empty") << QString{};
        QTest::newRow("too-long") << endpoint().leftJustified(97, QLatin1Char('a'));
        QTest::newRow("slash") << endpoint() + QLatin1Char('/');
        QTest::newRow("backslash") << endpoint() + QLatin1Char('\\');
        QTest::newRow("dot") << endpoint() + QLatin1Char('.');
        QTest::newRow("space") << endpoint() + QLatin1Char(' ');
        QTest::newRow("colon") << endpoint() + QLatin1Char(':');
        QTest::newRow("unicode") << endpoint() + QStringLiteral("á");
        QTest::newRow("embedded-NUL") << endpoint() + QChar{0};
        QTest::newRow("pipe-path") << QStringLiteral("\\\\.\\pipe\\") + endpoint();
        QTest::newRow("global-prefix") << QStringLiteral("Global\\") + endpoint();
    }

    void invalidEndpointsCreateNoMarker() {
        QFETCH(QString, value);
        const auto name = markerName(value);
        QVERIFY(!name.isEmpty());
        QVERIFY(markerMissing(name));
        WindowsNativeDownloadLease lease;
        QVERIFY(!lease.acquire(value));
        QVERIFY(markerMissing(name));
    }
};

QTEST_GUILESS_MAIN(WindowsNativeDownloadLeaseTests)
#include "WindowsNativeDownloadLeaseTests.moc"
