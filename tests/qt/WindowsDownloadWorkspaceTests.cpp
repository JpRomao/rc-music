#include "adapters/WindowsDownloadWorkspace.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Aclapi.h>
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

} // namespace

class WindowsDownloadWorkspaceTests final : public QObject {
    Q_OBJECT

  private slots:
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
