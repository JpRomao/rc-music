#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"
#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/application/DownloadService.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <AclAPI.h>
#include <Sddl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
using download_artifacts::Handle;
using download_artifacts::nativePath;

class Security final {
  public:
    Security() {
        Handle token;
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return;
        token = Handle(raw);
        DWORD size = 0;
        static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
        std::vector<unsigned char> buffer(size);
        if (buffer.empty() || !GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
            return;
        auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(user->User.Sid, &text))
            return;
        user_sid = QString::fromWCharArray(text);
        LocalFree(text);
    }
    ~Security() { LocalFree(descriptor_); }
    SECURITY_ATTRIBUTES* attributes(bool allow_world = false) {
        LocalFree(std::exchange(descriptor_, nullptr));
        const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + user_sid + QLatin1Char(')') +
                          (allow_world ? QStringLiteral("(A;;FR;;;WD)") : QString{});
        if (user_sid.isEmpty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(
                                      sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
            return nullptr;
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
        return &attributes_;
    }
    QString user_sid{};

  private:
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    SECURITY_ATTRIBUTES attributes_{};
};

bool protectedPrivate(HANDLE handle, const QString& user_sid) {
    PSECURITY_DESCRIPTOR security = nullptr;
    PACL dacl = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
                        &security) != ERROR_SUCCESS)
        return false;
    const auto free = qScopeGuard([&] { LocalFree(security); });
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(security, &control, &revision) || !(control & SE_DACL_PROTECTED) || !dacl ||
        dacl->AceCount != 2)
        return false;
    bool user = false, system = false;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw))
            return false;
        const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 || ace->Mask != FILE_ALL_ACCESS)
            return false;
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(const_cast<DWORD*>(&ace->SidStart), &text))
            return false;
        const QString principal = QString::fromWCharArray(text);
        LocalFree(text);
        user |= principal == user_sid;
        system |= principal == QStringLiteral("S-1-5-18");
        if (principal != user_sid && principal != QStringLiteral("S-1-5-18"))
            return false;
    }
    return user && system;
}

struct Fixture final {
    QTemporaryDir root{};
    Security security{};
    QString directory{root.filePath(QStringLiteral("private checkpoint espaço 音"))};
    bool created{false};
    explicit Fixture(bool allow_world = false) {
        auto* attributes = security.attributes(allow_world);
        created = root.isValid() && attributes && CreateDirectoryW(nativePath(directory).c_str(), attributes);
    }
    QString journal() const { return directory + QStringLiteral("/download-checkpoint.json"); }
    QString lock() const { return directory + QStringLiteral("/download-checkpoint.lock"); }
    QStringList entries() const { return QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
    bool write(const QString& path, const QByteArray& bytes, bool allow_world = false) {
        auto* attributes = security.attributes(allow_world);
        if (!attributes)
            return false;
        Handle handle(CreateFileW(nativePath(path).c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                  FILE_SHARE_READ, attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!handle.valid())
            return false;
        DWORD written = 0;
        return WriteFile(handle.get(), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
               written == static_cast<DWORD>(bytes.size()) && FlushFileBuffers(handle.get());
    }
    QByteArray bytes() const {
        QFile file(journal());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    bool privateFile(const QString& path) const {
        Handle handle(CreateFileW(nativePath(path).c_str(), READ_CONTROL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        return handle.valid() && protectedPrivate(handle.get(), security.user_sid);
    }
};

DownloadCheckpoint example(const std::string& title = "Áudio sintético 音") {
    return {
        .receipts = {{.request_id = "abcdefab-cdef-4abc-8def-000000000001",
                      .fingerprint = std::string(64, 'a'),
                      .job_ids = {"run-1"},
                      .retained_request_bytes = 200}},
        .jobs = {{.job_id = "run-1",
                  .title = title,
                  .state = DownloadJobState::Completed,
                  .completed_units = 64,
                  .total_units = 64,
                  .completed_mp3 = std::filesystem::path{L"C:/synthetic candidate/音.mp3"}}},
    };
}

struct ObservedExecution final : DownloadExecution {
    int starts{0}, polls{0}, cancels{0};
    DownloadExecutionUpdate update{
        .state = DownloadExecutionState::Published,
        .phase = DownloadPhase::Publishing,
        .completed_units = 64,
        .total_units = 64,
        .published_mp3 = std::filesystem::path{L"C:/synthetic candidate/音.mp3"},
    };
    bool start(const std::string& id, const DownloadItem&) override {
        ++starts;
        update.job_id = id;
        return true;
    }
    void cancel(std::string_view) override { ++cancels; }
    DownloadExecutionUpdate poll(std::string_view) override {
        ++polls;
        return update;
    }
};
struct ObservedImport final : DownloadImport {
    int calls{0};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};

struct CommitGate final {
    std::atomic<bool> entered{false};
    std::mutex mutex;
    std::condition_variable changed;
    bool released{false};
    bool wait() {
        std::unique_lock lock(mutex);
        entered.store(true);
        return changed.wait_for(lock, std::chrono::seconds{5}, [&] { return released; });
    }
    void release() {
        const std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};

// Alias and target belong to the same fresh QTemporaryDir. Always unlink the
// alias directly before Qt cleanup, including on a failing test assertion.
class AliasGuard final {
  public:
    AliasGuard(Fixture& fixture, QString alias, bool directory)
        : fixture_(fixture), alias_(std::move(alias)), directory_(directory) {}
    ~AliasGuard() {
        if (created && !unlink())
            fixture_.root.setAutoRemove(false);
    }
    bool unlink() {
        if (!created)
            return true;
        const bool removed = directory_ ? RemoveDirectoryW(nativePath(alias_).c_str()) != FALSE
                                        : DeleteFileW(nativePath(alias_).c_str()) != FALSE;
        if (removed)
            created = false;
        return removed;
    }
    bool created{false};

  private:
    Fixture& fixture_;
    QString alias_;
    bool directory_;
};
} // namespace

class WindowsDownloadCheckpointStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void actualWorkerAndStoreGateAdmissionAndRecoverWithoutExecution() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const DownloadRequest request{
            .request_id = "abcdefab-cdef-4abc-8def-000000000001",
            .items = {{.url = "https://media.example.test/audio.mp3?private-token=not-saved",
                       .title = "Synthetic durable admission",
                       .kind = DownloadMediaKind::Audio}},
        };
        QtDownloadRequestIdentity identity;
        ObservedExecution execution;
        ObservedImport importer;
        DownloadService service(execution, importer, "original", {}, &identity);
        CommitGate gate;
        const auto caller = std::this_thread::get_id();
        std::atomic<bool> save_on_worker{false};
        std::atomic<int> saves{0};
        SingleWorkerDownloadCheckpointIo io([&] {
            return std::make_unique<WindowsDownloadCheckpointStore>(
                fixture.directory, WindowsDownloadCheckpointStoreOptions{.inject_fault = [&](const auto point) {
                    if (point != WindowsDownloadCheckpointFaultPoint::BeforeCommit)
                        return false;
                    save_on_worker.store(std::this_thread::get_id() != caller);
                    if (saves.fetch_add(1) == 0)
                        return !gate.wait();
                    return false;
                }});
        });
        const auto release = qScopeGuard([&] { gate.release(); });
        DownloadAdmissionController owner(service, io);
        QVERIFY(owner.start());
        QTRY_VERIFY_WITH_TIMEOUT((owner.tick(0), owner.state() == DownloadAdmissionControllerState::Ready), 5000);
        QCOMPARE(owner.loadStatus(), std::optional{DownloadCheckpointLoadStatus::Missing});
        QCOMPARE(owner.begin(request).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(), 5000);
        QVERIFY(save_on_worker.load());
        owner.tick(0);
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.calls, 0);
        QVERIFY(!owner.poll(request.request_id));
        QVERIFY(!QFileInfo::exists(fixture.journal()));
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT((owner.tick(0), owner.state() == DownloadAdmissionControllerState::Ready), 5000);
        const auto acknowledged = owner.poll(request.request_id);
        QVERIFY(acknowledged && acknowledged->error == DownloadSubmissionError::None);
        QCOMPARE(acknowledged->job_ids.size(), std::size_t{1});
        const auto replay = owner.begin(request);
        QCOMPARE(replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(replay.result.error, acknowledged->error);
        QVERIFY(replay.result.job_ids == acknowledged->job_ids);
        QCOMPARE(saves.load(), 1);
        owner.tick(0);
        QCOMPARE(execution.starts, 1);
        QCOMPARE(importer.calls, 1);
        QVERIFY(fixture.privateFile(fixture.journal()));
        QVERIFY(!fixture.bytes().contains("media.example.test"));
        QVERIFY(!fixture.bytes().contains("private-token"));
        owner.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT((owner.tick(0), owner.stopped()), 5000);
        QCOMPARE(owner.saveStatus(), std::optional{DownloadCheckpointSaveStatus::Saved});
        QVERIFY(io.stopped());
        QCOMPARE(saves.load(), 2);

        ObservedExecution restored_execution;
        ObservedImport restored_import;
        DownloadService restored_service(restored_execution, restored_import, "fresh", {}, &identity);
        SingleWorkerDownloadCheckpointIo restored_io(
            [&] { return std::make_unique<WindowsDownloadCheckpointStore>(fixture.directory); });
        DownloadAdmissionController restored(restored_service, restored_io);
        QVERIFY(restored.start());
        QTRY_VERIFY_WITH_TIMEOUT((restored.tick(0), restored.state() == DownloadAdmissionControllerState::Ready), 5000);
        QCOMPARE(restored.loadStatus(), std::optional{DownloadCheckpointLoadStatus::Loaded});
        QCOMPARE(restored_service.snapshot()[0].state, DownloadJobState::Completed);
        QVERIFY(restored_service.snapshot()[0].completed_mp3.empty());
        const auto recovered_replay = restored.begin(request);
        QCOMPARE(recovered_replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(recovered_replay.result.error, acknowledged->error);
        QVERIFY(recovered_replay.result.job_ids == acknowledged->job_ids);
        restored.tick(0);
        QCOMPARE(restored_execution.starts, 0);
        QCOMPARE(restored_import.calls, 0);
        restored.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT((restored.tick(0), restored.stopped()), 5000);
    }
    void actualWorkerNeverAcknowledgesFailedPostCommitByReadingItBack() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const DownloadRequest request{
            .request_id = "abcdefab-cdef-4abc-8def-000000000002",
            .items = {{.url = "https://media.example.test/audio.mp3", .title = "Failed commit acknowledgement"}},
        };
        QtDownloadRequestIdentity identity;
        ObservedExecution execution;
        ObservedImport importer;
        DownloadService service(execution, importer, "unacknowledged", {}, &identity);
        std::atomic<int> saves{0};
        SingleWorkerDownloadCheckpointIo io([&] {
            return std::make_unique<WindowsDownloadCheckpointStore>(
                fixture.directory, WindowsDownloadCheckpointStoreOptions{.inject_fault = [&](const auto point) {
                    if (point == WindowsDownloadCheckpointFaultPoint::AfterCommit) {
                        saves.fetch_add(1);
                        return true;
                    }
                    return false;
                }});
        });
        DownloadAdmissionController owner(service, io);
        QVERIFY(owner.start());
        QTRY_VERIFY_WITH_TIMEOUT((owner.tick(0), owner.state() == DownloadAdmissionControllerState::Ready), 5000);
        QCOMPARE(owner.begin(request).state, DownloadAdmissionState::Pending);
        QTRY_VERIFY_WITH_TIMEOUT((owner.tick(0), owner.state() == DownloadAdmissionControllerState::AdmissionFailed),
                                 5000);
        const auto failed = owner.poll(request.request_id);
        QVERIFY(failed && failed->error == DownloadSubmissionError::NotAvailable && failed->job_ids.empty());
        QCOMPARE(execution.starts, 0);
        QCOMPARE(importer.calls, 0);
        QVERIFY(QFileInfo::exists(fixture.journal()));
        WindowsDownloadCheckpointStore observer(fixture.directory);
        QCOMPARE(observer.load().status, DownloadCheckpointLoadStatus::Loaded);
        owner.tick(0);
        QCOMPARE(owner.begin(request).result.error, DownloadSubmissionError::NotAvailable);
        QCOMPARE(saves.load(), 1);
        owner.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT((owner.tick(0), owner.stopped()), 5000);
        QCOMPARE(owner.saveStatus(), std::optional{DownloadCheckpointSaveStatus::Failed});
        QCOMPARE(saves.load(), 1);
    }
    void missingJournalDoesNotCreateDirectoryOrDocument() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto before = fixture.entries();
        WindowsDownloadCheckpointStore store(fixture.directory);
        const auto loaded = store.load();
        QCOMPARE(loaded.status, WindowsDownloadCheckpointLoadStatus::Missing);
        QVERIFY(!loaded.checkpoint);
        QCOMPARE(fixture.entries(), before);
    }
    void roundTripAndReplaceOnlyKnownValidPrivateJournal() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadCheckpointStore store(fixture.directory);
        const auto first = example();
        QVERIFY(validDownloadCheckpoint(first));
        QCOMPARE(store.save(first), WindowsDownloadCheckpointSaveStatus::Saved);
        const auto loaded = store.load();
        QCOMPARE(loaded.status, WindowsDownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.checkpoint && *loaded.checkpoint == first);
        const auto second = example("Segundo áudio");
        QCOMPARE(store.save(second), WindowsDownloadCheckpointSaveStatus::Saved);
        const auto replaced = store.load();
        QCOMPARE(replaced.status, WindowsDownloadCheckpointLoadStatus::Loaded);
        QVERIFY(replaced.checkpoint && *replaced.checkpoint == second);
        QCOMPARE(fixture.entries(), QStringList({"download-checkpoint.json", "download-checkpoint.lock"}));
    }
    void replacesLegacyJournalWithArchiveAndLoadsAfterReopen() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto legacy = example();
        const auto& receipt = legacy.receipts.front();
        const DownloadCheckpoint archived{
            .archived_receipts = {{receipt.request_id, receipt.fingerprint, receipt.job_ids}},
        };
        {
            WindowsDownloadCheckpointStore store(fixture.directory);
            QCOMPARE(store.save(legacy), WindowsDownloadCheckpointSaveStatus::Saved);
            const auto legacy_bytes = fixture.bytes();
            QVERIFY(legacy_bytes.contains("\"schemaVersion\":1"));
            const auto loaded = store.load();
            QCOMPARE(loaded.status, WindowsDownloadCheckpointLoadStatus::Loaded);
            QVERIFY(loaded.checkpoint && *loaded.checkpoint == legacy);
            QCOMPARE(fixture.bytes(), legacy_bytes);
            QCOMPARE(store.save(archived), WindowsDownloadCheckpointSaveStatus::Saved);
        }

        const auto archived_bytes = fixture.bytes();
        QVERIFY(archived_bytes.contains("\"schemaVersion\":2"));
        QVERIFY(!archived_bytes.contains("synthetic candidate"));
        WindowsDownloadCheckpointStore reopened(fixture.directory);
        const auto loaded = reopened.load();
        QCOMPARE(loaded.status, WindowsDownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.checkpoint && *loaded.checkpoint == archived);
        QVERIFY(fixture.privateFile(fixture.journal()));
        QCOMPARE(fixture.bytes(), archived_bytes);
    }
    void newJournalLockAndTemporaryHaveProtectedPrivateDacl() {
        Fixture fixture;
        QVERIFY(fixture.created);
        bool saw_temporary = false, temporary_private = false;
        WindowsDownloadCheckpointStoreOptions options;
        options.inject_fault = [&](const WindowsDownloadCheckpointFaultPoint point) {
            if (point == WindowsDownloadCheckpointFaultPoint::BeforeCommit) {
                const auto temporaries = QDir(fixture.directory).entryList({"download-checkpoint-*.tmp"}, QDir::Files);
                saw_temporary = temporaries.size() == 1;
                temporary_private = saw_temporary && fixture.privateFile(fixture.directory + '/' + temporaries[0]);
            }
            return false;
        };
        WindowsDownloadCheckpointStore store(fixture.directory, options);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        QVERIFY(saw_temporary);
        QVERIFY(temporary_private);
        QVERIFY(fixture.privateFile(fixture.journal()));
        QVERIFY(fixture.privateFile(fixture.lock()));
    }
    void refusesUnknownExistingDocumentWithoutChangingItsBytes_data() {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<int>("status");
        QTest::newRow("future") << QByteArray("{\"schemaVersion\":999}")
                                << int(WindowsDownloadCheckpointLoadStatus::FutureVersion);
        QTest::newRow("corrupt") << QByteArray("{broken checkpoint")
                                 << int(WindowsDownloadCheckpointLoadStatus::Invalid);
        QTest::newRow("unknown-shape") << QByteArray("{\"schemaVersion\":1,\"unexpected\":true}")
                                       << int(WindowsDownloadCheckpointLoadStatus::Invalid);
        QTest::newRow("empty") << QByteArray{} << int(WindowsDownloadCheckpointLoadStatus::Invalid);
    }
    void refusesUnknownExistingDocumentWithoutChangingItsBytes() {
        QFETCH(QByteArray, document);
        QFETCH(int, status);
        Fixture fixture;
        QVERIFY(fixture.created);
        QVERIFY(fixture.write(fixture.journal(), document));
        WindowsDownloadCheckpointStore store(fixture.directory);
        const auto loaded = store.load();
        QCOMPARE(int(loaded.status), status);
        QVERIFY(!loaded.checkpoint);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), document);
        QVERIFY(QDir(fixture.directory).entryList({"*.tmp"}, QDir::Files).isEmpty());
    }
    void refusesOversizedExistingDocumentWithoutTruncation() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const QByteArray document(WindowsDownloadCheckpointStore::maximum_file_size + 1, 'x');
        QVERIFY(fixture.write(fixture.journal(), document));
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Invalid);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), document);
    }
    void invalidIncomingCheckpointDoesNotTouchCurrentDocument() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        auto invalid = example();
        invalid.jobs.push_back(invalid.jobs.front());
        QCOMPARE(store.save(invalid), WindowsDownloadCheckpointSaveStatus::Failed);
        QCOMPARE(fixture.bytes(), before);
    }
    void beforeCommitFailureKeepsPreviousBytesAndCleansOwnedTemporary() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadCheckpointStore initial(fixture.directory);
        QCOMPARE(initial.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        WindowsDownloadCheckpointStore store(fixture.directory, {.inject_fault = [](const auto point) {
                                                 return point == WindowsDownloadCheckpointFaultPoint::BeforeCommit;
                                             }});
        QCOMPARE(store.save(example("not committed")), WindowsDownloadCheckpointSaveStatus::Failed);
        QCOMPARE(fixture.bytes(), before);
        QVERIFY(QDir(fixture.directory).entryList({"*.tmp"}, QDir::Files).isEmpty());
    }
    void afterCommitFailureRemainsFailedEvenWhenNewDocumentIsLoadable() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadCheckpointStore initial(fixture.directory);
        QCOMPARE(initial.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        const auto second = example("Committed but not acknowledged");
        WindowsDownloadCheckpointStore store(fixture.directory, {.inject_fault = [](const auto point) {
                                                 return point == WindowsDownloadCheckpointFaultPoint::AfterCommit;
                                             }});
        QCOMPARE(store.save(second), WindowsDownloadCheckpointSaveStatus::Failed);
        const auto loaded = store.load();
        QCOMPARE(loaded.status, WindowsDownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.checkpoint && *loaded.checkpoint == second);
        QVERIFY(QDir(fixture.directory).entryList({"*.tmp"}, QDir::Files).isEmpty());
    }
    void concurrentWriterIsRefusedWhileTheNativeLockIsHeld() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadCheckpointSaveStatus competing = WindowsDownloadCheckpointSaveStatus::Saved;
        bool observed = false;
        WindowsDownloadCheckpointStoreOptions options;
        options.inject_fault = [&](const auto point) {
            if (point == WindowsDownloadCheckpointFaultPoint::BeforeCommit) {
                observed = true;
                WindowsDownloadCheckpointStore contender(fixture.directory);
                competing = contender.save(example("must not win"));
            }
            return false;
        };
        WindowsDownloadCheckpointStore store(fixture.directory, options);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        QVERIFY(observed);
        QCOMPARE(competing, WindowsDownloadCheckpointSaveStatus::Failed);
        const auto loaded = store.load();
        QVERIFY(loaded.checkpoint && *loaded.checkpoint == example());
    }
    void actualReplaceSharingFailurePreservesOldJournalAndAllowsLaterRetry() {
        Fixture fixture;
        QVERIFY(fixture.created);
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        const auto before = fixture.bytes();
        {
            Handle reader(CreateFileW(nativePath(fixture.journal()).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            QVERIFY(reader.valid());
            QCOMPARE(store.save(example("later")), WindowsDownloadCheckpointSaveStatus::Failed);
            QCOMPARE(fixture.bytes(), before);
            QVERIFY(QDir(fixture.directory).entryList({"*.tmp"}, QDir::Files).isEmpty());
        }
        QCOMPARE(store.save(example("later")), WindowsDownloadCheckpointSaveStatus::Saved);
    }
    void unreadableExistingFileIsBlockedInsteadOfReplaced() {
        Fixture fixture;
        QVERIFY(fixture.created);
        QVERIFY(fixture.write(fixture.journal(), DownloadCheckpointJson::encode(example())));
        Handle holder(CreateFileW(nativePath(fixture.journal()).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        QVERIFY(holder.valid());
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example("not authorized")), WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
    }
    void refusesDirectoryAtJournalLeafWithoutDeletingIt() {
        Fixture fixture;
        QVERIFY(fixture.created);
        auto* attributes = fixture.security.attributes();
        QVERIFY(attributes);
        QVERIFY(CreateDirectoryW(nativePath(fixture.journal()).c_str(), attributes));
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
        QVERIFY(QFileInfo(fixture.journal()).isDir());
    }
    void refusesHardLinkedJournalAndPreservesBothOwnFixtureNames() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto document = DownloadCheckpointJson::encode(example());
        QVERIFY(!document.isEmpty());
        QVERIFY(fixture.write(fixture.journal(), document));
        const auto other = fixture.root.filePath("own-other-name.json");
        QVERIFY(CreateHardLinkW(nativePath(other).c_str(), nativePath(fixture.journal()).c_str(), nullptr));
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), document);
        QVERIFY(QFile::exists(other));
    }
    void refusesNonPrivateDirectoryWithoutRepairingItsAcl() {
        Fixture fixture(true);
        QVERIFY(fixture.created);
        const auto before = fixture.entries();
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Failed);
        QCOMPARE(fixture.entries(), before);
    }
    void refusesNonPrivateJournalWithoutRepairingOrReplacingIt() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto document = DownloadCheckpointJson::encode(example());
        QVERIFY(!document.isEmpty());
        QVERIFY(fixture.write(fixture.journal(), document, true));
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
        QCOMPARE(fixture.bytes(), document);
    }
    void neverCreatesMissingConfiguredDirectory() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto missing = root.filePath("not-created");
        WindowsDownloadCheckpointStore store(missing);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Failed);
        QVERIFY(!QFileInfo::exists(missing));
    }
    void refusesConfiguredDirectorySymlinkWithoutFollowingItsOwnTarget() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto document = DownloadCheckpointJson::encode(example());
        QVERIFY(!document.isEmpty());
        QVERIFY(fixture.write(fixture.journal(), document));
        const auto alias = fixture.root.filePath("own-config-alias");
        QVERIFY(alias.startsWith(fixture.root.path() + '/'));
        QVERIFY(fixture.directory.startsWith(fixture.root.path() + '/'));
        AliasGuard guard(fixture, alias, true);
        guard.created =
            CreateSymbolicLinkW(nativePath(alias).c_str(), nativePath(fixture.directory).c_str(),
                                SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
        if (!guard.created) {
            const auto error = GetLastError();
            if (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_ACCESS_DENIED || error == ERROR_INVALID_PARAMETER)
                QSKIP("Host policy does not permit this own-TEMP unprivileged directory symlink fixture");
            QFAIL("Unexpected failure creating own-TEMP directory symlink fixture");
        }
        const auto before = fixture.entries();
        WindowsDownloadCheckpointStore store(alias);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example("must not replace target")), WindowsDownloadCheckpointSaveStatus::Failed);
        QCOMPARE(fixture.bytes(), document);
        QCOMPARE(fixture.entries(), before);
        QVERIFY(guard.unlink());
    }
    void refusesJournalSymlinkWithoutFollowingOrReplacingItsOwnTarget() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const auto document = DownloadCheckpointJson::encode(example());
        QVERIFY(!document.isEmpty());
        const auto target = fixture.root.filePath("own-journal-target.json");
        QVERIFY(target.startsWith(fixture.root.path() + '/'));
        QVERIFY(fixture.journal().startsWith(fixture.root.path() + '/'));
        QVERIFY(fixture.write(target, document));
        AliasGuard guard(fixture, fixture.journal(), false);
        guard.created = CreateSymbolicLinkW(nativePath(fixture.journal()).c_str(), nativePath(target).c_str(),
                                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
        if (!guard.created) {
            const auto error = GetLastError();
            if (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_ACCESS_DENIED || error == ERROR_INVALID_PARAMETER)
                QSKIP("Host policy does not permit this own-TEMP unprivileged file symlink fixture");
            QFAIL("Unexpected failure creating own-TEMP file symlink fixture");
        }
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.load().status, WindowsDownloadCheckpointLoadStatus::Unreadable);
        QCOMPARE(store.save(example("must not replace alias")),
                 WindowsDownloadCheckpointSaveStatus::BlockedByExistingData);
        QFile original(target);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), document);
        original.close();
        QVERIFY(guard.unlink());
        QVERIFY(QFile::exists(target));
    }
    void pinsConfiguredDirectoryWhileCommitIsPending() {
        Fixture fixture;
        QVERIFY(fixture.created);
        bool attempted = false, renamed = false;
        WindowsDownloadCheckpointStoreOptions options;
        options.inject_fault = [&](const auto point) {
            if (point == WindowsDownloadCheckpointFaultPoint::BeforeCommit) {
                attempted = true;
                renamed = MoveFileW(nativePath(fixture.directory).c_str(),
                                    nativePath(fixture.root.filePath("renamed")).c_str()) != FALSE;
            }
            return false;
        };
        WindowsDownloadCheckpointStore store(fixture.directory, options);
        QCOMPARE(store.save(example()), WindowsDownloadCheckpointSaveStatus::Saved);
        QVERIFY(attempted);
        QVERIFY(!renamed);
        QVERIFY(QFileInfo::exists(fixture.journal()));
    }
    void realServiceIdentityAndStoreRestoreInertHistoryAndPreserveReplay() {
        Fixture fixture;
        QVERIFY(fixture.created);
        const DownloadRequest request{
            .request_id = "abcdefab-cdef-4abc-8def-000000000001",
            .items = {{.url = "https://media.example.test/audio.mp3?token=not-saved",
                       .title = "Synthetic completed audio",
                       .kind = DownloadMediaKind::Audio},
                      {.url = "https://media.example.test/video.mp4?token=not-saved",
                       .title = "Synthetic queued video",
                       .kind = DownloadMediaKind::Video}},
        };
        QtDownloadRequestIdentity identity;
        ObservedExecution original_execution;
        ObservedImport original_import;
        DownloadService original(original_execution, original_import, "old", {}, &identity);
        const auto admitted = original.submit(request);
        QCOMPARE(admitted.error, DownloadSubmissionError::None);
        original.tick(0);
        original.tick(1);
        QCOMPARE(original_execution.starts, 1);
        QCOMPARE(original_import.calls, 1);
        const auto checkpoint = original.checkpoint();
        QVERIFY(checkpoint);
        QCOMPARE(checkpoint->jobs[0].state, DownloadJobState::Completed);
        QCOMPARE(checkpoint->jobs[1].state, DownloadJobState::Queued);
        QVERIFY(!checkpoint->jobs[0].completed_mp3.empty());
        WindowsDownloadCheckpointStore store(fixture.directory);
        QCOMPARE(store.save(*checkpoint), WindowsDownloadCheckpointSaveStatus::Saved);
        QVERIFY(!fixture.bytes().contains("media.example.test"));
        QVERIFY(!fixture.bytes().contains("token=not-saved"));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, WindowsDownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.checkpoint && *loaded.checkpoint == *checkpoint);

        ObservedExecution restored_execution;
        ObservedImport restored_import;
        DownloadService restored(restored_execution, restored_import, "fresh", {}, &identity);
        QVERIFY(restored.restore(*loaded.checkpoint));
        QCOMPARE(restored.snapshot()[0].state, DownloadJobState::Completed);
        QCOMPARE(restored.snapshot()[1].state, DownloadJobState::Interrupted);
        QVERIFY(restored.snapshot()[0].completed_mp3.empty());
        QVERIFY(restored.snapshot()[1].completed_mp3.empty());
        const auto replay = restored.submit(request);
        QCOMPARE(replay.error, DownloadSubmissionError::None);
        QVERIFY(replay.job_ids == admitted.job_ids);
        auto conflict = request;
        conflict.items[0].url += "changed";
        QCOMPARE(restored.submit(conflict).error, DownloadSubmissionError::InvalidRequest);
        restored.tick(0);
        restored.tick(1000);
        QVERIFY(!restored.retryImport(admitted.job_ids[0]));
        QVERIFY(!restored.retryImport(admitted.job_ids[1]));
        QCOMPARE(restored_execution.starts, 0);
        QCOMPARE(restored_execution.polls, 0);
        QCOMPARE(restored_execution.cancels, 0);
        QCOMPARE(restored_import.calls, 0);
        const auto retained = restored.checkpoint();
        QVERIFY(retained);
        QVERIFY(retained->jobs[0].completed_mp3 == checkpoint->jobs[0].completed_mp3);
        QCOMPARE(restored.snapshot().size(), std::size_t{2});
    }
};
QTEST_GUILESS_MAIN(WindowsDownloadCheckpointStoreTests)
#include "WindowsDownloadCheckpointStoreTests.moc"
