#include "adapters/WindowsDownloadOwnershipRecovery.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsDownloadReadInput.hpp"
#include "adapters/WindowsDownloadWorkspace.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Aclapi.h>
#include <Sddl.h>
#include <Windows.h>
#include <winioctl.h>

namespace music_player::m0 {
namespace {

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
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

std::wstring nativePath(const QString& path) { return QDir::toNativeSeparators(path).toStdWString(); }

bool setSyntheticJunction(const QString& directory, const QString& target) {
    struct Header final {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
    };
    static_assert(sizeof(Header) == 16);
    const auto print = nativePath(target);
    const auto substitute = std::wstring(L"\\??\\") + print;
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
    HANDLE handle = CreateFileW(nativePath(directory).c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD returned = 0;
    const bool set = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                                     nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return set;
}

struct Fixture final {
    QTemporaryDir root;
    QString staging;
    QString publication;

    Fixture() {
        staging = root.filePath(QString::fromUtf8("área temporária"));
        publication = root.filePath(QString::fromUtf8("músicas publicadas"));
        QDir().mkdir(staging);
        QDir().mkdir(publication);
    }

    WindowsDownloadWorkspaceConfig config(std::uint64_t limit = 32) const { return {staging, publication, limit}; }
};

bool fillAndSeal(WindowsDownloadWorkspace& workspace, const QByteArray& input = "input",
                 const QByteArray& output = "synthetic-mp3") {
    auto* input_sink = workspace.inputSink();
    auto* output_sink = workspace.outputSink();
    return input_sink && output_sink && input_sink->write(input) == input.size() && workspace.sealInput() &&
           output_sink->write(output) == output.size() && workspace.sealOutput();
}

class OwnershipStore final : public DownloadOwnershipStore {
  public:
    std::vector<DownloadOwnershipRecord> attempts{};
    std::function<DownloadCheckpointSaveStatus(const DownloadOwnershipRecord&)> on_record{};
    int loads{0};
    int depth{0};
    int maximum_depth{0};
    DownloadOwnershipLoadResult load() override {
        ++loads;
        return {};
    }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& value) override {
        attempts.push_back(value);
        ++depth;
        maximum_depth = std::max(maximum_depth, depth);
        try {
            const auto result = on_record ? on_record(value) : DownloadCheckpointSaveStatus::Saved;
            --depth;
            return result;
        } catch (...) {
            --depth;
            throw;
        }
    }
};

std::optional<DownloadFileIdentity> fileIdentity(const QString& path, const bool directory = false) {
    const auto handle = CreateFileW(
        nativePath(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;
    FILE_ID_INFO identity{};
    const bool obtained = GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)) != FALSE;
    CloseHandle(handle);
    if (!obtained)
        return std::nullopt;
    DownloadFileIdentity result{.volume_serial = identity.VolumeSerialNumber};
    std::memcpy(result.file_id.data(), identity.FileId.Identifier, result.file_id.size());
    return result;
}

QStringList jobs(const Fixture& fixture) { return QDir(fixture.staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }

bool createPrivateJournalDirectory(const QString& path) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD required = 0;
    static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0, &required));
    std::vector<unsigned char> bytes(required);
    const bool obtained = !bytes.empty() && GetTokenInformation(token, TokenUser, bytes.data(), required, &required);
    CloseHandle(token);
    if (!obtained)
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(bytes.data())->User.Sid, &sid))
        return false;
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + QLatin1Char(')');
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const bool created = CreateDirectoryW(nativePath(path).c_str(), &attributes) != FALSE;
    LocalFree(descriptor);
    return created;
}

class FaultedRealStore final : public DownloadOwnershipStore {
  public:
    FaultedRealStore(const QString& directory, const int fault)
        : store(directory, {.inject_fault = [this, fault](const WindowsDownloadOwnershipFaultPoint point) {
                    return saving_publication &&
                           ((fault == 1 && point == WindowsDownloadOwnershipFaultPoint::BeforeCommit) ||
                            (fault == 2 && point == WindowsDownloadOwnershipFaultPoint::AfterCommit));
                }}) {}
    DownloadOwnershipLoadResult load() override { return store.load(); }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& value) override {
        saving_publication = value.stage == DownloadOwnershipStage::Published && !value.cleanup_complete;
        const auto status = store.record(value);
        saving_publication = false;
        return status;
    }

  private:
    bool saving_publication{false};
    WindowsDownloadOwnershipStore store;
};

} // namespace

class WindowsDownloadWorkspaceTests final : public QObject {
    Q_OBJECT

  private slots:
    void customPublicationCanBeRecoveredAfterChangingDefaultFolder() {
        Fixture fixture;
        QVERIFY(fixture.root.isValid());
        auto config = fixture.config();
        const auto custom = fixture.root.filePath(QStringLiteral("Custom music"));
        QVERIFY(QDir().mkdir(custom));
        QVERIFY(validateWindowsDownloadDirectory(config.staging_parent, custom));
        QVERIFY(!validateWindowsDownloadDirectory(config.staging_parent, fixture.root.filePath("missing")));
        QVERIFY(!validateWindowsDownloadDirectory(config.staging_parent, config.staging_parent));
        config.custom_publish_parent = custom;
        OwnershipStore journal;
        WindowsDownloadWorkspace workspace(config, "custom-download", journal);
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        const auto result = workspace.validateAndPublish([](const QString&) { return true; });
        QCOMPARE(QFileInfo(result).absolutePath(), custom);
        QVERIFY(workspace.cleanup());
        QCOMPARE(journal.attempts.back().publication_directory, custom.toStdString());

        auto later = fixture.config();
        later.custom_publish_parent = fixture.root.filePath(QStringLiteral("another choice"));
        const auto recovered = WindowsDownloadOwnershipRecovery::pinFinal(later, journal.attempts.back());
        QVERIFY(recovered.candidate);
        QCOMPARE(recovered.candidate->pathForValidation(), result);
        auto replaced = journal.attempts.back();
        replaced.publication_directory = fixture.publication.toStdString();
        QCOMPARE(WindowsDownloadOwnershipRecovery::assess(later, replaced), DownloadOwnershipAssessment::Conflict);
    }

    void readCapabilitiesRequireTheirOwnSealAndDisappearAfterCleanup() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(!workspace.readInput());
        QVERIFY(!workspace.readOutput());
        QVERIFY(workspace.create());
        QCOMPARE(workspace.inputSink()->write("input", 5), qint64{5});
        QVERIFY(!workspace.readInput());
        QVERIFY(!workspace.readOutput());
        QCOMPARE(workspace.error(), DownloadFailure::None);
        QVERIFY(workspace.sealInput());
        QVERIFY(workspace.readInput());
        QVERIFY(!workspace.readOutput());
        QCOMPARE(workspace.outputSink()->write("synthetic-mp3", 13), qint64{13});
        QVERIFY(!workspace.readOutput());
        QVERIFY(workspace.sealOutput());
        QVERIFY(workspace.readOutput());
        QCOMPARE(workspace.error(), DownloadFailure::None);
        QVERIFY(workspace.cleanup());
        QVERIFY(!workspace.readInput());
        QVERIFY(!workspace.readOutput());
    }

    void sealedReadCapabilitiesAreReadOnlyWithIndependentCursors_data() {
        QTest::addColumn<bool>("output");
        QTest::newRow("downloaded-input") << false;
        QTest::newRow("converted-output") << true;
    }
    void sealedReadCapabilitiesAreReadOnlyWithIndependentCursors() {
        QFETCH(bool, output);
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        auto first = output ? workspace.readOutput() : workspace.readInput();
        auto second = output ? workspace.readOutput() : workspace.readInput();
        QVERIFY(first);
        QVERIFY(second);
        const QByteArray expected = output ? QByteArray("synthetic-mp3") : QByteArray("input");
        QCOMPARE(readCapability(first, 2), expected.left(2));
        QCOMPARE(readCapability(second, 2), expected.left(2));
        QCOMPARE(readCapability(first, 2), expected.mid(2, 2));
        DWORD written = 0;
        SetLastError(ERROR_SUCCESS);
        const bool wrote =
            WriteFile(reinterpret_cast<HANDLE>(first->handleValue()), "x", 1, &written, nullptr) != FALSE;
        const auto error = GetLastError();
        QVERIFY(!wrote);
        QCOMPARE(error, DWORD{ERROR_ACCESS_DENIED});
        QCOMPARE(readFile(output ? workspace.outputPath() : workspace.inputPath()), expected);
        first.reset();
        second.reset();
        QVERIFY(workspace.cleanup());
    }

    void outputReadCapabilityUsesTheSameFileObjectDuringValidationAndAfterPublication() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        auto output = workspace.readOutput();
        QVERIFY(output);
        const auto final = workspace.validateAndPublish([&](const QString&) {
            const auto validation_input = workspace.readOutput();
            return validation_input && readCapability(validation_input) == QByteArray("synthetic-mp3");
        });
        QVERIFY(!final.isEmpty());
        QVERIFY(!workspace.readOutput());
        QCOMPARE(readCapability(output), QByteArray("synthetic-mp3"));
        QCOMPARE(readFile(final), QByteArray("synthetic-mp3"));
        output.reset();
        QVERIFY(workspace.cleanup());
    }

    void createGuardCleanupReturningTrueCannotResumeCreation_data() {
        QTest::addColumn<int>("guard_index");
        QTest::addColumn<bool>("real_store");
        QTest::newRow("Planned-fake") << 1 << false;
        QTest::newRow("Ready-fake") << 2 << false;
        QTest::newRow("Planned-real-store") << 1 << true;
    }
    void createGuardCleanupReturningTrueCannotResumeCreation() {
        QFETCH(int, guard_index);
        QFETCH(bool, real_store);
        Fixture fixture;
        OwnershipStore fake;
        const auto journal_directory = fixture.root.filePath(QStringLiteral("private guard journal"));
        std::unique_ptr<WindowsDownloadOwnershipStore> real;
        if (real_store) {
            QVERIFY(createPrivateJournalDirectory(journal_directory));
            real = std::make_unique<WindowsDownloadOwnershipStore>(journal_directory);
        }
        DownloadOwnershipStore& store =
            real_store ? static_cast<DownloadOwnershipStore&>(*real) : static_cast<DownloadOwnershipStore&>(fake);
        {
            WindowsDownloadWorkspace workspace(fixture.config(), "job-reentrant-guard", store);
            int calls = 0;
            QVERIFY(!workspace.create([&] {
                ++calls;
                if (calls == guard_index)
                    static_cast<void>(workspace.cleanup());
                return true;
            }));
            QCOMPARE(calls, guard_index);
            QVERIFY(workspace.inputSink() == nullptr);
            QVERIFY(workspace.outputSink() == nullptr);
            QVERIFY(workspace.inputPath().isEmpty());
            QVERIFY(workspace.outputPath().isEmpty());
            QVERIFY(workspace.retainedPublicationPath().isEmpty());
            QVERIFY(jobs(fixture).isEmpty());
            QVERIFY(QDir(fixture.publication).entryList(QDir::Files).isEmpty());
        }
        QVERIFY(jobs(fixture).isEmpty());
        if (real_store) {
            WindowsDownloadOwnershipStore reader(journal_directory);
            const auto loaded = reader.load();
            QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(loaded.journal);
            QCOMPARE(loaded.journal->records.size(), std::size_t{1});
            QCOMPARE(loaded.journal->records.front().stage, DownloadOwnershipStage::Planned);
            QVERIFY(loaded.journal->records.front().cleanup_complete);
        } else if (guard_index == 1) {
            QVERIFY(std::none_of(fake.attempts.begin(), fake.attempts.end(), [](const DownloadOwnershipRecord& value) {
                return value.stage == DownloadOwnershipStage::WorkspaceReady;
            }));
        }
    }

    void durableRealJournalSurvivesFreshReaderAndAssessesOnlyAFileCandidate_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("saved") << 0;
        QTest::newRow("failed-before-published-commit") << 1;
        QTest::newRow("failed-after-published-commit") << 2;
    }
    void durableRealJournalSurvivesFreshReaderAndAssessesOnlyAFileCandidate() {
        QFETCH(int, fault);
        Fixture fixture;
        const auto journal_directory = fixture.root.filePath(QStringLiteral("private journal 音"));
        QVERIFY(createPrivateJournalDirectory(journal_directory));
        FaultedRealStore store(journal_directory, fault);
        QString planned;
        {
            WindowsDownloadWorkspace workspace(fixture.config(), "job-real-journal", store);
            QVERIFY(workspace.create());
            QVERIFY(fillAndSeal(workspace));
            planned = workspace.plannedPublicationPath();
            // This trusted synthetic validator does not prove MP3 content.
            const auto final = workspace.validateAndPublish([](const QString&) { return true; });
            if (fault == 0) {
                QCOMPARE(final, planned);
                QVERIFY(workspace.cleanup());
            } else {
                QVERIFY(final.isEmpty());
                QCOMPARE(workspace.error(), DownloadFailure::Publication);
                QCOMPARE(workspace.retainedPublicationPath(), planned);
            }
        }
        QVERIFY(jobs(fixture).isEmpty());
        QCOMPARE(readFile(planned), QByteArray{"synthetic-mp3"});
        WindowsDownloadOwnershipStore reader(journal_directory);
        const auto loaded = reader.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), std::size_t{1});
        const auto record = loaded.journal->records.front();
        QCOMPARE(record.stage,
                 fault == 1 ? DownloadOwnershipStage::PublicationPrepared : DownloadOwnershipStage::Published);
        QCOMPARE(record.output, fileIdentity(planned));
        QCOMPARE(WindowsDownloadOwnershipRecovery::assess(fixture.config(), record),
                 DownloadOwnershipAssessment::OwnedFinalCandidate);
        QCOMPARE(readFile(planned), QByteArray{"synthetic-mp3"});
    }

    void durableGatesCaptureActualIdentityBeforeSinksRenameAndImportableReturn() {
        Fixture fixture;
        OwnershipStore store;
        WindowsDownloadWorkspace workspace(fixture.config(), "job-1", store);
        bool planned_before_creation = false;
        bool ready_before_sinks = false;
        bool prepared_after_validation = false;
        bool published_before_return = false;
        bool cleanup_after_elimination = false;
        bool validated = false;
        store.on_record = [&](const DownloadOwnershipRecord& value) {
            if (value.cleanup_complete) {
                cleanup_after_elimination = jobs(fixture).isEmpty();
            } else if (value.stage == DownloadOwnershipStage::Planned) {
                planned_before_creation = jobs(fixture).isEmpty() && workspace.inputSink() == nullptr &&
                                          workspace.outputSink() == nullptr && !value.job_directory && !value.input &&
                                          !value.output;
            } else if (value.stage == DownloadOwnershipStage::WorkspaceReady) {
                const auto job = fixture.staging + "/rc-music-job-" + QString::fromStdString(value.workspace_id);
                ready_before_sinks = workspace.inputSink() == nullptr && workspace.outputSink() == nullptr &&
                                     value.job_directory == fileIdentity(job, true) &&
                                     value.input == fileIdentity(job + "/input.bin") &&
                                     value.output == fileIdentity(job + "/converted.part");
            } else if (value.stage == DownloadOwnershipStage::PublicationPrepared) {
                prepared_after_validation = validated && QFile::exists(workspace.outputPath()) &&
                                            !QFile::exists(workspace.plannedPublicationPath());
            } else if (value.stage == DownloadOwnershipStage::Published) {
                published_before_return = QFile::exists(workspace.plannedPublicationPath()) &&
                                          value.output == fileIdentity(workspace.plannedPublicationPath());
            }
            return DownloadCheckpointSaveStatus::Saved;
        };
        QVERIFY(store.attempts.empty());
        QVERIFY(workspace.create());
        QVERIFY(planned_before_creation);
        QVERIFY(ready_before_sinks);
        QCOMPARE(store.attempts.size(), std::size_t{2});
        const auto ready = store.attempts.back();
        QCOMPARE(ready.job_id, std::string{"job-1"});
        const auto staging_identity = fileIdentity(fixture.staging, true);
        const auto publish_identity = fileIdentity(fixture.publication, true);
        QVERIFY(staging_identity);
        QVERIFY(publish_identity);
        QCOMPARE(ready.staging_parent, *staging_identity);
        QCOMPARE(ready.publish_parent, *publish_identity);
        QVERIFY(fillAndSeal(workspace));
        const auto output_identity = ready.output;
        const auto final = workspace.validateAndPublish([&](const QString&) {
            validated = true;
            return true;
        });
        QCOMPARE(final, workspace.plannedPublicationPath());
        QVERIFY(!final.isEmpty());
        QVERIFY(prepared_after_validation);
        QVERIFY(published_before_return);
        QCOMPARE(store.attempts.size(), std::size_t{4});
        const auto prepared = store.attempts[2];
        QCOMPARE(prepared.stage, DownloadOwnershipStage::PublicationPrepared);
        QCOMPARE(prepared.input_bytes, std::uint64_t{5});
        QCOMPARE(prepared.output_bytes, std::uint64_t{13});
        QCOMPARE(prepared.output_sha256,
                 QCryptographicHash::hash("synthetic-mp3", QCryptographicHash::Sha256).toHex().toStdString());
        QCOMPARE(prepared.output, output_identity);
        QCOMPARE(store.attempts[3].output, output_identity);
        QCOMPARE(store.attempts[3].stage, DownloadOwnershipStage::Published);
        QVERIFY(workspace.cleanup());
        QVERIFY(cleanup_after_elimination);
        QVERIFY(store.attempts.back().cleanup_complete);
        QCOMPARE(store.attempts.back().stage, DownloadOwnershipStage::Published);
        QCOMPARE(readFile(final), QByteArray{"synthetic-mp3"});
        QCOMPARE(store.loads, 0);
    }

    void durableGateFailuresNeverYieldAnImportablePath_data() {
        QTest::addColumn<int>("stage");
        QTest::addColumn<int>("failure");
        for (const auto stage : {DownloadOwnershipStage::Planned, DownloadOwnershipStage::WorkspaceReady,
                                 DownloadOwnershipStage::PublicationPrepared, DownloadOwnershipStage::Published}) {
            for (int failure = 0; failure != 4; ++failure) {
                const auto name = QByteArray::number(static_cast<int>(stage)) + '-' + QByteArray::number(failure);
                QTest::newRow(name.constData()) << static_cast<int>(stage) << failure;
            }
        }
    }

    void durableGateFailuresNeverYieldAnImportablePath() {
        QFETCH(int, stage);
        QFETCH(int, failure);
        const auto failed_stage = static_cast<DownloadOwnershipStage>(stage);
        Fixture fixture;
        OwnershipStore store;
        WindowsDownloadWorkspace workspace(fixture.config(), "job-1", store);
        store.on_record = [&](const DownloadOwnershipRecord& value) {
            if (value.stage != failed_stage || value.cleanup_complete)
                return DownloadCheckpointSaveStatus::Saved;
            if (failure == 3)
                throw std::runtime_error("private synthetic store failure");
            return failure == 0   ? DownloadCheckpointSaveStatus::Failed
                   : failure == 1 ? DownloadCheckpointSaveStatus::BlockedByExistingData
                                  : static_cast<DownloadCheckpointSaveStatus>(99);
        };
        const bool created = workspace.create();
        if (failed_stage == DownloadOwnershipStage::Planned || failed_stage == DownloadOwnershipStage::WorkspaceReady) {
            QVERIFY(!created);
            QVERIFY(workspace.inputSink() == nullptr);
            QVERIFY(workspace.outputSink() == nullptr);
        } else {
            QVERIFY(created);
            QVERIFY(fillAndSeal(workspace));
            QVERIFY(workspace.validateAndPublish([](const QString&) { return true; }).isEmpty());
        }
        QCOMPARE(workspace.error(), DownloadFailure::Publication);
        const auto planned = workspace.plannedPublicationPath();
        const auto retained = workspace.retainedPublicationPath();
        if (failed_stage == DownloadOwnershipStage::Published) {
            QCOMPARE(retained, planned);
            QVERIFY(!retained.isEmpty());
            QCOMPARE(readFile(retained), QByteArray{"synthetic-mp3"});
        } else {
            QVERIFY(retained.isEmpty());
            QVERIFY(planned.isEmpty() || !QFile::exists(planned));
        }
        static_cast<void>(workspace.cleanup());
        QVERIFY(jobs(fixture).isEmpty());
        if (!retained.isEmpty()) {
            QCOMPARE(workspace.retainedPublicationPath(), retained);
            QCOMPARE(readFile(retained), QByteArray{"synthetic-mp3"});
        }
        QCOMPARE(store.loads, 0);
    }

    void durableInvalidJobIdsNeverTouchStoreOrFilesystem_data() {
        QTest::addColumn<QByteArray>("id");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("path") << QByteArray{"../other"};
        QTest::newRow("control") << QByteArray{"job\n"};
        QTest::newRow("long") << QByteArray(65, 'a');
    }
    void durableInvalidJobIdsNeverTouchStoreOrFilesystem() {
        QFETCH(QByteArray, id);
        Fixture fixture;
        OwnershipStore store;
        WindowsDownloadWorkspace workspace(fixture.config(), id.toStdString(), store);
        QVERIFY(!workspace.create());
        QCOMPARE(workspace.error(), DownloadFailure::InvalidResult);
        QVERIFY(store.attempts.empty());
        QVERIFY(jobs(fixture).isEmpty());
    }

    void durableReentrantCleanupCannotExposeSinksOrPublish_data() {
        QTest::addColumn<int>("stage");
        for (const auto stage : {DownloadOwnershipStage::Planned, DownloadOwnershipStage::WorkspaceReady,
                                 DownloadOwnershipStage::PublicationPrepared, DownloadOwnershipStage::Published})
            QTest::newRow(QByteArray::number(static_cast<int>(stage)).constData()) << static_cast<int>(stage);
    }
    void durableReentrantCleanupCannotExposeSinksOrPublish() {
        QFETCH(int, stage);
        Fixture fixture;
        OwnershipStore store;
        WindowsDownloadWorkspace workspace(fixture.config(), "job-1", store);
        bool interrupted = false;
        store.on_record = [&](const DownloadOwnershipRecord& value) {
            if (!interrupted && static_cast<int>(value.stage) == stage && !value.cleanup_complete) {
                interrupted = true;
                static_cast<void>(workspace.cleanup());
            }
            return DownloadCheckpointSaveStatus::Saved;
        };
        const auto created = workspace.create();
        if (stage <= static_cast<int>(DownloadOwnershipStage::WorkspaceReady)) {
            QVERIFY(!created);
            QVERIFY(workspace.inputSink() == nullptr);
            QVERIFY(workspace.outputSink() == nullptr);
        } else {
            QVERIFY(created);
            QVERIFY(fillAndSeal(workspace));
            QVERIFY(workspace.validateAndPublish([](const QString&) { return true; }).isEmpty());
        }
        QVERIFY(interrupted);
        static_cast<void>(workspace.cleanup());
        QCOMPARE(store.maximum_depth, 1);
        QVERIFY(jobs(fixture).isEmpty());
        if (stage == static_cast<int>(DownloadOwnershipStage::Published))
            QCOMPARE(readFile(workspace.retainedPublicationPath()), QByteArray{"synthetic-mp3"});
        else
            QVERIFY(QDir(fixture.publication).entryList(QDir::Files).isEmpty());
    }

    void durableCommitGuardIsRecheckedAfterPreparedRecord() {
        Fixture fixture;
        OwnershipStore store;
        WindowsDownloadWorkspace workspace(fixture.config(), "job-1", store);
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        bool allowed = true;
        store.on_record = [&](const DownloadOwnershipRecord& value) {
            if (value.stage == DownloadOwnershipStage::PublicationPrepared)
                allowed = false;
            return DownloadCheckpointSaveStatus::Saved;
        };
        QVERIFY(workspace.validateAndPublish([](const QString&) { return true; }, [&] { return allowed; }).isEmpty());
        QVERIFY(!QFile::exists(workspace.plannedPublicationPath()));
        QVERIFY(workspace.retainedPublicationPath().isEmpty());
        QCOMPARE(store.attempts.back().stage, DownloadOwnershipStage::PublicationPrepared);
        QVERIFY(workspace.cleanup());
        QVERIFY(store.attempts.back().cleanup_complete);
    }

    void constructorDoesNotCreateOrPublishFiles() {
        Fixture fixture;
        QVERIFY(fixture.root.isValid());
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.inputSink() == nullptr);
        QVERIFY(workspace.outputSink() == nullptr);
        QVERIFY(workspace.inputPath().isEmpty());
        QVERIFY(workspace.outputPath().isEmpty());
        QVERIFY(workspace.plannedPublicationPath().isEmpty());
        QVERIFY(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        QVERIFY(QDir(fixture.publication).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void createsFreshOwnedSinksAndPublishesOnlyAfterSealingAndValidation() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(!workspace.create());
        QVERIFY(workspace.inputSink());
        QVERIFY(workspace.outputSink());
        QVERIFY(!workspace.inputSink()->isSequential());
        QVERIFY(workspace.inputSink()->isWritable());
        QCOMPARE(workspace.inputSink()->size(), qint64(0));
        QCOMPARE(workspace.inputSink()->pos(), qint64(0));
        QVERIFY(workspace.inputPath().isEmpty());
        QVERIFY(workspace.outputPath().isEmpty());
        const auto planned = workspace.plannedPublicationPath();
        QVERIFY(planned.startsWith(fixture.publication + QLatin1Char('/')));
        QVERIFY(planned.endsWith(QStringLiteral(".mp3")));
        QVERIFY(!QFile::exists(planned));
        QVERIFY(fillAndSeal(workspace));
        QCOMPARE(QFileInfo(workspace.inputPath()).fileName(), QStringLiteral("input.bin"));
        QCOMPARE(QFileInfo(workspace.outputPath()).fileName(), QStringLiteral("converted.part"));
        QCOMPARE(readFile(workspace.inputPath()), QByteArray("input"));
        QVERIFY(!workspace.inputSink()->isWritable());
        QVERIFY(!workspace.outputSink()->isWritable());
        int validations = 0;
        const auto final = workspace.validateAndPublish([&](const QString& sealed_path) {
            ++validations;
            return sealed_path == workspace.outputPath() && readFile(sealed_path) == "synthetic-mp3" &&
                   !QFile::exists(planned);
        });
        const auto native_error = GetLastError();
        QCOMPARE(validations, 1);
        const auto publication_failure = QStringLiteral("Publication refused: failure=%1 Win32=%2")
                                             .arg(static_cast<int>(workspace.error()))
                                             .arg(native_error);
        QVERIFY2(!final.isEmpty(), qPrintable(publication_failure));
        QCOMPARE(final, planned);
        QCOMPARE(workspace.error(), DownloadFailure::None);
        QCOMPARE(readFile(final), QByteArray("synthetic-mp3"));
        QVERIFY(workspace.cleanup());
        QCOMPARE(readFile(final), QByteArray("synthetic-mp3"));
        QVERIFY(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void sharesExactQuotaBetweenInputAndOutput() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config(8));
        QVERIFY(workspace.create());
        QCOMPARE(workspace.inputSink()->write("12345", 5), qint64(5));
        QVERIFY(workspace.sealInput());
        QCOMPARE(workspace.outputSink()->write("678", 3), qint64(3));
        QCOMPARE(workspace.inputSink()->size() + workspace.outputSink()->size(), qint64(8));
        QCOMPARE(workspace.outputSink()->write("9", 1), qint64(-1));
        QCOMPARE(workspace.outputSink()->size(), qint64(3));
        QCOMPARE(workspace.error(), DownloadFailure::LimitExceeded);
        QVERIFY(workspace.validateAndPublish([](const QString&) { return true; }).isEmpty());
        QVERIFY(workspace.cleanup());
    }

    void rejectsOversizedWriteBeforeChangingFile() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config(4));
        QVERIFY(workspace.create());
        QCOMPARE(workspace.inputSink()->write("12345", 5), qint64(-1));
        QCOMPARE(workspace.inputSink()->size(), qint64(0));
        QCOMPARE(workspace.inputSink()->pos(), qint64(0));
        QCOMPARE(workspace.error(), DownloadFailure::LimitExceeded);
    }

    void collisionPreservesExistingFinalAndDeletesOnlyOwnedPartials() {
        Fixture fixture;
        QString planned;
        {
            WindowsDownloadWorkspace workspace(fixture.config());
            QVERIFY(workspace.create());
            QVERIFY(fillAndSeal(workspace));
            planned = workspace.plannedPublicationPath();
            QVERIFY(writeFile(planned, "existing-user-media"));
            QVERIFY(workspace.validateAndPublish([](const QString&) { return true; }).isEmpty());
            QCOMPARE(workspace.error(), DownloadFailure::Publication);
            QCOMPARE(readFile(planned), QByteArray("existing-user-media"));
        }
        QCOMPARE(readFile(planned), QByteArray("existing-user-media"));
        QVERIFY(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void refusesMissingRejectedAndThrowingValidation_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("missing") << 0;
        QTest::newRow("rejected") << 1;
        QTest::newRow("throwing") << 2;
    }

    void refusesMissingRejectedAndThrowingValidation() {
        QFETCH(int, kind);
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        std::function<bool(const QString&)> validate;
        if (kind == 1) {
            validate = [](const QString&) { return false; };
        } else if (kind == 2) {
            validate = [](const QString&) -> bool { throw std::runtime_error("synthetic failure"); };
        }
        QVERIFY(workspace.validateAndPublish(validate).isEmpty());
        QVERIFY(workspace.error() != DownloadFailure::None);
        QVERIFY(!QFile::exists(workspace.plannedPublicationPath()));
    }

    void refusesUnsealedOrEmptyOutput() {
        Fixture fixture;
        WindowsDownloadWorkspace unsealed(fixture.config());
        QVERIFY(unsealed.create());
        QCOMPARE(unsealed.outputSink()->write("partial", 7), qint64(7));
        int validations = 0;
        QVERIFY(unsealed
                    .validateAndPublish([&](const QString&) {
                        ++validations;
                        return true;
                    })
                    .isEmpty());
        QCOMPARE(validations, 0);
        WindowsDownloadWorkspace empty(fixture.config());
        QVERIFY(empty.create());
        QCOMPARE(empty.inputSink()->write("input", 5), qint64(5));
        QVERIFY(empty.sealInput());
        QVERIFY(!empty.sealOutput());
        QVERIFY(empty
                    .validateAndPublish([&](const QString&) {
                        ++validations;
                        return true;
                    })
                    .isEmpty());
        QCOMPARE(validations, 0);
    }

    void cleanupDuringValidationCannotBecomePublication() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        const auto planned = workspace.plannedPublicationPath();
        bool cleaned = false;
        int validations = 0;
        const auto published = workspace.validateAndPublish([&](const QString&) {
            ++validations;
            cleaned = workspace.cleanup();
            return true;
        });
        QVERIFY(cleaned);
        QCOMPARE(validations, 1);
        QVERIFY(published.isEmpty());
        QVERIFY(!QFile::exists(planned));
        QVERIFY(workspace.inputSink() == nullptr);
        QVERIFY(workspace.outputSink() == nullptr);
        QVERIFY(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void recursivePublicationDoesNotRunValidationTwice() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        int nested_validations = 0;
        QString nested_result;
        const auto published = workspace.validateAndPublish([&](const QString&) {
            nested_result = workspace.validateAndPublish([&](const QString&) {
                ++nested_validations;
                return true;
            });
            return true;
        });
        QVERIFY(nested_result.isEmpty());
        QCOMPARE(nested_validations, 0);
        QCOMPARE(published, workspace.plannedPublicationPath());
        QCOMPARE(readFile(published), QByteArray("synthetic-mp3"));
    }

    void refusesDestinationJunctionIntroducedByValidation() {
        Fixture fixture;
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        bool changed = false;
        QString published;
        QString planned;
        DownloadFailure failure = DownloadFailure::None;
        {
            WindowsDownloadWorkspace workspace(fixture.config());
            QVERIFY(workspace.create());
            QVERIFY(fillAndSeal(workspace));
            planned = workspace.plannedPublicationPath();
            published = workspace.validateAndPublish([&](const QString&) {
                changed = setSyntheticJunction(fixture.publication, outside.path());
                return true;
            });
            failure = workspace.error();
        }
        if (!changed) {
            QSKIP("Host filesystem did not permit this synthetic destination junction.");
        }
        QVERIFY(RemoveDirectoryW(nativePath(fixture.publication).c_str()));
        QVERIFY(published.isEmpty());
        QVERIFY(failure != DownloadFailure::None);
        QVERIFY(!QFile::exists(planned));
        QVERIFY(QDir(outside.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        QVERIFY(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void cancellationCleanupPreservesNeighbours() {
        Fixture fixture;
        QVERIFY(writeFile(fixture.staging + QStringLiteral("/keep.bin"), "keep-staging"));
        QVERIFY(writeFile(fixture.publication + QStringLiteral("/keep.mp3"), "keep-final"));
        {
            WindowsDownloadWorkspace workspace(fixture.config());
            QVERIFY(workspace.create());
            QCOMPARE(workspace.inputSink()->write("partial", 7), qint64(7));
            QCOMPARE(workspace.outputSink()->write("partial", 7), qint64(7));
            QVERIFY(workspace.cleanup());
            QVERIFY(workspace.cleanup());
            QVERIFY(workspace.inputSink() == nullptr);
            QVERIFY(workspace.outputSink() == nullptr);
        }
        QCOMPARE(readFile(fixture.staging + QStringLiteral("/keep.bin")), QByteArray("keep-staging"));
        QCOMPARE(readFile(fixture.publication + QStringLiteral("/keep.mp3")), QByteArray("keep-final"));
        QCOMPARE(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries),
                 QStringList{QStringLiteral("keep.bin")});
    }

    void cleanupLeavesUnknownJobEntryAndReportsIncompleteRemoval() {
        Fixture fixture;
        QString unknown;
        QString input;
        QString output;
        {
            WindowsDownloadWorkspace workspace(fixture.config());
            QVERIFY(workspace.create());
            QVERIFY(fillAndSeal(workspace));
            input = workspace.inputPath();
            output = workspace.outputPath();
            const auto job_directory = QFileInfo(input).absolutePath();
            unknown = job_directory + QStringLiteral("/unknown.txt");
            QVERIFY(writeFile(unknown, "keep-unknown"));
            QVERIFY(!workspace.cleanup());
            QCOMPARE(workspace.error(), DownloadFailure::Publication);
            QVERIFY(!QFile::exists(input));
            QVERIFY(!QFile::exists(output));
            QCOMPARE(readFile(unknown), QByteArray("keep-unknown"));
            QCOMPARE(QDir(job_directory).entryList(QDir::NoDotAndDotDot | QDir::AllEntries),
                     QStringList{QStringLiteral("unknown.txt")});
        }
        QCOMPARE(readFile(unknown), QByteArray("keep-unknown"));
    }

    void invalidConfigurationDoesNotCreateParents_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 9; ++kind) {
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
        }
    }

    void invalidConfigurationDoesNotCreateParents() {
        QFETCH(int, kind);
        Fixture fixture;
        auto config = fixture.config();
        switch (kind) {
        case 0:
            config.staging_parent.clear();
            break;
        case 1:
            config.staging_parent = QStringLiteral("relative");
            break;
        case 2:
            config.staging_parent += QStringLiteral("/missing");
            break;
        case 3:
            config.publish_parent += QStringLiteral("/missing");
            break;
        case 4:
            config.staging_parent += QStringLiteral("/../alias");
            break;
        case 5:
            config.max_workspace_bytes = 0;
            break;
        case 6:
            config.max_workspace_bytes = 512ULL * 1024 * 1024 + 1;
            break;
        case 7:
            config.staging_parent += QStringLiteral(":stream");
            break;
        case 8:
            config.staging_parent = QStringLiteral("\\\\.\\C:\\");
            break;
        }
        WindowsDownloadWorkspace workspace(config);
        QVERIFY(!workspace.create());
        QCOMPARE(workspace.error(), DownloadFailure::InvalidResult);
        QVERIFY(workspace.inputSink() == nullptr);
        QVERIFY(QDir(fixture.staging).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        QVERIFY(QDir(fixture.publication).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void pinsParentsAgainstRenameAndFilesAgainstReplacement() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QVERIFY(fillAndSeal(workspace));
        const auto renamed_parent = fixture.root.filePath(QStringLiteral("renamed"));
        QVERIFY(!MoveFileExW(nativePath(fixture.staging).c_str(), nativePath(renamed_parent).c_str(), 0));
        const auto job_directory = QFileInfo(workspace.inputPath()).absolutePath();
        const auto renamed_job = fixture.staging + QStringLiteral("/renamed-job");
        QVERIFY(!MoveFileExW(nativePath(job_directory).c_str(), nativePath(renamed_job).c_str(), 0));
        QVERIFY(!DeleteFileW(nativePath(workspace.inputPath()).c_str()));
        QVERIFY(!DeleteFileW(nativePath(workspace.outputPath()).c_str()));
        QVERIFY(!writeFile(workspace.inputPath(), "replace"));
        QVERIFY(!writeFile(workspace.outputPath(), "replace"));
        QCOMPARE(readFile(workspace.inputPath()), QByteArray("input"));
        QCOMPARE(readFile(workspace.outputPath()), QByteArray("synthetic-mp3"));
    }

    void freshJobDirectoryUsesProtectedPrivateDacl() {
        Fixture fixture;
        WindowsDownloadWorkspace workspace(fixture.config());
        QVERIFY(workspace.create());
        QCOMPARE(workspace.inputSink()->write("input", 5), qint64(5));
        QVERIFY(workspace.sealInput());
        const auto directory = QFileInfo(workspace.inputPath()).absolutePath();
        HANDLE handle = CreateFileW(nativePath(directory).c_str(), READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PACL dacl = nullptr;
        PSID owner = nullptr;
        const DWORD result =
            GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                            nullptr, &dacl, nullptr, &descriptor);
        CloseHandle(handle);
        QCOMPARE(result, DWORD(ERROR_SUCCESS));
        QVERIFY(descriptor);
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        const bool queried = GetSecurityDescriptorControl(descriptor, &control, &revision) != FALSE;
        bool present = dacl != nullptr && dacl->AceCount == 2;
        HANDLE token = nullptr;
        bool user_found = false;
        bool system_found = false;
        bool correct_owner = false;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            DWORD required = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &required);
            std::vector<std::byte> user(required);
            if (required > 0 && GetTokenInformation(token, TokenUser, user.data(), required, &required) && present) {
                const auto* token_user = reinterpret_cast<const TOKEN_USER*>(user.data());
                correct_owner = owner && EqualSid(owner, token_user->User.Sid);
                for (DWORD i = 0; i < dacl->AceCount; ++i) {
                    void* raw_ace = nullptr;
                    if (!GetAce(dacl, i, &raw_ace)) {
                        present = false;
                        break;
                    }
                    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
                    const DWORD inheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
                    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Mask != FILE_ALL_ACCESS ||
                        (ace->Header.AceFlags & inheritance) != inheritance) {
                        present = false;
                        break;
                    }
                    const auto sid = const_cast<DWORD*>(&ace->SidStart);
                    user_found = user_found || EqualSid(sid, token_user->User.Sid);
                    system_found = system_found || IsWellKnownSid(sid, WinLocalSystemSid);
                }
            }
            CloseHandle(token);
        }
        LocalFree(descriptor);
        QVERIFY(queried);
        QVERIFY(control & SE_DACL_PROTECTED);
        QVERIFY(present);
        QVERIFY(user_found);
        QVERIFY(system_found);
        QVERIFY(correct_owner);
    }

    void refusesSyntheticSymlinkAtParent_data() {
        QTest::addColumn<bool>("staging");
        QTest::newRow("staging") << true;
        QTest::newRow("publication") << false;
    }

    void refusesSyntheticSymlinkAtParent() {
        QFETCH(bool, staging);
        Fixture fixture;
        QTemporaryDir outside;
        const auto link = fixture.root.filePath(QStringLiteral("parent-link"));
        if (!CreateSymbolicLinkW(nativePath(link).c_str(), nativePath(outside.path()).c_str(),
                                 SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            QSKIP("Host policy does not permit this synthetic symbolic link.");
        }
        auto config = fixture.config();
        (staging ? config.staging_parent : config.publish_parent) = link;
        WindowsDownloadWorkspace workspace(config);
        const bool created = workspace.create();
        QVERIFY(RemoveDirectoryW(nativePath(link).c_str()));
        QVERIFY(!created);
        QVERIFY(QDir(outside.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }
};

} // namespace music_player::m0

QTEST_GUILESS_MAIN(music_player::m0::WindowsDownloadWorkspaceTests)
#include "WindowsDownloadWorkspaceTests.moc"
