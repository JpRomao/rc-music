#include "app/PlayerDownloadComponentProbe.hpp"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
constexpr auto marker_bytes = "RCMusicDownloadTools-v1\n";
std::wstring native(const QString& path) { return QDir::toNativeSeparators(path).toStdWString(); }
QString marker(const QString& application) { return application + QStringLiteral("/download-tools/installed-v1"); }
bool write(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
PlayerDownloadComponentState readOnWorker(const QString& root) {
    auto result = PlayerDownloadComponentState::Failed;
    std::thread worker([&] {
        try {
            result = readPlayerDownloadComponentMarker(root);
        } catch (...) {
            result = PlayerDownloadComponentState::Failed;
        }
    });
    worker.join();
    return result;
}

struct Blocked final {
    std::mutex mutex;
    std::condition_variable condition;
    bool release_reader{false}, release_destructor{false}, block_destructor{false};
    std::atomic<bool> entered{false}, destructor_entered{false}, destroyed{false};
    std::atomic<bool> owner_destructor_entered{false}, owner_destructor_returned{false};
    std::atomic<DWORD> reader_thread{0}, destructor_thread{0};
    void releaseAll() {
        {
            const std::scoped_lock lock(mutex);
            release_reader = true;
            release_destructor = true;
        }
        condition.notify_all();
    }
    void releaseReader() {
        {
            const std::scoped_lock lock(mutex);
            release_reader = true;
        }
        condition.notify_all();
    }
    PlayerDownloadComponentState call() {
        reader_thread = GetCurrentThreadId();
        entered = true;
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return release_reader; });
        return PlayerDownloadComponentState::Enabled;
    }
};
struct Captured final {
    std::shared_ptr<Blocked> state;
    explicit Captured(std::shared_ptr<Blocked> value) : state(std::move(value)) {}
    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;
    ~Captured() {
        state->destructor_thread = GetCurrentThreadId();
        state->destructor_entered = true;
        std::unique_lock lock(state->mutex);
        state->condition.wait(lock, [&] { return !state->block_destructor || state->release_destructor; });
        state->destroyed = true;
    }
};

bool junction(const QString& path, const QString& target) {
    struct Header final {
        DWORD tag;
        WORD data_length, reserved, substitute_offset, substitute_length, print_offset, print_length;
    };
    static_assert(sizeof(Header) == 16);
    const auto print = native(target);
    const auto substitute = std::wstring{L"\\??\\"} + print;
    const auto substitute_bytes = substitute.size() * sizeof(wchar_t);
    const auto print_bytes = print.size() * sizeof(wchar_t);
    std::vector<std::byte> bytes(sizeof(Header) + substitute_bytes + print_bytes + 2 * sizeof(wchar_t));
    auto* header = reinterpret_cast<Header*>(bytes.data());
    header->tag = IO_REPARSE_TAG_MOUNT_POINT;
    header->data_length = static_cast<WORD>(bytes.size() - 8);
    header->substitute_length = static_cast<WORD>(substitute_bytes);
    header->print_offset = static_cast<WORD>(substitute_bytes + sizeof(wchar_t));
    header->print_length = static_cast<WORD>(print_bytes);
    std::memcpy(bytes.data() + sizeof(Header), substitute.data(), substitute_bytes);
    std::memcpy(bytes.data() + sizeof(Header) + header->print_offset, print.data(), print_bytes);
    const auto handle =
        CreateFileW(native(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;
    const bool result = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                                        nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return result;
}
struct JunctionGuard final {
    QTemporaryDir& owner;
    QString path;
    bool active{false};
    bool unlink() {
        if (!active)
            return true;
        if (!path.startsWith(owner.path() + QLatin1Char('/')) || !RemoveDirectoryW(native(path).c_str())) {
            owner.setAutoRemove(false);
            return false;
        }
        active = false;
        return true;
    }
    ~JunctionGuard() {
        if (!unlink())
            qWarning("Preserved own TEMP fixture after uncertain junction removal");
    }
};
} // namespace

class PlayerDownloadComponentProbeTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingToolsOrMarkerAreAbsentAndNeverCreated() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Absent);
        QVERIFY(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(QDir{}.mkdir(root.filePath(QStringLiteral("download-tools"))));
        QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Absent);
        QVERIFY(QDir(root.filePath(QStringLiteral("download-tools")))
                    .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                    .isEmpty());
    }
    void exactMarkerEnablesWithoutChangingAnyFile() {
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/RC Music gate espaço-音-XXXXXX")};
        QVERIFY(root.isValid());
        QVERIFY(QDir{}.mkdir(root.filePath(QStringLiteral("download-tools"))));
        QVERIFY(write(marker(root.path()), marker_bytes));
        QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Enabled);
        QCOMPARE(read(marker(root.path())), QByteArray{marker_bytes});
        QCOMPARE(
            QDir(root.filePath(QStringLiteral("download-tools"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
            QStringList{QStringLiteral("installed-v1")});
    }
    void invalidExistingMarkerObjectsArePreserved_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("tools-file") << QStringLiteral("tools-file");
        QTest::newRow("marker-directory") << QStringLiteral("marker-directory");
        QTest::newRow("oversize") << QStringLiteral("oversize");
        QTest::newRow("empty") << QStringLiteral("empty");
        QTest::newRow("corrupt-same-size") << QStringLiteral("corrupt");
        QTest::newRow("CRLF") << QStringLiteral("CRLF");
    }
    void invalidExistingMarkerObjectsArePreserved() {
        QFETCH(QString, kind);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto tools = root.filePath(QStringLiteral("download-tools"));
        if (kind == QStringLiteral("tools-file")) {
            QVERIFY(write(tools, "preserve tools collision"));
            QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Invalid);
            QCOMPARE(read(tools), QByteArray{"preserve tools collision"});
            return;
        }
        QVERIFY(QDir{}.mkdir(tools));
        if (kind == QStringLiteral("marker-directory")) {
            QVERIFY(QDir{}.mkdir(marker(root.path())));
            QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Invalid);
            QVERIFY(QDir(marker(root.path())).exists());
            return;
        }
        QByteArray bytes{marker_bytes};
        if (kind == QStringLiteral("oversize"))
            bytes.append('x');
        else if (kind == QStringLiteral("empty"))
            bytes.clear();
        else if (kind == QStringLiteral("corrupt"))
            bytes[0] = 'X';
        else if (kind == QStringLiteral("CRLF"))
            bytes.replace("\n", "\r\n");
        QVERIFY(write(marker(root.path()), bytes));
        QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Invalid);
        QCOMPARE(read(marker(root.path())), bytes);
    }
    void markerHardLinksAreInvalidAndBothNamesSurvive() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir{}.mkdir(root.filePath(QStringLiteral("download-tools"))));
        const auto target = root.filePath(QStringLiteral("owned-marker-target"));
        QVERIFY(write(target, marker_bytes));
        QVERIFY(CreateHardLinkW(native(marker(root.path())).c_str(), native(target).c_str(), nullptr));
        QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Invalid);
        QCOMPARE(read(marker(root.path())), QByteArray{marker_bytes});
        QCOMPARE(read(target), QByteArray{marker_bytes});
    }
    void anExistingMarkerSharingFailureIsFailedWithoutChangingBytes() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir{}.mkdir(root.filePath(QStringLiteral("download-tools"))));
        QVERIFY(write(marker(root.path()), marker_bytes));
        auto held = CreateFileW(native(marker(root.path())).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        const auto close = qScopeGuard([&] {
            if (held != INVALID_HANDLE_VALUE)
                CloseHandle(held);
        });
        QCOMPARE(readOnWorker(root.path()), PlayerDownloadComponentState::Failed);
        QVERIFY(CloseHandle(held));
        held = INVALID_HANDLE_VALUE;
        QCOMPARE(read(marker(root.path())), QByteArray{marker_bytes});
    }
    void invalidApplicationPathIsRejectedBeforeFilesystemAccess_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("relative") << QStringLiteral("relative-application");
        QTest::newRow("UNC") << QStringLiteral("//server.invalid/share/app");
        QTest::newRow("device") << QStringLiteral("//./NUL");
        QTest::newRow("reserved") << QStringLiteral("C:/NUL/app");
        QTest::newRow("ADS") << QStringLiteral("C:/fixture/app:stream");
    }
    void invalidApplicationPathIsRejectedBeforeFilesystemAccess() {
        QFETCH(QString, path);
        QCOMPARE(readOnWorker(path), PlayerDownloadComponentState::Invalid);
    }
    void ownedAncestorAndToolsJunctionsAreInvalidAndDoNotTouchTargets_data() {
        QTest::addColumn<bool>("ancestor");
        QTest::newRow("application-ancestor") << true;
        QTest::newRow("tools-directory") << false;
    }
    void ownedAncestorAndToolsJunctionsAreInvalidAndDoNotTouchTargets() {
        QFETCH(bool, ancestor);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto app = root.filePath(QStringLiteral("app"));
        const auto target = root.filePath(QStringLiteral("own-target"));
        QVERIFY(QDir{}.mkdir(app));
        QVERIFY(QDir{}.mkdir(target));
        QVERIFY(write(target + QStringLiteral("/preserve.keep"), "owned target"));
        const auto alias = ancestor ? root.filePath(QStringLiteral("alias")) : app + QStringLiteral("/download-tools");
        QVERIFY(QDir{}.mkdir(alias));
        JunctionGuard guard{root, alias};
        guard.active = junction(alias, target);
        if (!guard.active)
            QSKIP("Host filesystem did not permit the own TEMP junction fixture");
        const auto before = QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        QCOMPARE(readOnWorker(ancestor ? alias + QStringLiteral("/app") : app), PlayerDownloadComponentState::Invalid);
        QCOMPARE(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
        QCOMPARE(read(target + QStringLiteral("/preserve.keep")), QByteArray{"owned target"});
        QVERIFY(guard.unlink());
    }
    void constructorIsPassiveAndStartPollAreOneShot() {
        const auto state = std::make_shared<Blocked>();
        PlayerDownloadComponentProbe probe{[state] { return state->call(); }};
        const auto release = qScopeGuard([&] { state->releaseAll(); });
        QVERIFY(!state->entered);
        QVERIFY(probe.stopped());
        QVERIFY(!probe.poll());
        QVERIFY(probe.start());
        QVERIFY(!probe.start());
        QTRY_VERIFY_WITH_TIMEOUT(state->entered.load(), 5000);
        QVERIFY(state->reader_thread != GetCurrentThreadId());
        QVERIFY(!probe.stopped());
        QVERIFY(!probe.poll());
        state->releaseAll();
        QTRY_VERIFY_WITH_TIMEOUT(probe.stopped(), 5000);
        const auto result = probe.poll();
        QVERIFY(result);
        QCOMPARE(*result, PlayerDownloadComponentState::Enabled);
        QVERIFY(!probe.poll());
        QVERIFY(!probe.start());
    }
    void noResultOrStoppedAcknowledgementBeforeCapturedResourcesReleaseOnWorker() {
        const auto state = std::make_shared<Blocked>();
        state->block_destructor = true;
        auto resource = std::make_shared<Captured>(state);
        PlayerDownloadComponentProbe probe{[state, resource] { return state->call(); }};
        resource.reset();
        const auto release = qScopeGuard([&] { state->releaseAll(); });
        QVERIFY(probe.start());
        QTRY_VERIFY_WITH_TIMEOUT(state->entered.load(), 5000);
        state->releaseReader();
        QTRY_VERIFY_WITH_TIMEOUT(state->destructor_entered.load(), 5000);
        QVERIFY(!state->destroyed);
        QVERIFY(!probe.stopped());
        QVERIFY(!probe.poll());
        state->releaseAll();
        QTRY_VERIFY_WITH_TIMEOUT(probe.stopped(), 5000);
        QVERIFY(state->destroyed);
        QCOMPARE(state->destructor_thread.load(), state->reader_thread.load());
        QVERIFY(state->destructor_thread != GetCurrentThreadId());
        QVERIFY(probe.poll());
        QVERIFY(!probe.poll());
    }
    void emptyReaderCannotStart() {
        PlayerDownloadComponentProbe probe{PlayerDownloadComponentReader{}};
        QVERIFY(!probe.start());
        QVERIFY(!probe.start());
        QVERIFY(probe.stopped());
        QVERIFY(!probe.poll());
    }
    void readerExceptionsAndInvalidStatesFailConservatively_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("exception") << true;
        QTest::newRow("invalid-state") << false;
    }
    void readerExceptionsAndInvalidStatesFailConservatively() {
        QFETCH(bool, throws);
        PlayerDownloadComponentProbe probe{[throws]() -> PlayerDownloadComponentState {
            if (throws)
                throw std::runtime_error{"owned reader failure"};
            return static_cast<PlayerDownloadComponentState>(99);
        }};
        QVERIFY(probe.start());
        QTRY_VERIFY_WITH_TIMEOUT(probe.stopped(), 5000);
        const auto result = probe.poll();
        QVERIFY(result);
        QCOMPARE(*result, PlayerDownloadComponentState::Failed);
        QVERIFY(!probe.poll());
    }
    void cancellationBeforeStartSealsAdmissionWithoutCallingReader() {
        std::atomic<int> calls{0};
        PlayerDownloadComponentProbe probe{[&] {
            ++calls;
            return PlayerDownloadComponentState::Enabled;
        }};
        probe.cancel();
        probe.cancel();
        QVERIFY(!probe.start());
        QVERIFY(probe.stopped());
        QVERIFY(!probe.poll());
        QCOMPARE(calls.load(), 0);
    }
    void cancellationDuringReadDoesNotAcknowledgeStopOrExposeLateResults() {
        const auto state = std::make_shared<Blocked>();
        PlayerDownloadComponentProbe probe{[state] { return state->call(); }};
        const auto release = qScopeGuard([&] { state->releaseAll(); });
        QVERIFY(probe.start());
        QTRY_VERIFY_WITH_TIMEOUT(state->entered.load(), 5000);
        probe.cancel();
        probe.cancel();
        QVERIFY(!probe.stopped());
        QVERIFY(!probe.poll());
        state->releaseAll();
        QTRY_VERIFY_WITH_TIMEOUT(probe.stopped(), 5000);
        QVERIFY(!probe.poll());
        QVERIFY(!probe.start());
    }
    void cancellationAfterWorkerExitRevokesTheUnobservedResult() {
        PlayerDownloadComponentProbe probe{[] { return PlayerDownloadComponentState::Enabled; }};
        QVERIFY(probe.start());
        QTRY_VERIFY_WITH_TIMEOUT(probe.stopped(), 5000);
        probe.cancel();
        QVERIFY(!probe.poll());
        QVERIFY(!probe.start());
    }
    void destructorJoinsAndCapturedReaderResourcesDieOnTheWorker() {
        const auto state = std::make_shared<Blocked>();
        auto resource = std::make_shared<Captured>(state);
        auto probe = std::make_unique<PlayerDownloadComponentProbe>([state, resource] { return state->call(); });
        resource.reset();
        const auto release = qScopeGuard([&] { state->releaseAll(); });
        QVERIFY(probe->start());
        QTRY_VERIFY_WITH_TIMEOUT(state->entered.load(), 5000);
        std::jthread owner([owned = std::move(probe), state]() mutable {
            state->owner_destructor_entered = true;
            owned.reset();
            state->owner_destructor_returned = true;
        });
        const auto release_before_join = qScopeGuard([&] { state->releaseAll(); });
        QTRY_VERIFY_WITH_TIMEOUT(state->owner_destructor_entered.load(), 5000);
        QVERIFY(!state->owner_destructor_returned);
        state->releaseAll();
        QTRY_VERIFY_WITH_TIMEOUT(state->owner_destructor_returned.load(), 5000);
        QVERIFY(state->destroyed);
        QCOMPARE(state->destructor_thread.load(), state->reader_thread.load());
    }
};

QTEST_GUILESS_MAIN(PlayerDownloadComponentProbeTests)
#include "PlayerDownloadComponentProbeTests.moc"
