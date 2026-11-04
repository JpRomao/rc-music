#include "../windows/sandbox/AppContainerObserverProcess.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <QCoreApplication>
#include <QTest>

#include <array>
#include <memory>
#include <type_traits>

using Witness = music_player::sandbox_probe::AppContainerObserverProcess;
using Identity = music_player::sandbox_probe::AppContainerObserverIdentity;
static_assert(!std::is_copy_constructible_v<Witness> && !std::is_copy_assignable_v<Witness>);
static_assert(!std::is_move_constructible_v<Witness> && !std::is_move_assignable_v<Witness>);
namespace {
std::uint64_t filetime(const FILETIME value) {
    return (std::uint64_t{value.dwHighDateTime} << 32) | value.dwLowDateTime;
}
struct OwnSuspendedChild final {
    HANDLE process{}, thread{};
    DWORD pid{};
    ~OwnSuspendedChild() {
        if (process) {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process, 254);
                WaitForSingleObject(process, 5000);
            }
            CloseHandle(process);
        }
        if (thread)
            CloseHandle(thread);
    }
    bool create() {
        const auto image = QString::fromUtf8(RC_MUSIC_TEST_SANDBOX_HELPER_PATH).toStdWString();
        auto command = std::wstring{L"\""} + image + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION created{};
        // This fixture never resumes the helper's first thread. No helper code,
        // AppContainer, socket, GUI or elevation path is executed.
        if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &created))
            return false;
        process = created.hProcess;
        thread = created.hThread;
        pid = created.dwProcessId;
        return true;
    }
};
} // namespace

class AppContainerObserverProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndPollingBeforeOpenDoNotAcquireProcessAuthority() {
        DWORD before{};
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &before));
        {
            Witness witness(GetCurrentProcessId());
            const auto& snapshot = witness.snapshot();
            QVERIFY(!snapshot.complete);
            QCOMPARE(snapshot.error, Witness::not_called);
            QCOMPARE(snapshot.pid, std::uint32_t{GetCurrentProcessId()});
            QCOMPARE(snapshot.creation_filetime, std::uint64_t{0});
            QVERIFY(snapshot.image_path.empty() && !snapshot.token.complete);
            const auto poll = witness.poll();
            QCOMPARE(poll.state, Witness::State::Unknown);
            QCOMPARE(poll.error, std::uint32_t{ERROR_INVALID_HANDLE});
            QVERIFY(!poll.exit_code);
        }
        DWORD after{};
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &after));
        QCOMPARE(after, before);
    }
    void zeroPidFailsOnceWithoutAnOwnedHandleOrSyntheticIdentity() {
        Witness witness(0);
        const auto* first = &witness.open();
        QVERIFY(!first->complete);
        QCOMPARE(first->error, std::uint32_t{ERROR_INVALID_PARAMETER});
        QVERIFY(!first->token.complete && first->image_path.empty());
        QCOMPARE(&witness.open(), first);
        QCOMPARE(&witness.snapshot(), first);
        const auto poll = witness.poll();
        QCOMPARE(poll.state, Witness::State::Unknown);
        QCOMPARE(poll.error, std::uint32_t{ERROR_INVALID_HANDLE});
        QVERIFY(!poll.exit_code);
    }
    void nonexistentPidRemainsIncompleteAcrossRepeatedOpenAttempts() {
        Witness witness(std::numeric_limits<std::uint32_t>::max());
        const auto* first = &witness.open();
        QVERIFY(!first->complete);
        QVERIFY(first->error != 0 && first->error != Witness::not_called);
        const auto error = first->error;
        QCOMPARE(&witness.open(), first);
        QCOMPARE(witness.snapshot().error, error);
        QVERIFY(witness.snapshot().image_path.empty());
        QCOMPARE(witness.poll().state, Witness::State::Unknown);
    }
    void currentProcessSnapshotBindsCreationImageAndTokenOnOneOwnedHandle() {
        FILETIME creation{}, exit{}, kernel{}, user{};
        QVERIFY(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user));
        std::array<wchar_t, Witness::maximum_image_path_units + 1> image{};
        DWORD length = static_cast<DWORD>(image.size());
        QVERIFY(QueryFullProcessImageNameW(GetCurrentProcess(), 0, image.data(), &length));
        const auto token = Identity::captureCurrentToken();
        QVERIFY(token.complete);
        Witness witness(GetCurrentProcessId());
        const auto& snapshot = witness.open();
        QVERIFY(snapshot.complete);
        QCOMPARE(snapshot.error, 0U);
        QCOMPARE(snapshot.pid, std::uint32_t{GetCurrentProcessId()});
        QCOMPARE(snapshot.creation_filetime, filetime(creation));
        QVERIFY(snapshot.image_path == std::wstring(image.data(), length));
        QVERIFY(snapshot.image_path.size() <= Witness::maximum_image_path_units);
        QVERIFY(snapshot.token.complete && snapshot.token.user_sid == token.user_sid);
        QCOMPARE(snapshot.token.session_id, token.session_id);
        QCOMPARE(snapshot.token.elevated, token.elevated);
        QCOMPARE(snapshot.token.elevation_type, token.elevation_type);
        QCOMPARE(snapshot.token.integrity_rid, token.integrity_rid);
        const auto poll = witness.poll();
        QCOMPARE(poll.state, Witness::State::Running);
        QCOMPARE(poll.error, 0U);
        QVERIFY(!poll.exit_code);
    }
    void openIsOneShotAndDestructionClosesOnlyTheWitnessHandle() {
        // Warm up the independent token inspector before measuring owned handles.
        QVERIFY(Identity::captureCurrentToken().complete);
        DWORD before{};
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &before));
        std::unique_ptr<void, decltype(&CloseHandle)> peer{
            OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId()), &CloseHandle};
        QVERIFY(peer);
        {
            Witness witness(GetCurrentProcessId());
            const auto* first = &witness.open();
            QVERIFY(first->complete);
            DWORD opened{};
            QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &opened));
            QCOMPARE(opened, before + 2);
            for (int i = 0; i < 10; ++i) {
                QCOMPARE(&witness.open(), first);
                QCOMPARE(witness.poll().state, Witness::State::Running);
            }
            DWORD repeated{};
            QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &repeated));
            QCOMPARE(repeated, opened);
        }
        QCOMPARE(GetProcessId(peer.get()), GetCurrentProcessId());
        QCOMPARE(WaitForSingleObject(peer.get(), 0), DWORD{WAIT_TIMEOUT});
        peer.reset();
        DWORD after{};
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &after));
        QCOMPARE(after, before);
    }
    void signaledProcessWithExitCode259IsStoppedAndKeepsItsOriginalSnapshot() {
        OwnSuspendedChild child;
        QVERIFY(child.create());
        Witness witness(child.pid);
        const auto* first = &witness.open();
        QVERIFY(first->complete);
        const auto creation = first->creation_filetime;
        const auto image = first->image_path;
        const auto user = first->token.user_sid;
        QCOMPARE(witness.poll().state, Witness::State::Running);
        QVERIFY(TerminateProcess(child.process, 259));
        QCOMPARE(WaitForSingleObject(child.process, 5000), DWORD{WAIT_OBJECT_0});
        const auto poll = witness.poll();
        QCOMPARE(poll.state, Witness::State::Stopped);
        QCOMPARE(poll.error, 0U);
        QVERIFY(poll.exit_code);
        QCOMPARE(*poll.exit_code, 259U);
        QCOMPARE(&witness.open(), first);
        QVERIFY(witness.snapshot().complete);
        QCOMPARE(witness.snapshot().creation_filetime, creation);
        QVERIFY(witness.snapshot().image_path == image && witness.snapshot().token.user_sid == user);
        const auto repeated = witness.poll();
        QCOMPARE(repeated.state, Witness::State::Stopped);
        QVERIFY(repeated.exit_code && *repeated.exit_code == 259U);
    }
    void destroyingWitnessDoesNotTerminateItsPeerOrCloseAnotherOwnersHandle() {
        OwnSuspendedChild child;
        QVERIFY(child.create());
        {
            Witness witness(child.pid);
            QVERIFY(witness.open().complete);
            QCOMPARE(witness.poll().state, Witness::State::Running);
        }
        QCOMPARE(GetProcessId(child.process), child.pid);
        QCOMPARE(WaitForSingleObject(child.process, 0), DWORD{WAIT_TIMEOUT});
        QVERIFY(TerminateProcess(child.process, 7));
        QCOMPARE(WaitForSingleObject(child.process, 5000), DWORD{WAIT_OBJECT_0});
        DWORD code{};
        QVERIFY(GetExitCodeProcess(child.process, &code));
        QCOMPARE(code, DWORD{7});
    }
};
QTEST_GUILESS_MAIN(AppContainerObserverProcessTests)
#include "AppContainerObserverProcessTests.moc"
