#include "adapters/WindowsDownloadOwnershipRecovery.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cstring>
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
} // namespace

class WindowsDownloadOwnershipRecoveryTests final : public QObject {
    Q_OBJECT

  private slots:
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
