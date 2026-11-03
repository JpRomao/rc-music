#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsDownloadRecoveryValidation.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <sddl.h>

using namespace music_player::m0;
using namespace std::chrono_literals;
using Outcome = DownloadRecoveryValidationOutcome;
using Command = DownloadRecoveryValidationCommand;
namespace {
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{false}, released{false}, expired{false};
    void enter() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        expired = !changed.wait_for(lock, 5s, [&] { return released; });
    }
    bool wait() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] { return entered; });
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
    bool blocked() {
        std::lock_guard lock(mutex);
        return entered && !released && !expired;
    }
};
struct Control {
    std::mutex mutex;
    std::optional<Outcome> outcome{};
    Gate factory_gate, start_gate, poll_gate, cancel_gate, destroy_gate, capture_gate;
    bool block_factory{false}, block_start{false}, block_poll{false}, block_cancel{false}, block_destroy{false};
    bool block_capture{false}, start_result{true};
    std::atomic_bool throw_start{false}, throw_poll{false}, throw_cancel{false};
    std::atomic_int factories{0}, starts{0}, polls{0}, cancels{0}, destroyed{0}, capture_destroyed{0};
    std::atomic_int first_poll_cancels{-1};
    std::thread::id factory_thread{}, start_thread{}, destroy_thread{}, capture_thread{};
    void publish(std::optional<Outcome> value) {
        std::lock_guard lock(mutex);
        outcome = value;
    }
    void releaseAll() {
        factory_gate.release();
        start_gate.release();
        poll_gate.release();
        cancel_gate.release();
        destroy_gate.release();
        capture_gate.release();
    }
};
class Task final : public WindowsDownloadRecoveryValidationTask {
  public:
    explicit Task(std::shared_ptr<Control> state) : c(std::move(state)) {}
    ~Task() override {
        if (c->block_destroy)
            c->destroy_gate.enter();
        c->destroy_thread = std::this_thread::get_id();
        ++c->destroyed;
    }
    bool start(const std::string&, qint64) override {
        c->start_thread = std::this_thread::get_id();
        ++c->starts;
        if (c->block_start)
            c->start_gate.enter();
        if (c->throw_start)
            throw std::runtime_error("private detail");
        return c->start_result;
    }
    void cancel() override {
        ++c->cancels;
        if (c->block_cancel)
            c->cancel_gate.enter();
        if (c->throw_cancel)
            throw std::runtime_error("not stopped");
    }
    std::optional<Outcome> poll(qint64) override {
        if (c->polls.fetch_add(1) == 0)
            c->first_poll_cancels = c->cancels.load();
        if (c->block_poll)
            c->poll_gate.enter();
        if (c->throw_poll)
            throw std::runtime_error("not stopped");
        std::lock_guard lock(c->mutex);
        return c->outcome;
    }

  private:
    std::shared_ptr<Control> c;
};
WindowsDownloadRecoveryValidation::TaskFactory factory(const std::shared_ptr<Control>& c) {
    return [c] {
        c->factory_thread = std::this_thread::get_id();
        ++c->factories;
        if (c->block_factory)
            c->factory_gate.enter();
        return std::make_unique<Task>(c);
    };
}
struct Finish {
    std::shared_ptr<Control> c;
    WindowsDownloadRecoveryValidation& owner;
    ~Finish() {
        c->throw_start = c->throw_poll = c->throw_cancel = false;
        c->publish(Outcome::Released);
        c->releaseAll();
        owner.shutdown();
    }
};
struct FactoryCapture {
    std::shared_ptr<Control> c;
    explicit FactoryCapture(std::shared_ptr<Control> value) : c(std::move(value)) {}
    ~FactoryCapture() {
        c->capture_thread = std::this_thread::get_id();
        if (c->block_capture)
            c->capture_gate.enter();
        ++c->capture_destroyed;
    }
};
bool complete(WindowsDownloadRecoveryValidation& owner, std::uint64_t id,
              std::optional<DownloadRecoveryValidationResult>& result) {
    if (!result)
        result = owner.poll(id);
    return result.has_value();
}
bool privateObject(const QString& path, bool directory, const QByteArray& bytes = {}) {
    using download_artifacts::Handle;
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> user(size);
    if (!size || !GetTokenInformation(token.get(), TokenUser, user.data(), size, &size))
        return false;
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &text))
        return false;
    const auto free_text = qScopeGuard([&] { LocalFree(text); });
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(text) + ')';
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    if (directory)
        return CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != 0;
    Handle file(CreateFileW(download_artifacts::nativePath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD written = 0;
    return file.valid() &&
           WriteFile(file.get(), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
           written == static_cast<DWORD>(bytes.size());
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
#ifdef RC_MUSIC_TEST_FFMPEG_PATH
class Store final : public DownloadOwnershipStore {
  public:
    WindowsDownloadOwnershipStore real;
    bool refuse_published{false};
    explicit Store(QString directory) : real(std::move(directory)) {}
    DownloadOwnershipLoadResult load() override { return real.load(); }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& record) override {
        return refuse_published && record.stage == DownloadOwnershipStage::Published
                   ? DownloadCheckpointSaveStatus::Failed
                   : real.record(record);
    }
};
bool writable(const QString& path) {
    download_artifacts::Handle writer(CreateFileW(download_artifacts::nativePath(path).c_str(), GENERIC_WRITE,
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    return writer.valid();
}
#endif
} // namespace

class WindowsDownloadRecoveryValidationTests final : public QObject {
    Q_OBJECT
  private slots:
    void validationRetainsItsSlotAndCapabilityUntilMatchingReleaseAcknowledgement() {
        auto c = std::make_shared<Control>();
        c->publish(Outcome::Validated);
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        QTRY_VERIFY(owner.poll(99) == std::nullopt && c->polls > 0);
        QCOMPARE(owner.startValidation(9, "job-9"), Command::Busy);
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->operation_id, std::uint64_t{1});
        QCOMPARE(result->job_id, std::string{"job-1"});
        QCOMPARE(result->outcome, Outcome::Validated);
        QCOMPARE(c->destroyed.load(), 0);
        QVERIFY(!owner.poll(1));
        QCOMPARE(owner.startValidation(2, "job-2"), Command::Busy);
        QVERIFY(!owner.release(2));
        QVERIFY(owner.release(1));
        QVERIFY(owner.release(1));
        result.reset();
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Released);
        QCOMPARE(c->destroyed.load(), 1);
        QVERIFY(!owner.release(1));
        QCOMPARE(owner.startValidation(1, "job-2"), Command::InvalidRequest);
        c->publish(Outcome::InvalidMedia);
        QCOMPARE(owner.startValidation(2, "job-2"), Command::Accepted);
    }
    void errorsDestroyTaskBeforeMatchingPollCanFreeAdmission_data() {
        QTest::addColumn<int>("outcome");
        for (const auto value : {Outcome::Unverified, Outcome::OwnedPartial, Outcome::Missing, Outcome::Conflict,
                                 Outcome::Unreadable, Outcome::HistoryMissing, Outcome::HistoryInvalid,
                                 Outcome::HistoryFuture, Outcome::InvalidMedia, Outcome::Timeout})
            QTest::newRow(qPrintable(QString::number(static_cast<int>(value)))) << static_cast<int>(value);
    }
    void errorsDestroyTaskBeforeMatchingPollCanFreeAdmission() {
        QFETCH(int, outcome);
        auto c = std::make_shared<Control>();
        c->publish(static_cast<Outcome>(outcome));
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        QTRY_VERIFY(c->destroyed == 1);
        QCOMPARE(owner.startValidation(99, "job-99"), Command::Busy);
        QVERIFY(!owner.poll(2));
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, static_cast<Outcome>(outcome));
        QCOMPARE(owner.startValidation(2, "job-2"), Command::Accepted);
    }
    void slowOperationsNeverRunInsideFastMethods_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("factory") << 0;
        QTest::newRow("start") << 1;
        QTest::newRow("poll") << 2;
        QTest::newRow("cancel") << 3;
        QTest::newRow("destroy") << 4;
    }
    void slowOperationsNeverRunInsideFastMethods() {
        QFETCH(int, operation);
        auto c = std::make_shared<Control>();
        c->block_factory = operation == 0;
        c->block_start = operation == 1;
        c->block_poll = operation == 2;
        c->block_cancel = operation == 3;
        c->block_destroy = operation == 4;
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        if (operation >= 3) {
            QTRY_VERIFY(c->polls > 0);
            if (operation == 3)
                QVERIFY(owner.release(1));
            else
                c->publish(Outcome::InvalidMedia);
        }
        Gate* gates[]{&c->factory_gate, &c->start_gate, &c->poll_gate, &c->cancel_gate, &c->destroy_gate};
        QVERIFY(gates[operation]->wait());
        QVERIFY(!owner.poll(1));
        QCOMPARE(owner.startValidation(2, "job-2"), Command::Busy);
        QVERIFY(!owner.release(2));
        QVERIFY(owner.release(1));
        owner.shutdown();
        QVERIFY(!owner.stopped());
        QCOMPARE(owner.startValidation(2, "job-2"), Command::Closed);
        QVERIFY(gates[operation]->blocked());
        c->publish(Outcome::Released);
        c->releaseAll();
        QTRY_VERIFY(owner.stopped());
        QVERIFY(c->factory_thread != std::this_thread::get_id());
        if (operation != 0)
            QVERIFY(c->destroy_thread != std::this_thread::get_id());
    }
    void invalidRequestsAndClosedOwnerNeverRunFactory() {
        auto c = std::make_shared<Control>();
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(0, "job-1"), Command::InvalidRequest);
        for (const auto& id : {std::string{}, std::string(65, 'a'), std::string{"../job"}, std::string{"x\0y", 3}})
            QCOMPARE(owner.startValidation(100, id), Command::InvalidRequest);
        QVERIFY(!owner.poll(1));
        QVERIFY(!owner.release(1));
        QCOMPARE(c->factories.load(), 0);
        owner.shutdown();
        QTRY_VERIFY(owner.stopped());
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Closed);
        QCOMPARE(c->factories.load(), 0);
    }
    void releaseAfterBlockedStartRunsCancellationBeforeFirstPoll() {
        auto c = std::make_shared<Control>();
        c->block_start = true;
        c->publish(Outcome::Released);
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        QVERIFY(c->start_gate.wait());
        QVERIFY(owner.release(1));
        QVERIFY(!owner.poll(1));
        c->start_gate.release();
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Released);
        QVERIFY(c->cancels > 0);
        QVERIFY(c->first_poll_cancels > 0);
        QCOMPARE(c->destroyed.load(), 1);
    }
    void uncertainTaskCallsNeedPhysicalAcknowledgement_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("start-throw") << 0;
        QTest::newRow("poll-throw") << 1;
        QTest::newRow("cancel-throw") << 2;
        QTest::newRow("start-false") << 3;
        QTest::newRow("unknown-outcome") << 4;
    }
    void uncertainTaskCallsNeedPhysicalAcknowledgement() {
        QFETCH(int, operation);
        auto c = std::make_shared<Control>();
        c->throw_start = operation == 0;
        c->throw_poll = operation == 1;
        c->throw_cancel = operation == 2;
        c->start_result = operation != 3;
        if (operation == 4)
            c->publish(static_cast<Outcome>(999));
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        QTRY_VERIFY(c->starts > 0);
        if (operation == 2)
            QVERIFY(owner.release(1));
        QTRY_VERIFY(c->cancels > 0);
        QVERIFY(!owner.poll(1));
        QCOMPARE(c->destroyed.load(), 0);
        QCOMPARE(owner.startValidation(2, "job-2"), Command::Busy);
        c->throw_start = c->throw_poll = c->throw_cancel = false;
        c->publish(Outcome::Released);
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Unreadable);
        QCOMPARE(c->destroyed.load(), 1);
    }
    void releaseRevokesAnUnobservedValidatedCompletion() {
        auto c = std::make_shared<Control>();
        c->publish(Outcome::Validated);
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        QTRY_VERIFY(c->polls > 0);
        QVERIFY(owner.release(1));
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Released);
        QCOMPARE(c->destroyed.load(), 1);
    }
    void stoppedAcknowledgesFactoryCaptureDestructionOnTheWorker() {
        auto c = std::make_shared<Control>();
        c->block_capture = true;
        auto capture = std::make_shared<FactoryCapture>(c);
        WindowsDownloadRecoveryValidation owner([capture, c] { return std::make_unique<Task>(c); });
        Finish finish{c, owner};
        capture.reset();
        owner.shutdown();
        QVERIFY(c->capture_gate.wait());
        QVERIFY(!owner.stopped());
        QVERIFY(c->capture_gate.blocked());
        c->capture_gate.release();
        QTRY_VERIFY(owner.stopped());
        QCOMPARE(c->capture_destroyed.load(), 1);
        QVERIFY(c->capture_thread != std::this_thread::get_id());
    }
    void rejectedIdsNeverAdvanceTheAcceptedCounterAndMaximumNeverWraps() {
        auto c = std::make_shared<Control>();
        c->publish(Outcome::InvalidMedia);
        WindowsDownloadRecoveryValidation owner(factory(c));
        Finish finish{c, owner};
        QCOMPARE(owner.startValidation(100, "bad/job"), Command::InvalidRequest);
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        QCOMPARE(owner.startValidation(maximum, "job-max"), Command::Accepted);
        result.reset();
        QTRY_VERIFY(complete(owner, maximum, result));
        QCOMPARE(owner.startValidation(maximum, "job-max"), Command::InvalidRequest);
        QCOMPARE(owner.startValidation(maximum - 1, "job-old"), Command::InvalidRequest);
        QCOMPARE(owner.startValidation(0, "job-zero"), Command::InvalidRequest);
    }
    void nullOrThrowingInertFactoryReturnsUnreadable_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("null") << false;
        QTest::newRow("throw") << true;
    }
    void nullOrThrowingInertFactoryReturnsUnreadable() {
        QFETCH(bool, throws);
        WindowsDownloadRecoveryValidation owner([throws]() -> std::unique_ptr<WindowsDownloadRecoveryValidationTask> {
            if (throws)
                throw std::runtime_error("inert construction");
            return {};
        });
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Unreadable);
        owner.shutdown();
        QTRY_VERIFY(owner.stopped());
    }
    void defaultFactoryReportsOnlyTrustedExactJournalEvidence_data() {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<int>("outcome");
        QTest::newRow("missing") << QByteArray{} << static_cast<int>(Outcome::HistoryMissing);
        QTest::newRow("invalid") << QByteArray{"not json"} << static_cast<int>(Outcome::HistoryInvalid);
        QTest::newRow("future") << QByteArray{"{\"schemaVersion\":999,\"owner\":\"rc-music-download-ownership\"}"}
                                << static_cast<int>(Outcome::HistoryFuture);
        QTest::newRow("exact job absent")
            << QByteArray{"{\"schemaVersion\":1,\"owner\":\"rc-music-download-ownership\",\"records\":[]}"}
            << static_cast<int>(Outcome::Unverified);
    }
    void defaultFactoryReportsOnlyTrustedExactJournalEvidence() {
        QFETCH(QByteArray, document);
        QFETCH(int, outcome);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto journal = directory.filePath("private-journal");
        QVERIFY(privateObject(journal, true));
        const auto path = journal + "/download-ownership.json";
        if (!document.isEmpty())
            QVERIFY(privateObject(path, false, document));
        WindowsDownloadRecoveryValidation owner(journal, {}, {});
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, static_cast<Outcome>(outcome));
        QCOMPARE(read(path), document);
        owner.shutdown();
        QTRY_VERIFY(owner.stopped());
    }
#ifdef RC_MUSIC_TEST_FFMPEG_PATH
    void defaultFactoryValidatesRealMediaAndRetainsPinsWithoutSavingPublication_data() {
        QTest::addColumn<bool>("published");
        QTest::newRow("published") << true;
        QTest::newRow("prepared without acknowledgement") << false;
    }
    void defaultFactoryValidatesRealMediaAndRetainsPinsWithoutSavingPublication() {
        QFETCH(bool, published);
        const auto bytes = read(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + "/mp3-unicode-tags.mp3");
        QCOMPARE(bytes.size(), 24966);
        QCOMPARE(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
                 QByteArray{"65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"});
        QTemporaryDir directory(QDir::tempPath() + "/RC Music validation worker 日本-XXXXXX");
        QVERIFY(directory.isValid());
        const auto journal = directory.filePath("journal");
        const auto staging = directory.filePath("staging");
        const auto publication = directory.filePath("published");
        QVERIFY(privateObject(journal, true));
        QVERIFY(QDir().mkdir(staging));
        QVERIFY(QDir().mkdir(publication));
        WindowsDownloadWorkspaceConfig workspace_config{staging, publication, 1024 * 1024};
        Store store(journal);
        store.refuse_published = !published;
        QString final;
        {
            WindowsDownloadWorkspace workspace(workspace_config, "job-1", store);
            QVERIFY(workspace.create());
            QCOMPARE(workspace.inputSink()->write(bytes), bytes.size());
            QVERIFY(workspace.sealInput());
            QCOMPARE(workspace.outputSink()->write(bytes), bytes.size());
            QVERIFY(workspace.sealOutput());
            final = workspace.validateAndPublish([](const QString&) { return true; });
            if (!published)
                final = workspace.retainedPublicationPath();
            QVERIFY(!final.isEmpty());
        }
        const auto journal_bytes = read(journal + "/download-ownership.json");
        WindowsLocalMp3ValidatorConfig tools{QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH),
                                             QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
        WindowsDownloadRecoveryValidation owner(journal, workspace_config, tools);
        QCOMPARE(owner.startValidation(1, "job-1"), Command::Accepted);
        std::optional<DownloadRecoveryValidationResult> result;
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Validated);
        QVERIFY(!writable(final));
        QCOMPARE(read(final), bytes);
        QCOMPARE(read(journal + "/download-ownership.json"), journal_bytes);
        QVERIFY(owner.release(1));
        result.reset();
        QTRY_VERIFY(complete(owner, 1, result));
        QCOMPARE(result->outcome, Outcome::Released);
        QVERIFY(writable(final));
        QCOMPARE(read(final), bytes);
        QCOMPARE(read(journal + "/download-ownership.json"), journal_bytes);
        owner.shutdown();
        QTRY_VERIFY(owner.stopped());
    }
#endif
};
QTEST_GUILESS_MAIN(WindowsDownloadRecoveryValidationTests)
#include "WindowsDownloadRecoveryValidationTests.moc"
