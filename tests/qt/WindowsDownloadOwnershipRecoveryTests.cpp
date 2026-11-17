#include "adapters/DownloadOwnershipJson.hpp"
#include "adapters/WindowsDownloadOwnershipRecovery.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsDownloadReadInput.hpp"
#include "adapters/WindowsDownloadRecoveryReview.hpp"
#include "m0/ports/DownloadExecution.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <AclAPI.h>
#include <Windows.h>
#include <sddl.h>

namespace music_player::m0 {
namespace {
constexpr auto uuid = "abcdefab-cdef-4abc-8def-000000000001";
constexpr auto input_content = "input";
constexpr auto output_content = "synthetic-mp3";

std::wstring native(const QString& path) { return QDir::toNativeSeparators(path).toStdWString(); }

class Handle final {
  public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~Handle() {
        if (valid())
            CloseHandle(handle_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }

  private:
    HANDLE handle_;
};

std::optional<DownloadFileIdentity> identity(const QString& path, bool directory) {
    Handle handle(CreateFileW(native(path).c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    FILE_ID_INFO id{};
    if (!handle.valid() || !GetFileInformationByHandleEx(handle.get(), FileIdInfo, &id, sizeof(id)))
        return std::nullopt;
    DownloadFileIdentity result{.volume_serial = id.VolumeSerialNumber};
    std::memcpy(result.file_id.data(), id.FileId.Identifier, result.file_id.size());
    return result;
}

class PrivateSecurity final {
  public:
    explicit PrivateSecurity(bool inheritance = true) {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return;
        Handle token(raw);
        DWORD required = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
        if (!required)
            return;
        std::vector<std::byte> bytes(required);
        if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), required, &required))
            return;
        LPWSTR raw_sid = nullptr;
        const auto* user = reinterpret_cast<const TOKEN_USER*>(bytes.data());
        if (!ConvertSidToStringSidW(user->User.Sid, &raw_sid))
            return;
        const auto sid = QString::fromWCharArray(raw_sid);
        LocalFree(raw_sid);
        const auto flags = inheritance ? QStringLiteral("OICI") : QString{};
        const auto sddl = QStringLiteral("O:%1D:P(A;%2;FA;;;%1)(A;%2;FA;;;SY)").arg(sid, flags);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1,
                                                                  &descriptor_, nullptr))
            return;
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
    }
    ~PrivateSecurity() { LocalFree(descriptor_); }
    bool valid() const { return descriptor_ != nullptr; }
    SECURITY_ATTRIBUTES* attributes() { return &attributes_; }
    PSECURITY_DESCRIPTOR descriptor() const { return descriptor_; }

  private:
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    SECURITY_ATTRIBUTES attributes_{};
};

bool createFile(const QString& path, const QByteArray& content, PrivateSecurity& security) {
    Handle handle(CreateFileW(native(path).c_str(), GENERIC_READ | GENERIC_WRITE, 0, security.attributes(), CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    DWORD written = 0;
    return handle.valid() &&
           WriteFile(handle.get(), content.constData(), static_cast<DWORD>(content.size()), &written, nullptr) &&
           written == static_cast<DWORD>(content.size()) && FlushFileBuffers(handle.get());
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QByteArray readCapability(const std::shared_ptr<const WindowsDownloadReadInput>& input, const DWORD wanted = 64) {
    if (!input)
        return {};
    QByteArray bytes(static_cast<qsizetype>(wanted), '\0');
    DWORD count = 0;
    if (!ReadFile(reinterpret_cast<HANDLE>(input->handleValue()), bytes.data(), wanted, &count, nullptr))
        return {};
    bytes.resize(static_cast<qsizetype>(count));
    return bytes;
}

bool changeBytes(const QString& path, const QByteArray& value) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(value) == value.size();
}

bool replaceDacl(const QString& path, bool directory, PrivateSecurity& security, bool protected_dacl = true) {
    Handle handle(CreateFileW(native(path).c_str(), WRITE_DAC | READ_CONTROL | FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    PACL dacl = nullptr;
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    if (!handle.valid())
        return false;
    if (!GetSecurityDescriptorDacl(security.descriptor(), &present, &dacl, &defaulted) || !present)
        return false;
    const auto status =
        SetSecurityInfo(handle.get(), SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION | (protected_dacl ? PROTECTED_DACL_SECURITY_INFORMATION
                                                                    : UNPROTECTED_DACL_SECURITY_INFORMATION),
                        nullptr, nullptr, dacl, nullptr);
    return status == ERROR_SUCCESS;
}

struct Fixture final {
    QTemporaryDir root{};
    PrivateSecurity security{};
    QString staging = root.filePath(QStringLiteral("staging"));
    QString publication = root.filePath(QStringLiteral("publication"));
    QString job = staging + QStringLiteral("/rc-music-job-") + QString::fromLatin1(uuid);
    QString input = job + QStringLiteral("/input.bin");
    QString output = job + QStringLiteral("/converted.part");
    QString final = publication + QStringLiteral("/rc-music-") + QString::fromLatin1(uuid) + QStringLiteral(".mp3");
    DownloadOwnershipRecord record{.job_id = "synthetic-1", .workspace_id = uuid, .maximum_workspace_bytes = 64};
    bool initialized{false};

    Fixture() {
        if (!root.isValid() || !security.valid() || !QDir().mkdir(staging) || !QDir().mkdir(publication))
            return;
        const auto source = identity(staging, true);
        const auto destination = identity(publication, true);
        if (!source || !destination)
            return;
        record.staging_parent = *source;
        record.publish_parent = *destination;
        initialized = true;
    }
    WindowsDownloadWorkspaceConfig config(std::uint64_t limit = 64) const { return {staging, publication, limit}; }
    bool ready(const QByteArray& in = input_content, const QByteArray& out = output_content) {
        if (!initialized || !CreateDirectoryW(native(job).c_str(), security.attributes()) ||
            !createFile(input, in, security) || !createFile(output, out, security))
            return false;
        record.job_directory = identity(job, true);
        record.input = identity(input, false);
        record.output = identity(output, false);
        record.stage = DownloadOwnershipStage::WorkspaceReady;
        return record.job_directory && record.input && record.output;
    }
    void prepared() {
        record.stage = DownloadOwnershipStage::PublicationPrepared;
        record.input_bytes = std::strlen(input_content);
        record.output_bytes = std::strlen(output_content);
        record.output_sha256 =
            QCryptographicHash::hash(output_content, QCryptographicHash::Sha256).toHex().toStdString();
    }
    bool publish() {
        prepared();
        if (!MoveFileExW(native(output).c_str(), native(final).c_str(), 0))
            return false;
        record.stage = DownloadOwnershipStage::Published;
        return true;
    }
    bool removeTemps() {
        return DeleteFileW(native(input).c_str()) && (!QFile::exists(output) || DeleteFileW(native(output).c_str())) &&
               RemoveDirectoryW(native(job).c_str());
    }
    DownloadOwnershipAssessment assess() const { return WindowsDownloadOwnershipRecovery::assess(config(), record); }
};

// Aliases are unlinked directly before Qt TEMP's own recursive destruction.
struct RemoveDirectoryAlias final {
    QString path;
    ~RemoveDirectoryAlias() {
        if (!path.isEmpty())
            RemoveDirectoryW(native(path).c_str());
    }
};

class RecordingStore final : public DownloadOwnershipStore {
  public:
    DownloadCheckpointSaveStatus status{DownloadCheckpointSaveStatus::Saved};
    bool throw_on_record{false};
    bool commit_before_failure{false};
    int load_calls{0};
    std::vector<DownloadOwnershipRecord> attempts{};
    std::optional<DownloadOwnershipRecord> committed{};
    std::function<void()> before_record{};

    DownloadOwnershipLoadResult load() override {
        ++load_calls;
        return committed ? DownloadOwnershipLoadResult{DownloadCheckpointLoadStatus::Loaded,
                                                       DownloadOwnershipJournal{{*committed}}}
                         : DownloadOwnershipLoadResult{};
    }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& value) override {
        attempts.push_back(value);
        if (before_record)
            before_record();
        if (status == DownloadCheckpointSaveStatus::Saved || commit_before_failure)
            committed = value;
        if (throw_on_record)
            throw std::runtime_error("synthetic save failure");
        return status;
    }
};

struct RestoreAttributes final {
    QString path;
    ~RestoreAttributes() { SetFileAttributesW(native(path).c_str(), FILE_ATTRIBUTE_NORMAL); }
};

DownloadOwnershipJournal journalWith(const DownloadOwnershipRecord& record,
                                     const std::size_t count = DownloadOwnershipJournal::maximum_records) {
    DownloadOwnershipJournal journal{{record}};
    for (std::size_t index = 1; index < count; ++index) {
        auto suffix = std::to_string(index);
        suffix.insert(0, 12 - suffix.size(), '0');
        journal.records.push_back({.job_id = "unrelated-" + std::to_string(index),
                                   .workspace_id = "feedface-cdef-4abc-8def-" + suffix,
                                   .staging_parent = record.staging_parent,
                                   .publish_parent = record.publish_parent});
    }
    return journal;
}

QString seedJournal(const Fixture& fixture, const QByteArray& contents) {
    PrivateSecurity security(false);
    const auto directory = fixture.root.filePath(QStringLiteral("private-reclaim-journal"));
    if (!security.valid() || !CreateDirectoryW(native(directory).c_str(), security.attributes()) ||
        !createFile(directory + QStringLiteral("/download-ownership.json"), contents, security))
        return {};
    return directory;
}
} // namespace

class WindowsDownloadOwnershipRecoveryTests final : public QObject {
    Q_OBJECT

  private slots:
    void reviewOffersCleanupOnlyForProvenCleanAbsence_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<DownloadRecoveryReviewOutcome>("expected");
        using Outcome = DownloadRecoveryReviewOutcome;
        QTest::newRow("clean-planned") << 0 << Outcome::PlannedWithoutFiles;
        QTest::newRow("clean-ready") << 1 << Outcome::CleanupCandidate;
        QTest::newRow("unconfirmed-missing") << 2 << Outcome::Missing;
        QTest::newRow("clean-with-residual") << 3 << Outcome::Conflict;
        QTest::newRow("clean-with-final") << 4 << Outcome::OwnedFinalCandidate;
        QTest::newRow("wrong-roots") << 5 << Outcome::Conflict;
    }

    void reviewOffersCleanupOnlyForProvenCleanAbsence() {
        QFETCH(int, kind);
        QFETCH(DownloadRecoveryReviewOutcome, expected);
        Fixture f;
        QVERIFY(f.initialized);
        if (kind != 0) {
            QVERIFY(f.ready());
            if (kind == 4)
                QVERIFY(f.publish());
            if (kind != 3)
                QVERIFY(f.removeTemps());
        }
        f.record.cleanup_complete = kind != 2;
        auto config = f.config();
        if (kind == 5)
            config.publish_parent = config.staging_parent;
        const auto before = DownloadOwnershipJson::encode({{f.record}});
        const auto directory = seedJournal(f, before);
        QVERIFY(!directory.isEmpty());
        WindowsDownloadRecoveryReview review(directory, config);
        QCOMPARE(review.startReview(1, f.record.job_id), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->action, DownloadRecoveryReviewAction::ReviewFiles);
        QCOMPARE(result->outcome, expected);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), before);
        review.shutdown();
        QTRY_VERIFY(review.stopped());
    }

    void cleanedAbsentDiscardRequiresNewExactAcknowledgement_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("new-ack-after-ambiguous-commit") << 0;
        QTest::newRow("journal-row-removed") << 1;
        QTest::newRow("journal-row-changed") << 2;
    }

    void cleanedAbsentDiscardRequiresNewExactAcknowledgement() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialized);
        f.record.cleanup_complete = true;
        auto before = DownloadOwnershipJournal{{f.record}};
        if (kind == 1)
            before.records.clear();
        if (kind == 2)
            before.records.front().cleanup_complete = false;
        const auto encoded = DownloadOwnershipJson::encode(before);
        const auto directory = seedJournal(f, encoded);
        QVERIFY(!directory.isEmpty());
        bool failed_after_commit = false;
        WindowsDownloadOwnershipStore faulted(directory, {.inject_fault = [&](const auto point) {
                                                  failed_after_commit =
                                                      point == WindowsDownloadOwnershipFaultPoint::AfterCommit;
                                                  return failed_after_commit;
                                              }});
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, faulted),
                 DownloadOwnershipDiscardOutcome::Unconfirmed);
        QCOMPARE(failed_after_commit, kind == 0);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), encoded);
        if (kind != 0)
            return;

        bool saw_pinned_parent = false;
        WindowsDownloadOwnershipStore retried(
            directory, {.inject_fault = [&](const auto point) {
                if (point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) {
                    Handle replaceable(CreateFileW(native(f.staging).c_str(), DELETE,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
                    saw_pinned_parent = !replaceable.valid();
                }
                return false;
            }});
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, retried),
                 DownloadOwnershipDiscardOutcome::Discarded);
        QVERIFY(saw_pinned_parent);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), encoded);
    }

    void plannedAbsenceConfirmationMarksTheExactRowWithoutDeletingFiles_data() {
        QTest::addColumn<bool>("ambiguous_commit");
        QTest::newRow("saved") << false;
        QTest::newRow("failure-after-commit-needs-new-acknowledgement") << true;
    }

    void plannedAbsenceConfirmationMarksTheExactRowWithoutDeletingFiles() {
        QFETCH(bool, ambiguous_commit);
        Fixture f;
        QVERIFY(f.initialized);
        const auto sentinel = f.staging + QStringLiteral("/keep-sibling.bin");
        QVERIFY(createFile(sentinel, "keep-sibling", f.security));
        const auto directory = seedJournal(f, DownloadOwnershipJson::encode({{f.record}}));
        QVERIFY(!directory.isEmpty());
        bool saw_pinned_parent = false;
        WindowsDownloadOwnershipStore store(
            directory, {.inject_fault = [&](const auto point) {
                if (point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) {
                    Handle replaceable(CreateFileW(native(f.staging).c_str(), DELETE,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
                    saw_pinned_parent = !replaceable.valid();
                }
                return ambiguous_commit && point == WindowsDownloadOwnershipFaultPoint::AfterCommit;
            }});
        using Outcome = DownloadOwnershipDiscardOutcome;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store), Outcome::Unverified);
        QVERIFY(WindowsDownloadOwnershipRecovery::plannedWithoutFiles(f.config(), f.record));
        QCOMPARE(WindowsDownloadOwnershipRecovery::confirmPlannedWithoutFiles(f.config(), f.record, store),
                 ambiguous_commit ? Outcome::Unconfirmed : Outcome::Discarded);
        QVERIFY(saw_pinned_parent);
        QVERIFY(!QFile::exists(f.job));
        QVERIFY(!QFile::exists(f.final));
        QCOMPARE(readFile(sentinel), QByteArray("keep-sibling"));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.journal);
        auto cleaned = f.record;
        cleaned.cleanup_complete = true;
        QCOMPARE(loaded.journal->records, std::vector{cleaned});
        if (ambiguous_commit) {
            WindowsDownloadOwnershipStore retry(directory);
            QVERIFY(WindowsDownloadOwnershipRecovery::plannedWithoutFiles(f.config(), cleaned));
            QCOMPARE(WindowsDownloadOwnershipRecovery::confirmPlannedWithoutFiles(f.config(), cleaned, retry),
                     Outcome::Discarded);
        }
    }

    void plannedAbsenceConfirmationRefusesObjectsOrChangedHistory_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("unowned-temporaries") << 0;
        QTest::newRow("unowned-final") << 1;
        QTest::newRow("journal-row-removed") << 2;
        QTest::newRow("journal-row-changed") << 3;
        QTest::newRow("advanced-stage-with-owned-files") << 4;
    }

    void plannedAbsenceConfirmationRefusesObjectsOrChangedHistory() {
        QFETCH(int, scenario);
        Fixture f;
        QVERIFY(f.initialized);
        const auto planned = f.record;
        if (scenario == 0 || scenario == 4)
            QVERIFY(f.ready());
        if (scenario == 1)
            QVERIFY(createFile(f.final, output_content, f.security));
        const auto expected = scenario == 4 ? f.record : planned;
        auto journal = DownloadOwnershipJournal{{expected}};
        if (scenario == 2)
            journal.records.clear();
        if (scenario == 3)
            ++journal.records.front().maximum_workspace_bytes;
        const auto before = DownloadOwnershipJson::encode(journal);
        const auto directory = seedJournal(f, before);
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        using Outcome = DownloadOwnershipDiscardOutcome;
        const bool files_absent = scenario == 2 || scenario == 3;
        QCOMPARE(WindowsDownloadOwnershipRecovery::plannedWithoutFiles(f.config(), expected), files_absent);
        QCOMPARE(WindowsDownloadOwnershipRecovery::confirmPlannedWithoutFiles(f.config(), expected, store),
                 files_absent ? Outcome::Unconfirmed : Outcome::Unverified);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), before);
        if (scenario == 0 || scenario == 4) {
            QCOMPARE(readFile(f.input), QByteArray(input_content));
            QCOMPARE(readFile(f.output), QByteArray(output_content));
        }
        if (scenario == 1)
            QCOMPARE(readFile(f.final), QByteArray(output_content));
    }

    void fullJournalReclaimsOnePublishedRowAndKeepsThePhysicalFile_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("saved") << 0;
        QTest::newRow("before-commit") << 1;
        QTest::newRow("after-commit") << 2;
    }
    void fullJournalReclaimsOnePublishedRowAndKeepsThePhysicalFile() {
        QFETCH(int, fault);
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        QVERIFY(f.removeTemps());
        f.record.cleanup_complete = true;
        // The file remains user media. Reclaim compares exact identity and size
        // without decoding or hashing its contents again.
        const QByteArray preserved(static_cast<qsizetype>(f.record.output_bytes), 'x');
        QVERIFY(changeBytes(f.final, preserved));
        auto before = journalWith(f.record);
        before.records.back().cleanup_complete = true;
        const auto directory = seedJournal(f, DownloadOwnershipJson::encode(before));
        QVERIFY(!directory.isEmpty());
        bool saw_pinned_final = false;
        WindowsDownloadOwnershipStore store(
            directory, {.inject_fault = [&](const auto point) {
                if (point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) {
                    Handle writable(CreateFileW(native(f.final).c_str(), GENERIC_WRITE,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
                    saw_pinned_final = !writable.valid();
                    return fault == 1;
                }
                return fault == 2;
            }});
        const std::array ids{f.record.job_id, before.records.back().job_id};
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(f.config(), store, {ids, ids}),
                 fault == 0 ? DownloadOwnershipCapacity::Available : DownloadOwnershipCapacity::ReclaimUnconfirmed);
        QVERIFY(saw_pinned_final);
        const auto loaded = store.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.journal);
        auto expected = before;
        if (fault != 1)
            expected.records.erase(expected.records.begin());
        QCOMPARE(*loaded.journal, expected);
        QCOMPARE(readFile(f.final), preserved);
        QVERIFY(!QFile::exists(f.job));
    }

    void fullJournalPreservesUnqualifiedRowsAndPhysicalObjects_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("not-archived") << 0;
        QTest::newRow("not-retired-in-session") << 1;
        QTest::newRow("cleanup-not-confirmed") << 2;
        QTest::newRow("prepared-final") << 3;
        QTest::newRow("changed-parent") << 4;
        QTest::newRow("wrong-final-size") << 5;
        QTest::newRow("staging-residual") << 6;
        QTest::newRow("wrong-final-identity") << 7;
    }
    void fullJournalPreservesUnqualifiedRowsAndPhysicalObjects() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        if (kind != 6)
            QVERIFY(f.removeTemps());
        f.record.cleanup_complete = kind != 2;
        if (kind == 3)
            f.record.stage = DownloadOwnershipStage::PublicationPrepared;
        auto config = f.config();
        if (kind == 4)
            config.publish_parent = config.staging_parent;
        if (kind == 5)
            QVERIFY(changeBytes(f.final, "other-size"));
        if (kind == 7) {
            auto& first_byte = f.record.output->file_id[0];
            first_byte = first_byte == 1 ? 2 : 1;
        }
        const auto final_bytes = readFile(f.final);
        const auto before = DownloadOwnershipJson::encode(journalWith(f.record));
        const auto directory = seedJournal(f, before);
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        const std::array ids{f.record.job_id};
        const std::span<const std::string> archived = kind != 0 ? std::span{ids} : std::span<const std::string>{};
        const std::span<const std::string> retired = kind != 1 ? std::span{ids} : std::span<const std::string>{};
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(config, store, {archived, retired}),
                 DownloadOwnershipCapacity::Full);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), before);
        QCOMPARE(readFile(f.final), final_bytes);
        QCOMPARE(QFile::exists(f.job), kind == 6);
    }

    void fullJournalCanReclaimAnArchivedRowWithConfirmedCleanupAndNoFiles() {
        Fixture f;
        QVERIFY(f.initialized);
        f.record.cleanup_complete = true;
        const auto before = journalWith(f.record);
        const auto directory = seedJournal(f, DownloadOwnershipJson::encode(before));
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        const std::array ids{f.record.job_id};
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(f.config(), store, {ids, {}}),
                 DownloadOwnershipCapacity::Available);
        auto expected = before;
        expected.records.erase(expected.records.begin());
        const auto loaded = store.load();
        QVERIFY(loaded.journal);
        QCOMPARE(*loaded.journal, expected);
        QVERIFY(!QFile::exists(f.job));
        QVERIFY(!QFile::exists(f.final));
    }

    // A new attempt inside the same job (a source that refused the stream is
    // resolved again) reuses the job ID: a prior attempt that left no files is
    // retired so the new attempt can record its planned row.
    void aCleanPriorAttemptOfTheSameJobIsRetired() {
        Fixture f;
        QVERIFY(f.initialized);
        f.record.cleanup_complete = true;
        const auto directory = seedJournal(f, DownloadOwnershipJson::encode(journalWith(f.record, 2)));
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        QCOMPARE(WindowsDownloadOwnershipRecovery::retirePriorAttempt(f.config(), store, f.record.job_id),
                 DownloadOwnershipCapacity::Available);
        const auto loaded = store.load();
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), std::size_t{1});
        QVERIFY(loaded.journal->records.front().job_id != f.record.job_id);
        // Without a prior row there is nothing to do.
        const auto bytes = readFile(directory + QStringLiteral("/download-ownership.json"));
        QCOMPARE(WindowsDownloadOwnershipRecovery::retirePriorAttempt(f.config(), store, f.record.job_id),
                 DownloadOwnershipCapacity::Available);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), bytes);
    }
    void aPublishedPriorAttemptIsNeverRetired() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        QVERIFY(f.removeTemps());
        f.record.cleanup_complete = true;
        const auto before = DownloadOwnershipJson::encode(journalWith(f.record, 1));
        const auto directory = seedJournal(f, before);
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        QCOMPARE(WindowsDownloadOwnershipRecovery::retirePriorAttempt(f.config(), store, f.record.job_id),
                 DownloadOwnershipCapacity::PriorAttemptUnresolved);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), before);
    }
    // L8: parallel lanes each record a planned row after this check, so the
    // check leaves one free row per lane.
    void headroomReclaimsUntilEveryLaneHasARow() {
        Fixture f;
        QVERIFY(f.initialized);
        f.record.cleanup_complete = true;
        auto before = journalWith(f.record);
        std::vector<std::string> ids{f.record.job_id};
        for (std::size_t index = 1; index <= 3; ++index) {
            before.records[index].cleanup_complete = true;
            ids.push_back(before.records[index].job_id);
        }
        const auto directory = seedJournal(f, DownloadOwnershipJson::encode(before));
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(f.config(), store, {ids, {}}, {}, 3),
                 DownloadOwnershipCapacity::Available);
        const auto loaded = store.load();
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), DownloadOwnershipJournal::maximum_records - 3);
        // With room for every lane, nothing else is rewritten.
        const auto bytes = readFile(directory + QStringLiteral("/download-ownership.json"));
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(f.config(), store, {ids, {}}, {}, 3),
                 DownloadOwnershipCapacity::Available);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), bytes);
    }
    void headroomStillAdmitsAJobWhenFewerRowsAreReclaimable() {
        Fixture f;
        QVERIFY(f.initialized);
        f.record.cleanup_complete = true;
        const auto before = journalWith(f.record);
        const auto directory = seedJournal(f, DownloadOwnershipJson::encode(before));
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        const std::array ids{f.record.job_id};
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(f.config(), store, {ids, {}}, {}, 3),
                 DownloadOwnershipCapacity::Available);
        const auto loaded = store.load();
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), DownloadOwnershipJournal::maximum_records - 1);
    }
    void capacityDoesNotRewriteSmallOrUnknownHistory_data() {
        QTest::addColumn<bool>("future");
        QTest::newRow("space-available") << false;
        QTest::newRow("future-schema") << true;
    }
    void capacityDoesNotRewriteSmallOrUnknownHistory() {
        QFETCH(bool, future);
        Fixture f;
        QVERIFY(f.initialized);
        f.record.cleanup_complete = true;
        const QByteArray before =
            future ? QByteArray{R"({"schemaVersion":9})"} : DownloadOwnershipJson::encode(journalWith(f.record, 1));
        const auto directory = seedJournal(f, before);
        QVERIFY(!directory.isEmpty());
        WindowsDownloadOwnershipStore store(directory);
        const std::array ids{f.record.job_id};
        QCOMPARE(WindowsDownloadOwnershipRecovery::ensureCapacity(f.config(), store, {ids, {}}),
                 future ? DownloadOwnershipCapacity::HistoryUnavailable : DownloadOwnershipCapacity::Available);
        QCOMPARE(readFile(directory + QStringLiteral("/download-ownership.json")), before);
    }

    void finalReadCapabilityHasIndependentReadOnlyAccessWithoutDependingOnTheOriginalName() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        QVERIFY(f.removeTemps());
        f.record.cleanup_complete = true;
        auto pinned = WindowsDownloadOwnershipRecovery::pinFinal(f.config(), f.record);
        QCOMPARE(pinned.assessment, DownloadOwnershipAssessment::OwnedFinalCandidate);
        QVERIFY(pinned.candidate);
        auto first = pinned.candidate->readInput();
        auto second = pinned.candidate->readInput();
        QVERIFY(first);
        QVERIFY(second);
        const QByteArray expected(output_content);
        QCOMPARE(readCapability(first, 2), expected.left(2));
        QCOMPARE(readCapability(second, 2), expected.left(2));
        DWORD written = 0;
        SetLastError(ERROR_SUCCESS);
        const bool wrote =
            WriteFile(reinterpret_cast<HANDLE>(first->handleValue()), "x", 1, &written, nullptr) != FALSE;
        const auto error = GetLastError();
        QVERIFY(!wrote);
        QCOMPARE(error, DWORD{ERROR_ACCESS_DENIED});
        second.reset();
        pinned.candidate.reset();
        const auto renamed = f.final + QStringLiteral(".moved");
        QVERIFY(MoveFileExW(native(f.final).c_str(), native(renamed).c_str(), 0));
        QCOMPARE(readCapability(first), expected.mid(2));
        QCOMPARE(readFile(renamed), expected);
    }

    void finalPinRetainsDerivedPathDetachedEvidenceAndReadOnlyAccess_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("prepared-after-rename") << 0;
        QTest::newRow("published-with-temporaries") << 1;
        QTest::newRow("published-cleaned") << 2;
    }
    void finalPinRetainsDerivedPathDetachedEvidenceAndReadOnlyAccess() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        if (kind == 0)
            f.record.stage = DownloadOwnershipStage::PublicationPrepared;
        if (kind == 2) {
            QVERIFY(f.removeTemps());
            f.record.cleanup_complete = true;
        }
        const auto evidence = f.record;
        auto pinned = WindowsDownloadOwnershipRecovery::pinFinal(f.config(), f.record);
        QCOMPARE(pinned.assessment, DownloadOwnershipAssessment::OwnedFinalCandidate);
        QVERIFY(pinned.candidate);
        QCOMPARE(pinned.candidate->pathForValidation(), f.final);
        QCOMPARE(pinned.candidate->record(), evidence);
        QCOMPARE(pinned.candidate->revalidate(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        // These intentionally non-MP3 bytes may be pinned, never media-approved.
        QCOMPARE(readFile(pinned.candidate->pathForValidation()), QByteArray(output_content));
        f.record.job_id = "caller-replaced-id";
        QCOMPARE(pinned.candidate->record(), evidence);
        QCOMPARE(pinned.candidate->revalidate(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        pinned.candidate.reset();
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        QCOMPARE(QFile::exists(f.input), kind != 2);
        QCOMPARE(QFile::exists(f.job), kind != 2);
        QVERIFY(!QFile::exists(f.output));
    }

    void recoveryUsesRecordedUnicodeLeafAndRefusesAnUnrelatedReplacement() {
        Fixture f;
        QVERIFY(f.ready());
        const auto legacy_path = f.final;
        const auto leaf = QStringLiteral("曲名 - Artista (2).mp3");
        f.final = f.publication + QLatin1Char('/') + leaf;
        QVERIFY(f.publish());
        auto root = QJsonDocument::fromJson(DownloadOwnershipJson::encode({{f.record}})).object();
        auto records = root.value(QStringLiteral("records")).toArray();
        auto row = records[0].toObject();
        row.insert(QStringLiteral("publicationLeaf"), leaf);
        records[0] = row;
        root.insert(QStringLiteral("schemaVersion"), 3);
        root.insert(QStringLiteral("records"), records);
        const auto decoded = DownloadOwnershipJson::decode(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QCOMPARE(decoded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(decoded.journal);
        const auto& named = decoded.journal->records.front();
        {
            auto pinned = WindowsDownloadOwnershipRecovery::pinFinal(f.config(), named);
            QCOMPARE(pinned.assessment, DownloadOwnershipAssessment::OwnedFinalCandidate);
            QVERIFY(pinned.candidate);
            QCOMPARE(pinned.candidate->pathForValidation(), f.final);
            QCOMPARE(pinned.candidate->revalidate(), DownloadOwnershipAssessment::OwnedFinalCandidate);
            QVERIFY(!QFile::exists(legacy_path));
        }
        const auto moved = f.final + QStringLiteral(".moved");
        QVERIFY(MoveFileExW(native(f.final).c_str(), native(moved).c_str(), 0));
        QVERIFY(createFile(f.final, output_content, f.security));
        QCOMPARE(WindowsDownloadOwnershipRecovery::assess(f.config(), named), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        QVERIFY(QFile::exists(moved));
    }

    void finalPinKeepsFileTemporaryAndParentIdentitiesUntilItsLastOwnerReleases() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        auto pinned = WindowsDownloadOwnershipRecovery::pinFinal(f.config(), f.record);
        QCOMPARE(pinned.assessment, DownloadOwnershipAssessment::OwnedFinalCandidate);
        QVERIFY(pinned.candidate);
        auto owner = std::move(pinned.candidate);
        QVERIFY(!pinned.candidate);
        const auto renamed = f.final + QStringLiteral(".moved");
        const auto moved_publication = f.publication + QStringLiteral("-moved");
        const auto moved_staging = f.staging + QStringLiteral("-moved");
        {
            Handle final_writer(CreateFileW(native(f.final).c_str(), GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
            Handle input_writer(CreateFileW(native(f.input).c_str(), GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
            QVERIFY(!final_writer.valid());
            QVERIFY(!input_writer.valid());
        }
        QVERIFY(!changeBytes(f.final, "must-not-replace"));
        QVERIFY(!DeleteFileW(native(f.final).c_str()));
        QVERIFY(!MoveFileExW(native(f.final).c_str(), native(renamed).c_str(), 0));
        QVERIFY(!MoveFileExW(native(f.publication).c_str(), native(moved_publication).c_str(), 0));
        QVERIFY(!MoveFileExW(native(f.staging).c_str(), native(moved_staging).c_str(), 0));
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(owner->revalidate(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        owner.reset();
        {
            Handle final_writer(CreateFileW(native(f.final).c_str(), GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
            Handle input_writer(CreateFileW(native(f.input).c_str(), GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
            QVERIFY(final_writer.valid());
            QVERIFY(input_writer.valid());
        }
        QVERIFY(MoveFileExW(native(f.final).c_str(), native(renamed).c_str(), 0));
        QVERIFY(MoveFileExW(native(renamed).c_str(), native(f.final).c_str(), 0));
        QVERIFY(MoveFileExW(native(f.publication).c_str(), native(moved_publication).c_str(), 0));
        QVERIFY(MoveFileExW(native(moved_publication).c_str(), native(f.publication).c_str(), 0));
        QVERIFY(DeleteFileW(native(f.final).c_str()));
        QCOMPARE(readFile(f.input), QByteArray(input_content));
    }

    void finalPinRevalidatesDaclRatherThanReusingItsEarlierObservation() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        auto pinned = WindowsDownloadOwnershipRecovery::pinFinal(f.config(), f.record);
        QCOMPARE(pinned.assessment, DownloadOwnershipAssessment::OwnedFinalCandidate);
        QVERIFY(pinned.candidate);
        QVERIFY(pinned.candidate->readInput());
        PrivateSecurity wrong_dacl(false);
        QVERIFY(wrong_dacl.valid());
        QVERIFY(replaceDacl(f.final, false, wrong_dacl));
        QCOMPARE(pinned.candidate->revalidate(), DownloadOwnershipAssessment::Conflict);
        QVERIFY(!pinned.candidate->readInput());
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        // Restore the fixture's original explicit descriptor and ACE flags.
        // This is fixture restoration only; recovery never repairs permissions.
        {
            Handle restore(CreateFileW(native(f.final).c_str(), WRITE_DAC,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            QVERIFY(restore.valid());
            QVERIFY(SetKernelObjectSecurity(restore.get(),
                                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                            f.security.descriptor()));
        }
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        QCOMPARE(pinned.candidate->revalidate(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        QVERIFY(pinned.candidate->readInput());
        QCOMPARE(pinned.candidate->record(), f.record);
    }

    void finalPinPreservesNonFinalOrRefusedAssessmentAndNeverReturnsACapability_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("expected");
        const auto row = [](const char* name, int kind, DownloadOwnershipAssessment expected) {
            QTest::newRow(name) << kind << static_cast<int>(expected);
        };
        row("partial", 0, DownloadOwnershipAssessment::OwnedPartial);
        row("prepared-partial", 1, DownloadOwnershipAssessment::OwnedPartial);
        row("missing", 2, DownloadOwnershipAssessment::Missing);
        row("invalid-record", 3, DownloadOwnershipAssessment::Unverified);
        row("bad-hash", 4, DownloadOwnershipAssessment::Conflict);
        row("hardlinked-final", 5, DownloadOwnershipAssessment::Conflict);
        row("wrong-final-dacl", 6, DownloadOwnershipAssessment::Conflict);
        row("over-budget", 7, DownloadOwnershipAssessment::Conflict);
        row("invalid-config", 8, DownloadOwnershipAssessment::Unverified);
        row("sharing-denied", 9, DownloadOwnershipAssessment::Unreadable);
    }
    void finalPinPreservesNonFinalOrRefusedAssessmentAndNeverReturnsACapability() {
        QFETCH(int, kind);
        QFETCH(int, expected);
        Fixture f;
        QVERIFY(f.ready());
        if (kind == 1)
            f.prepared();
        if (kind >= 2)
            QVERIFY(f.publish());
        auto config = f.config();
        PrivateSecurity wrong_dacl(false);
        std::unique_ptr<Handle> blocker;
        if (kind == 2) {
            QVERIFY(f.removeTemps());
            QVERIFY(DeleteFileW(native(f.final).c_str()));
        } else if (kind == 3)
            f.record.job_directory.reset();
        else if (kind == 4)
            f.record.output_sha256 = std::string(64, 'b');
        else if (kind == 5) {
            const auto alias = f.root.filePath(QStringLiteral("same-final-hardlink.bin"));
            QVERIFY(CreateHardLinkW(native(alias).c_str(), native(f.final).c_str(), nullptr));
        } else if (kind == 6) {
            QVERIFY(wrong_dacl.valid());
            QVERIFY(replaceDacl(f.final, false, wrong_dacl));
        } else if (kind == 7)
            config.max_workspace_bytes = 1;
        else if (kind == 8)
            config.max_workspace_bytes = 0;
        else if (kind == 9) {
            blocker = std::make_unique<Handle>(CreateFileW(native(f.final).c_str(), GENERIC_WRITE,
                                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                           nullptr, OPEN_EXISTING, 0, nullptr));
            QVERIFY(blocker->valid());
        }
        const auto input_before = readFile(f.input);
        const auto output_before = readFile(f.output);
        const auto final_before = readFile(f.final);
        const auto evidence = f.record;
        const auto pinned = WindowsDownloadOwnershipRecovery::pinFinal(config, f.record);
        QCOMPARE(pinned.assessment, static_cast<DownloadOwnershipAssessment>(expected));
        QVERIFY(!pinned.candidate);
        QCOMPARE(f.record, evidence);
        QCOMPARE(readFile(f.input), input_before);
        QCOMPARE(readFile(f.output), output_before);
        QCOMPARE(readFile(f.final), final_before);
    }

    void discardOwnedPartialsRemovesRealObjectsBeforeDurableConfirmation() {
        Fixture f;
        QVERIFY(f.ready());
        const auto sentinel = f.staging + QStringLiteral("/keep-sibling.bin");
        QVERIFY(createFile(sentinel, "keep-sibling", f.security));
        RecordingStore store;
        bool absent_at_save = false;
        bool parent_still_pinned = false;
        store.before_record = [&] {
            absent_at_save = !QFile::exists(f.input) && !QFile::exists(f.output) && !QFile::exists(f.job);
            Handle rename_attempt(CreateFileW(native(f.staging).c_str(), DELETE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            parent_still_pinned = !rename_attempt.valid();
        };
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Discarded);
        QVERIFY(absent_at_save);
        QVERIFY(parent_still_pinned);
        QVERIFY(!QFile::exists(f.job));
        QVERIFY(QFile::exists(f.staging));
        QVERIFY(QFile::exists(f.publication));
        QCOMPARE(readFile(sentinel), QByteArray("keep-sibling"));
        QCOMPARE(store.attempts.size(), std::size_t{1});
        auto cleaned = f.record;
        cleaned.cleanup_complete = true;
        QCOMPARE(store.attempts.front(), cleaned);
        QVERIFY(!f.record.cleanup_complete);
        QCOMPARE(store.load_calls, 0);
    }

    void discardVerifiedPreparedPartialsNeverRequiresMediaDecode() {
        Fixture f;
        QVERIFY(f.ready());
        f.prepared();
        RecordingStore store;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Discarded);
        QVERIFY(!QFile::exists(f.job));
        QCOMPARE(store.attempts.size(), std::size_t{1});
        QCOMPARE(store.attempts.front().stage, DownloadOwnershipStage::PublicationPrepared);
        QCOMPARE(store.attempts.front().output_sha256, f.record.output_sha256);
    }

    void discardFinalCandidateRefusesTheEntireOperation_data() {
        QTest::addColumn<bool>("published");
        QTest::newRow("prepared-rename") << false;
        QTest::newRow("published") << true;
    }
    void discardFinalCandidateRefusesTheEntireOperation() {
        QFETCH(bool, published);
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        if (!published)
            f.record.stage = DownloadOwnershipStage::PublicationPrepared;
        // A final needs only read access. This reader denies DELETE sharing,
        // so obtaining a delete capability for the final would fail the test.
        Handle final_reader(CreateFileW(native(f.final).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        QVERIFY(final_reader.valid());
        RecordingStore store;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::OwnedFinalCandidate);
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QVERIFY(QFile::exists(f.job));
        QVERIFY(store.attempts.empty());
    }

    void discardRevalidatesAfterAssessmentAndPreservesReplacements() {
        Fixture f;
        QVERIFY(f.ready());
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        const auto original = f.root.filePath(QStringLiteral("keep-original-input.bin"));
        QVERIFY(MoveFileExW(native(f.input).c_str(), native(original).c_str(), 0));
        QVERIFY(createFile(f.input, input_content, f.security));
        QVERIFY(identity(f.input, false) != f.record.input);
        RecordingStore store;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Conflict);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(original), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(store.attempts.empty());
    }

    void discardRefusesConflictingEvidenceBeforeAnyMutation_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("unknown-child") << 0;
        QTest::newRow("cleanup-contradiction") << 1;
        QTest::newRow("staging-identity") << 2;
        QTest::newRow("publication-identity") << 3;
        QTest::newRow("job-identity") << 4;
        QTest::newRow("input-identity") << 5;
        QTest::newRow("output-identity") << 6;
        QTest::newRow("prepared-digest") << 7;
        QTest::newRow("prepared-input-size") << 8;
        QTest::newRow("configured-budget") << 9;
        QTest::newRow("private-dacl") << 10;
        QTest::newRow("hardlink") << 11;
        QTest::newRow("ready-final-collision") << 12;
        QTest::newRow("published-source-contradiction") << 13;
    }
    void discardRefusesConflictingEvidenceBeforeAnyMutation() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        auto config = f.config();
        const auto sentinel = f.root.filePath(QStringLiteral("keep.bin"));
        QVERIFY(createFile(sentinel, "keep", f.security));
        const auto unknown = f.job + QStringLiteral("/unknown.bin");
        const auto hardlink = f.root.filePath(QStringLiteral("keep-hardlink.bin"));
        if (kind == 0)
            QVERIFY(createFile(unknown, "unknown", f.security));
        else if (kind == 1)
            f.record.cleanup_complete = true;
        else if (kind >= 2 && kind <= 6) {
            auto* changed = kind == 2   ? &f.record.staging_parent
                            : kind == 3 ? &f.record.publish_parent
                            : kind == 4 ? &*f.record.job_directory
                            : kind == 5 ? &*f.record.input
                                        : &*f.record.output;
            changed->file_id[15] ^= 0x55;
        } else if (kind == 7 || kind == 8) {
            f.prepared();
            if (kind == 7)
                f.record.output_sha256 = std::string(64, 'a');
            else
                ++f.record.input_bytes;
        } else if (kind == 9)
            config.max_workspace_bytes = 1;
        else if (kind == 10) {
            PrivateSecurity no_inheritance(false);
            QVERIFY(no_inheritance.valid());
            QVERIFY(replaceDacl(f.output, false, no_inheritance));
        } else if (kind == 11)
            QVERIFY(CreateHardLinkW(native(hardlink).c_str(), native(f.output).c_str(), nullptr));
        else if (kind == 12)
            QVERIFY(createFile(f.final, "keep-final", f.security));
        else {
            f.prepared();
            f.record.stage = DownloadOwnershipStage::Published;
        }
        RecordingStore store;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(config, f.record, store),
                 DownloadOwnershipDiscardOutcome::Conflict);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QCOMPARE(readFile(sentinel), QByteArray("keep"));
        QVERIFY(QFile::exists(f.job));
        QVERIFY(store.attempts.empty());
        if (kind == 0)
            QCOMPARE(readFile(unknown), QByteArray("unknown"));
        if (kind == 11)
            QCOMPARE(readFile(hardlink), QByteArray(output_content));
        if (kind == 12)
            QCOMPARE(readFile(f.final), QByteArray("keep-final"));
    }

    void discardPlannedInvalidOrUntrustedConfigurationCannotClaimObjects_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("planned") << 0;
        QTest::newRow("invalid-id") << 1;
        QTest::newRow("relative-parent") << 2;
        QTest::newRow("zero-budget") << 3;
    }
    void discardPlannedInvalidOrUntrustedConfigurationCannotClaimObjects() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        auto config = f.config();
        if (kind == 0) {
            f.record.stage = DownloadOwnershipStage::Planned;
            f.record.job_directory.reset();
            f.record.input.reset();
            f.record.output.reset();
        } else if (kind == 1)
            f.record.workspace_id = "../outside";
        else if (kind == 2)
            config.staging_parent = QStringLiteral("relative/staging");
        else
            config.max_workspace_bytes = 0;
        RecordingStore store;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(config, f.record, store),
                 DownloadOwnershipDiscardOutcome::Unverified);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(store.attempts.empty());
    }

    void discardDoesNotCreateMissingParentsOrRewriteMissingWorkspace() {
        Fixture f;
        QVERIFY(f.ready());
        RecordingStore store;
        auto config = f.config();
        config.staging_parent += QStringLiteral("/not-created");
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(config, f.record, store),
                 DownloadOwnershipDiscardOutcome::Unreadable);
        QVERIFY(!QFile::exists(config.staging_parent));
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QVERIFY(f.removeTemps());
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Missing);
        QVERIFY(!QFile::exists(f.job));
        QVERIFY(store.attempts.empty());
    }

    void discardSharingDenialPreservesEveryTemporary() {
        Fixture f;
        QVERIFY(f.ready());
        RecordingStore store;
        {
            Handle blocker(CreateFileW(native(f.output).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            QVERIFY(blocker.valid());
            QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                     DownloadOwnershipDiscardOutcome::Unreadable);
        }
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(store.attempts.empty());
    }

    void discardPhysicalFailureKeepsRecordAndAllowsFreshRetryOfSurvivingObjects() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(SetFileAttributesW(native(f.output).c_str(), FILE_ATTRIBUTE_READONLY));
        RestoreAttributes restore{f.output};
        RecordingStore store;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Incomplete);
        QVERIFY(!QFile::exists(f.input));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(QFile::exists(f.job));
        QVERIFY(store.attempts.empty());
        QVERIFY(!f.record.cleanup_complete);
        QVERIFY(SetFileAttributesW(native(f.output).c_str(), FILE_ATTRIBUTE_NORMAL));
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Discarded);
        QVERIFY(!QFile::exists(f.job));
        QCOMPARE(store.attempts.size(), std::size_t{1});
        QVERIFY(store.attempts.front().cleanup_complete);
    }

    void discardDeletePendingIsNotPhysicalCompletionBeforeExternalReaderCloses() {
        Fixture f;
        QVERIFY(f.ready());
        RecordingStore store;
        {
            Handle reader(CreateFileW(native(f.input).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            QVERIFY(reader.valid());
            QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                     DownloadOwnershipDiscardOutcome::Incomplete);
            QVERIFY(store.attempts.empty());
            // The still-open reader can read the delete-pending original.
            std::array<char, 16> bytes{};
            DWORD read = 0;
            QVERIFY(ReadFile(reader.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr));
            QCOMPARE(QByteArray(bytes.data(), read), QByteArray(input_content));
            QCOMPARE(readFile(f.output), QByteArray(output_content));
            QVERIFY(QFile::exists(f.job));
        }
        QVERIFY(!QFile::exists(f.input));
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Discarded);
        QVERIFY(!QFile::exists(f.job));
        QCOMPARE(store.attempts.size(), std::size_t{1});
    }

    void discardRealStoreSeparatesPhysicalRemovalFromJournalAcknowledgement_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("saved") << 0;
        QTest::newRow("failed-before-commit") << 1;
        QTest::newRow("failed-after-commit") << 2;
        QTest::newRow("save-directory-absent") << 3;
    }
    void discardRealStoreSeparatesPhysicalRemovalFromJournalAcknowledgement() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialized);
        PrivateSecurity document_security(false);
        QVERIFY(document_security.valid());
        const auto directory = f.root.filePath(QStringLiteral("private-journal"));
        QVERIFY(CreateDirectoryW(native(directory).c_str(), document_security.attributes()));
        WindowsDownloadOwnershipStore baseline(directory);
        QCOMPARE(baseline.record(f.record), DownloadCheckpointSaveStatus::Saved);
        QVERIFY(f.ready());
        QCOMPARE(baseline.record(f.record), DownloadCheckpointSaveStatus::Saved);
        const auto journal = directory + QStringLiteral("/download-ownership.json");
        const auto before = readFile(journal);
        QVERIFY(!before.isEmpty());
        const auto sentinel = directory + QStringLiteral("/keep-sentinel.bin");
        QVERIFY(createFile(sentinel, "keep-sentinel", document_security));
        const auto entries = QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        const auto missing_directory = f.root.filePath(QStringLiteral("never-created-journal"));
        WindowsDownloadOwnershipStore store(
            kind == 3 ? missing_directory : directory,
            {.inject_fault = [kind](WindowsDownloadOwnershipFaultPoint point) {
                return (kind == 1 && point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) ||
                       (kind == 2 && point == WindowsDownloadOwnershipFaultPoint::AfterCommit);
            }});
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 kind == 0 ? DownloadOwnershipDiscardOutcome::Discarded : DownloadOwnershipDiscardOutcome::Unconfirmed);
        QVERIFY(!QFile::exists(f.job));
        QVERIFY(!QFile::exists(f.input));
        QVERIFY(!QFile::exists(f.output));
        QVERIFY(!QFile::exists(f.final));
        QVERIFY(!QFile::exists(missing_directory));
        QCOMPARE(readFile(sentinel), QByteArray("keep-sentinel"));
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), entries);
        WindowsDownloadOwnershipStore restarted(directory);
        const auto loaded = restarted.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), std::size_t{1});
        auto expected = f.record;
        expected.cleanup_complete = kind == 0 || kind == 2;
        QCOMPARE(loaded.journal->records.front(), expected);
        const auto after = readFile(journal);
        if (kind == 1 || kind == 3)
            QCOMPARE(after, before);
        else
            QVERIFY(after != before);
        // A stale record without cleanup confirmation cannot authorize another save.
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Missing);
        QCOMPARE(readFile(journal), after);
        if (kind == 2) {
            // A new explicit discard with current evidence can obtain its own Saved acknowledgement.
            QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), loaded.journal->records.front(),
                                                                       restarted),
                     DownloadOwnershipDiscardOutcome::Discarded);
            QCOMPARE(readFile(journal), after);
        }
    }

    void discardRequiresSavedNotAReadableCommitAfterRemoval_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("failed-before-commit") << 0;
        QTest::newRow("failed-after-commit") << 1;
        QTest::newRow("blocked-save") << 2;
        QTest::newRow("throw-before-commit") << 3;
        QTest::newRow("throw-after-commit") << 4;
    }
    void discardRequiresSavedNotAReadableCommitAfterRemoval() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        RecordingStore store;
        store.status =
            kind == 2 ? DownloadCheckpointSaveStatus::BlockedByExistingData : DownloadCheckpointSaveStatus::Failed;
        store.commit_before_failure = kind == 1 || kind == 4;
        store.throw_on_record = kind == 3 || kind == 4;
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Unconfirmed);
        QVERIFY(!QFile::exists(f.input));
        QVERIFY(!QFile::exists(f.output));
        QVERIFY(!QFile::exists(f.job));
        QCOMPARE(store.attempts.size(), std::size_t{1});
        QVERIFY(store.attempts.front().cleanup_complete);
        QVERIFY(!f.record.cleanup_complete);
        QCOMPARE(store.load_calls, 0);
        if (store.commit_before_failure)
            QVERIFY(store.committed && store.committed->cleanup_complete);
        // Explicit recheck cannot turn an unacknowledged save into success.
        QCOMPARE(WindowsDownloadOwnershipRecovery::discardPartials(f.config(), f.record, store),
                 DownloadOwnershipDiscardOutcome::Missing);
        QCOMPARE(store.attempts.size(), std::size_t{1});
        QCOMPARE(store.load_calls, 0);
    }

    void plannedEvidenceDoesNotClaimExistingObjectsOrMutateThem() {
        Fixture f;
        QVERIFY(f.ready());
        auto planned = f.record;
        planned.stage = DownloadOwnershipStage::Planned;
        planned.job_directory.reset();
        planned.input.reset();
        planned.output.reset();
        QCOMPARE(WindowsDownloadOwnershipRecovery::assess(f.config(), planned),
                 DownloadOwnershipAssessment::Unverified);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(!QFile::exists(f.final));
    }

    void ownedPartialNeedsMatchingDirectoryAndFileIdentities() {
        Fixture f;
        QVERIFY(f.ready());
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(!QFile::exists(f.final));
    }

    void preparedSourceIsPartialNotACompletedMp3() {
        Fixture f;
        QVERIFY(f.ready());
        f.prepared();
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        QVERIFY(QFile::exists(f.output));
        QVERIFY(!QFile::exists(f.final));
    }

    void renameWithoutPublishedAcknowledgementIsOnlyAFinalCandidate() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        f.record.stage = DownloadOwnershipStage::PublicationPrepared;
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        QVERIFY(!QFile::exists(f.output));
    }

    void publishedCandidateIsNotMediaApprovalEvenWithMatchingHash() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        // This is deliberately not valid MP3 media. Identity+hash do not decode.
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        QCOMPARE(readFile(f.final), QByteArray(output_content));
    }

    void cleanedPublishedFinalRemainsAReadOnlyCandidate() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        QVERIFY(f.removeTemps());
        f.record.cleanup_complete = true;
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedFinalCandidate);
        QCOMPARE(readFile(f.final), QByteArray(output_content));
        QVERIFY(!QFile::exists(f.job));
    }

    void allExpectedObjectsMissingCannotClaimOwnership() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.removeTemps());
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Missing);
        f.record.cleanup_complete = true;
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Missing);
    }

    void partialCleanupStillOnlyClassifiesRemainingOwnedObjects() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(DeleteFileW(native(f.input).c_str()));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QVERIFY(!QFile::exists(f.input));
    }

    void missingPublishedFinalDoesNotImportSurvivingTemporary() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        QVERIFY(DeleteFileW(native(f.final).c_str()));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
    }

    void claimedCleanupWithSurvivingTemporaryConflicts() {
        Fixture f;
        QVERIFY(f.ready());
        f.record.cleanup_complete = true;
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QVERIFY(QFile::exists(f.input));
        QVERIFY(QFile::exists(f.output));
    }

    void detectsIdentityMismatch_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind != 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void detectsIdentityMismatch() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        auto* changed = kind == 0   ? &f.record.staging_parent
                        : kind == 1 ? &f.record.publish_parent
                        : kind == 2 ? &*f.record.job_directory
                        : kind == 3 ? &*f.record.input
                                    : &*f.record.output;
        changed->file_id[15] ^= 0x55;
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
    }

    void unknownJobChildIsPreservedAndConflicts() {
        Fixture f;
        QVERIFY(f.ready());
        const auto unknown = f.job + QStringLiteral("/unknown.bin");
        QVERIFY(createFile(unknown, "keep-unknown", f.security));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(unknown), QByteArray("keep-unknown"));
        QCOMPARE(readFile(f.input), QByteArray(input_content));
    }

    void sourceAndDestinationTogetherConflictWithoutOverwritingEither() {
        Fixture f;
        QVERIFY(f.ready());
        f.prepared();
        QVERIFY(createFile(f.final, "keep-collision", f.security));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(f.output), QByteArray(output_content));
        QCOMPARE(readFile(f.final), QByteArray("keep-collision"));
    }

    void readyStageDestinationCollisionIsNotAnOwnedFinal() {
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(createFile(f.final, "keep-collision", f.security));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(f.final), QByteArray("keep-collision"));
    }

    void detectsHashSizeOrContentMismatch_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind != 4; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void detectsHashSizeOrContentMismatch() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        QVERIFY(f.publish());
        if (kind == 0)
            f.record.output_sha256 = std::string(64, 'a');
        else if (kind == 1)
            ++f.record.output_bytes;
        else if (kind == 2)
            QVERIFY(changeBytes(f.final, QByteArray(std::strlen(output_content), 'z')));
        else
            QVERIFY(changeBytes(f.final, "different-size"));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QVERIFY(QFile::exists(f.final));
    }

    void currentPartialBytesShareTheConfiguredAndRecordedBudget_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("recorded") << 0;
        QTest::newRow("configured") << 1;
        QTest::newRow("exact") << 2;
    }
    void currentPartialBytesShareTheConfiguredAndRecordedBudget() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready("12345", "123456789012"));
        if (kind == 0)
            f.record.maximum_workspace_bytes = 16;
        const auto result = WindowsDownloadOwnershipRecovery::assess(f.config(kind == 1 ? 16 : 17), f.record);
        QCOMPARE(result, kind == 2 ? DownloadOwnershipAssessment::OwnedPartial : DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(f.input), QByteArray("12345"));
        QCOMPARE(readFile(f.output), QByteArray("123456789012"));
    }

    void zeroLengthReadyFilesAreOwnedPartialNotPublishedMedia() {
        Fixture f;
        QVERIFY(f.ready({}, {}));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        QCOMPARE(readFile(f.input), QByteArray{});
        QCOMPARE(readFile(f.output), QByteArray{});
    }

    void wrongDaclOrInheritanceIsNotAccepted_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind != 4; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void wrongDaclOrInheritanceIsNotAccepted() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        PrivateSecurity non_inheritable(false);
        QVERIFY(non_inheritable.valid());
        const auto path = kind == 0 ? f.job : kind == 1 ? f.input : f.output;
        QVERIFY(replaceDacl(path, kind == 0, kind == 3 ? f.security : non_inheritable, kind != 3));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
    }

    void hardlinkedFileIsNotAnOwnedSingleLeaf() {
        Fixture f;
        QVERIFY(f.ready());
        const auto alias = f.root.filePath(QStringLiteral("same-temp-hardlink.bin"));
        QVERIFY(CreateHardLinkW(native(alias).c_str(), native(f.output).c_str(), nullptr));
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(alias), QByteArray(output_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
    }

    void sharingDenialIsUnreadableWithoutMutatingTheFile() {
        Fixture f;
        QVERIFY(f.ready());
        {
            Handle blocker(CreateFileW(native(f.output).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
            QVERIFY(blocker.valid());
            QCOMPARE(f.assess(), DownloadOwnershipAssessment::Unreadable);
        }
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::OwnedPartial);
        QCOMPARE(readFile(f.output), QByteArray(output_content));
    }

    void invalidRecordCannotTurnIdentifiersIntoPaths_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind != 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void invalidRecordCannotTurnIdentifiersIntoPaths() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.ready());
        if (kind == 0)
            f.record.workspace_id = "../../outside";
        else if (kind == 1)
            f.record.workspace_id = "abcdefab-cdef-4abc-8def-000000000001:stream";
        else if (kind == 2)
            f.record.stage = static_cast<DownloadOwnershipStage>(99);
        else if (kind == 3)
            f.record.maximum_workspace_bytes = 512ULL * 1024 * 1024 + 1;
        else
            f.record.output_sha256 = "not-a-hash";
        QCOMPARE(f.assess(), DownloadOwnershipAssessment::Unverified);
        QCOMPARE(readFile(f.input), QByteArray(input_content));
        QCOMPARE(readFile(f.output), QByteArray(output_content));
    }

    void missingTrustedParentIsUnreadableNotMissingOwnedObjects() {
        Fixture f;
        QVERIFY(f.ready());
        auto config = f.config();
        config.staging_parent += QStringLiteral("/not-created");
        QCOMPARE(WindowsDownloadOwnershipRecovery::assess(config, f.record), DownloadOwnershipAssessment::Unreadable);
        QVERIFY(!QFile::exists(config.staging_parent));
        QCOMPARE(readFile(f.input), QByteArray(input_content));
    }

    void configuredParentAndJobSymlinksAreNeverFollowed_data() {
        QTest::addColumn<bool>("parent");
        QTest::newRow("parent") << true;
        QTest::newRow("job") << false;
    }
    void configuredParentAndJobSymlinksAreNeverFollowed() {
        QFETCH(bool, parent);
        Fixture f;
        QVERIFY(f.ready());
        const auto target = f.root.filePath(QStringLiteral("same-temp-target"));
        QVERIFY(QDir().mkdir(target));
        QVERIFY(createFile(target + QStringLiteral("/keep.bin"), "keep-target", f.security));
        auto config = f.config();
        QString alias;
        if (parent) {
            alias = f.root.filePath(QStringLiteral("parent-alias"));
            config.staging_parent = alias;
        } else {
            QVERIFY(f.removeTemps());
            alias = f.job;
        }
        if (!CreateSymbolicLinkW(native(alias).c_str(), native(target).c_str(),
                                 SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
            QSKIP("Host policy does not permit this fixture-owned synthetic symbolic link.");
        RemoveDirectoryAlias unlink{alias};
        QCOMPARE(WindowsDownloadOwnershipRecovery::assess(config, f.record), DownloadOwnershipAssessment::Conflict);
        QCOMPARE(readFile(target + QStringLiteral("/keep.bin")), QByteArray("keep-target"));
    }
};

} // namespace music_player::m0

QTEST_GUILESS_MAIN(music_player::m0::WindowsDownloadOwnershipRecoveryTests)
#include "WindowsDownloadOwnershipRecoveryTests.moc"
