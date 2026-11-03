#include "adapters/WindowsNativeDownloadPipeReservation.hpp"

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
static_assert(!std::is_copy_constructible_v<WindowsNativeDownloadPipeReservation>);
static_assert(!std::is_copy_assignable_v<WindowsNativeDownloadPipeReservation>);
static_assert(noexcept(std::declval<WindowsNativeDownloadPipeReservation&>().reset()));

class Handle final {
  public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset() {
        if (valid())
            CloseHandle(value_);
        value_ = nullptr;
    }

  private:
    HANDLE value_;
};

QString endpoint() {
    return QStringLiteral("RcMusic-PipeReservation-Test_") + QUuid::createUuid().toString(QUuid::Id128);
}

QString pipePath(const QString& value) { return QStringLiteral("\\\\.\\pipe\\") + value; }

HANDLE createRawInstance(const QString& value, const bool first) {
    const auto name = pipePath(value).toStdWString();
    // DUPLEX includes READ_CONTROL. No production helper, private HANDLE or
    // friend is used to inspect the named pipe's externally visible security.
    return CreateNamedPipeW(name.c_str(),
                            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            PIPE_UNLIMITED_INSTANCES, 0, 0, 3'000, nullptr);
}

QString currentProcessUserSid() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
        return {};
    const Handle token{raw_token};
    DWORD bytes = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes));
    if (bytes < sizeof(TOKEN_USER) || bytes > 64 * 1024)
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
    return QString::fromWCharArray(text);
}
} // namespace

class WindowsNativeDownloadPipeReservationTests final : public QObject {
    Q_OBJECT

  private slots:
    void createsProtectedPipeGrantingOnlyCurrentUserAndSystem() {
        const auto value = endpoint();
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(reservation.acquire(value));
        const Handle observed{createRawInstance(value, false)};
        QVERIFY(observed.valid());
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
        const auto user_text = currentProcessUserSid().toStdWString();
        QVERIFY(!user_text.empty());
        PSID user = nullptr;
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
            GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
            MapGenericMask(&mask, &mapping);
            QCOMPARE(mask, DWORD{FILE_ALL_ACCESS});
            auto* sid = const_cast<DWORD*>(&ace->SidStart);
            QVERIFY(IsValidSid(sid));
            if (EqualSid(sid, user))
                ++user_aces;
            else if (EqualSid(sid, system.data()))
                ++system_aces;
            else
                QFAIL("Pipe reservation grants rights to an unexpected principal");
        }
        QCOMPARE(user_aces, 1);
        QCOMPARE(system_aces, 1);
    }

    void refusesPreexistingRawInstanceWithoutFirstInstanceFlag() {
        const auto value = endpoint();
        Handle blocker{createRawInstance(value, false)};
        QVERIFY(blocker.valid());
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(!reservation.acquire(value));
        DWORD flags = 0;
        QVERIFY(GetNamedPipeInfo(blocker.get(), &flags, nullptr, nullptr, nullptr));
        blocker.reset();
        QVERIFY(reservation.acquire(value));
    }

    void occupiedAnchorCannotReceiveAnOrdinaryClient() {
        const auto value = endpoint();
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(reservation.acquire(value));
        const auto name = pipePath(value).toStdWString();
        const Handle client{CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OVERLAPPED, nullptr)};
        const auto error = GetLastError();
        QVERIFY(!client.valid());
        QCOMPARE(error, DWORD{ERROR_PIPE_BUSY});
    }

    void nativeFirstInstanceRemainsUnavailableUntilReset() {
        const auto value = endpoint();
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(reservation.acquire(value));
        {
            const Handle forbidden{createRawInstance(value, true)};
            const auto error = GetLastError();
            QVERIFY(!forbidden.valid());
            QCOMPARE(error, DWORD{ERROR_ACCESS_DENIED});
        }
        reservation.reset();
        const Handle replacement{createRawInstance(value, true)};
        QVERIFY(replacement.valid());
    }

    void caseAliasesCannotReserveTheSameNativeName() {
        const auto value = endpoint();
        WindowsNativeDownloadPipeReservation reservation, contender;
        QVERIFY(reservation.acquire(value));
        QVERIFY(!contender.acquire(value.toUpper()));
        const Handle forbidden{createRawInstance(value.toLower(), true)};
        QVERIFY(!forbidden.valid());
        reservation.reset();
        QVERIFY(contender.acquire(value.toUpper()));
    }

    void repeatedAcquireCannotReplaceAnOwnedEndpoint() {
        const auto value = endpoint(), unrelated = endpoint();
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(reservation.acquire(value));
        QVERIFY(!reservation.acquire(value));
        QVERIFY(!reservation.acquire(unrelated));
        const Handle unrelated_owner{createRawInstance(unrelated, true)};
        QVERIFY(unrelated_owner.valid());
        const Handle forbidden{createRawInstance(value, true)};
        QVERIFY(!forbidden.valid());
    }

    void differentReservationsResetIndependently() {
        const auto first = endpoint(), second = endpoint();
        WindowsNativeDownloadPipeReservation owner, independent, contender;
        QVERIFY(owner.acquire(first));
        QVERIFY(independent.acquire(second));
        owner.reset();
        QVERIFY(contender.acquire(first));
        const Handle forbidden{createRawInstance(second, true)};
        QVERIFY(!forbidden.valid());
    }

    void destructionReleasesTheNameWhileThisProcessRemainsAlive() {
        const auto value = endpoint();
        {
            WindowsNativeDownloadPipeReservation reservation;
            QVERIFY(reservation.acquire(value));
            const Handle forbidden{createRawInstance(value, true)};
            QVERIFY(!forbidden.valid());
        }
        const Handle replacement{createRawInstance(value, true)};
        QVERIFY(replacement.valid());
    }

    void anotherNativeServerHandleRetainsTheNameAfterReset() {
        const auto value = endpoint();
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(reservation.acquire(value));
        Handle observed{createRawInstance(value, false)};
        QVERIFY(observed.valid());
        reservation.reset();
        {
            const Handle forbidden{createRawInstance(value, true)};
            QVERIFY(!forbidden.valid());
        }
        observed.reset();
        const Handle replacement{createRawInstance(value, true)};
        QVERIFY(replacement.valid());
    }

    void resetIsIdempotentAndAllowsReacquisition() {
        const auto value = endpoint();
        WindowsNativeDownloadPipeReservation reservation;
        reservation.reset();
        reservation.reset();
        QVERIFY(reservation.acquire(value));
        reservation.reset();
        reservation.reset();
        QVERIFY(reservation.acquire(value));
    }

    void acceptsNinetySixCharacterAsciiEndpoint() {
        const auto value = endpoint().leftJustified(96, QLatin1Char('A'));
        QCOMPARE(value.size(), qsizetype{96});
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(reservation.acquire(value));
        const Handle forbidden{createRawInstance(value, true)};
        QVERIFY(!forbidden.valid());
    }

    void rejectsInvalidEndpoints_data() {
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
        QTest::newRow("pipe-path") << pipePath(endpoint());
        QTest::newRow("global-prefix") << QStringLiteral("Global\\") + endpoint();
        QTest::newRow("control") << endpoint() + QChar{0x1f};
    }

    void rejectsInvalidEndpoints() {
        QFETCH(QString, value);
        WindowsNativeDownloadPipeReservation reservation;
        QVERIFY(!reservation.acquire(value));
        const auto valid = endpoint();
        QVERIFY(reservation.acquire(valid));
    }
};

QTEST_GUILESS_MAIN(WindowsNativeDownloadPipeReservationTests)
#include "WindowsNativeDownloadPipeReservationTests.moc"
