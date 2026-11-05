#include "adapters/WindowsYoutubeRuntime.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
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

#include <array>

using namespace music_player::m0;
namespace {
using Role = WindowsYoutubeRuntimeRole;
using Error = WindowsYoutubeRuntimeError;
constexpr quint64 maximum_artifact_bytes = 128ULL * 1024 * 1024;
const std::array<Role, 4> required_roles{Role::Extractor, Role::Node, Role::EjsCore, Role::EjsLibrary};
const std::array<QString, 6> relative_paths{QStringLiteral("extractor/yt-dlp.exe"),
                                            QStringLiteral("javascript/node.exe"),
                                            QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js"),
                                            QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"),
                                            QStringLiteral("extractor/_internal/support/dependency.bin"),
                                            QStringLiteral("notices/LICENSE.txt")};

QByteArray readFile(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QByteArray fileHash(const QString& path) {
    return QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex();
}
struct OwnedHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~OwnedHandle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
bool differentFileIdentities(const QString& source, const QString& destination) {
    const auto open = [](const QString& path) {
        return CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    };
    const OwnedHandle first{open(source)}, second{open(destination)};
    FILE_ID_INFO left{}, right{};
    return first.value != INVALID_HANDLE_VALUE && second.value != INVALID_HANDLE_VALUE &&
           GetFileInformationByHandleEx(first.value, FileIdInfo, &left, sizeof(left)) &&
           GetFileInformationByHandleEx(second.value, FileIdInfo, &right, sizeof(right)) &&
           (left.VolumeSerialNumber != right.VolumeSerialNumber ||
            QByteArray(reinterpret_cast<const char*>(left.FileId.Identifier), sizeof(left.FileId.Identifier)) !=
                QByteArray(reinterpret_cast<const char*>(right.FileId.Identifier), sizeof(right.FileId.Identifier)));
}
bool sparseFile(const QString& path, const quint64 bytes) {
    const OwnedHandle handle{CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(),
                                         GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                         FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned{};
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(bytes);
    return DeviceIoControl(handle.value, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) &&
           SetFilePointerEx(handle.value, end, nullptr, FILE_BEGIN) && SetEndOfFile(handle.value);
}
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music YouTube inventário 日本-XXXXXX")};
    QString sources{parent.filePath(QStringLiteral("source originals com espaços"))};
    QString runtime{parent.filePath(QStringLiteral("private runtimes 日本"))};
    WindowsYoutubeRuntimeConfig config{};
    bool initialize() {
        if (!parent.isValid() || !QDir().mkdir(sources) || !QDir().mkdir(runtime))
            return false;
        config.runtime_parent = runtime;
        for (std::size_t index = 0; index < relative_paths.size(); ++index) {
            const auto source = sources + QStringLiteral("/synthetic-%1.bin").arg(index);
            const auto bytes = QByteArray("synthetic inventory bytes, not executable: ") + QByteArray::number(index);
            if (!writeFile(source, bytes))
                return false;
            config.artifacts.push_back({index < required_roles.size() ? required_roles[index] : Role::Support,
                                        relative_paths[index], source, fileHash(source)});
        }
        return true;
    }
    QStringList runtimes() const { return QDir(runtime).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
    void verifySources() const {
        for (std::size_t index = 0; index < config.artifacts.size(); ++index) {
            QCOMPARE(readFile(config.artifacts[index].source_path),
                     QByteArray("synthetic inventory bytes, not executable: ") + QByteArray::number(index));
            QCOMPARE(fileHash(config.artifacts[index].source_path), config.artifacts[index].sha256);
        }
    }
    WindowsYoutubeRuntimeSource support(const QString& relative) const {
        const auto& source = config.artifacts.back();
        return {Role::Support, relative, source.source_path, source.sha256};
    }
};
QString deepRelative(const qsizetype segments) {
    QStringList parts{QStringLiteral("notices")};
    while (parts.size() < segments - 1)
        parts.push_back(QStringLiteral("level%1").arg(parts.size()));
    parts.push_back(QStringLiteral("synthetic.txt"));
    return parts.join('/');
}
QString longRelative(const qsizetype units) {
    QStringList parts{QStringLiteral("notices")};
    for (int index = 0; index < 14; ++index)
        parts.push_back(QString(64, QLatin1Char('a')));
    const auto prefix = parts.join('/') + '/';
    return prefix + QString(units - prefix.size(), QLatin1Char('b'));
}
} // namespace

class WindowsYoutubeRuntimeTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingConfigurationCannotCreateAnInventory() {
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare({}, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
    }
    void requiresEveryExplicitRole_data() {
        QTest::addColumn<int>("index");
        for (int index = 0; index < 4; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void requiresEveryExplicitRole() {
        QFETCH(int, index);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        config.artifacts.erase(config.artifacts.begin() + index);
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void requiredRolesCannotBeReassignedToOtherDestinations_data() { requiresEveryExplicitRole_data(); }
    void requiredRolesCannotBeReassignedToOtherDestinations() {
        QFETCH(int, index);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        config.artifacts[static_cast<std::size_t>(index)].relative_path = QStringLiteral("notices/reassigned.bin");
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void duplicateOrUnknownRolesAreInvalid_data() {
        QTest::addColumn<bool>("unknown");
        QTest::newRow("duplicate unique role") << false;
        QTest::newRow("unknown role") << true;
    }
    void duplicateOrUnknownRolesAreInvalid() {
        QFETCH(bool, unknown);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        auto extra = h.support(QStringLiteral("notices/extra.bin"));
        extra.role = unknown ? static_cast<Role>(99) : Role::Extractor;
        config.artifacts.push_back(extra);
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void malformedParentsAndTrustAreRejectedBeforeCreatingDirectories_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode < 7; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void malformedParentsAndTrustAreRejectedBeforeCreatingDirectories() {
        QFETCH(int, mode);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        switch (mode) {
        case 0:
            config.runtime_parent.clear();
            break;
        case 1:
            config.runtime_parent = QStringLiteral("relative/runtimes");
            break;
        case 2:
            config.runtime_parent = QStringLiteral("//server/share/runtimes");
            break;
        case 3:
            config.artifacts.front().source_path = QStringLiteral("relative.bin");
            break;
        case 4:
            config.artifacts.front().sha256.clear();
            break;
        case 5:
            config.artifacts.front().sha256 = QByteArray(63, 'a');
            break;
        case 6:
            config.artifacts.front().sha256 = QByteArray(64, 'A');
            break;
        }
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void rejectsMalformedSupportDestinations_data() {
        QTest::addColumn<QString>("path");
        for (const auto* path : {"",
                                 "/notices/a",
                                 "C:/notices/a",
                                 "../notices/a",
                                 "notices",
                                 "notices/",
                                 "notices//a",
                                 "notices/./a",
                                 "notices/../a",
                                 "notices/a\\b",
                                 "notices/a.",
                                 "notices/a ",
                                 "notices/a:b",
                                 "notices/a?b",
                                 "notices/a|b",
                                 "notices/CON.txt",
                                 "notices/prn",
                                 "notices/AUX.bin",
                                 "notices/NUL",
                                 "notices/COM1.txt",
                                 "notices/lpt9",
                                 "javascript/extra.exe",
                                 "extractor/extra.exe",
                                 "other/data.bin"})
            QTest::newRow(path) << QString::fromLatin1(path);
        QTest::newRow("NUL") << QStringLiteral("notices/a") + QChar{0} + QStringLiteral("b");
        QTest::newRow("control") << QStringLiteral("notices/a\nb");
        QTest::newRow("malformed UTF16") << QStringLiteral("notices/a") + QChar{0xd800};
    }
    void rejectsMalformedSupportDestinations() {
        QFETCH(QString, path);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        config.artifacts.push_back(h.support(path));
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void rejectsCaseAliasesAndFileDirectoryCollisions_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("required alias") << 0;
        QTest::newRow("support alias") << 1;
        QTest::newRow("file is directory prefix") << 2;
        QTest::newRow("reverse file prefix") << 3;
        QTest::newRow("required file prefix") << 4;
    }
    void rejectsCaseAliasesAndFileDirectoryCollisions() {
        QFETCH(int, mode);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        if (mode == 0)
            config.artifacts.push_back(
                h.support(QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/CORE.MIN.JS")));
        else if (mode == 1)
            config.artifacts.push_back(h.support(QStringLiteral("notices/license.TXT")));
        else if (mode == 2) {
            config.artifacts.push_back(h.support(QStringLiteral("notices/package")));
            config.artifacts.push_back(h.support(QStringLiteral("notices/PACKAGE/data.bin")));
        } else if (mode == 3) {
            config.artifacts.push_back(h.support(QStringLiteral("notices/PACKAGE/data.bin")));
            config.artifacts.push_back(h.support(QStringLiteral("notices/package")));
        } else
            config.artifacts.push_back(h.support(relative_paths[2] + QStringLiteral("/data.bin")));
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void hashMismatchInAnyRequiredOrSupportArtifactPrecedesPrivateCopy_data() {
        QTest::addColumn<int>("index");
        for (int index = 0; index < static_cast<int>(relative_paths.size()); ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void hashMismatchInAnyRequiredOrSupportArtifactPrecedesPrivateCopy() {
        QFETCH(int, index);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        config.artifacts[static_cast<std::size_t>(index)].sha256 = QByteArray(64, '0');
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::HashMismatch);
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void missingAnyRequiredOrSupportSourcePrecedesPrivateCopy_data() {
        hashMismatchInAnyRequiredOrSupportArtifactPrecedesPrivateCopy_data();
    }
    void missingAnyRequiredOrSupportSourcePrecedesPrivateCopy() {
        QFETCH(int, index);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        config.artifacts[static_cast<std::size_t>(index)].source_path = h.sources + QStringLiteral("/missing.bin");
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void inventoryQuotasAreCheckedBeforeCreatingPrivateDirectories_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("257 files") << 0;
        QTest::newRow("17 segments") << 1;
        QTest::newRow("1025 relative units") << 2;
    }
    void inventoryQuotasAreCheckedBeforeCreatingPrivateDirectories() {
        QFETCH(int, mode);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        if (mode == 0) {
            while (config.artifacts.size() <= youtube_runtime_max_files)
                config.artifacts.push_back(
                    h.support(QStringLiteral("notices/file-%1.bin").arg(config.artifacts.size())));
        } else if (mode == 1)
            config.artifacts.push_back(h.support(deepRelative(youtube_runtime_max_segments + 1)));
        else
            config.artifacts.push_back(h.support(longRelative(youtube_runtime_max_relative_units + 1)));
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::LimitExceeded);
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void byteQuotasArePreflightedWithoutHashingOrCopyingSparseSources_data() {
        QTest::addColumn<bool>("aggregate");
        QTest::newRow("individual exceeds 128 MiB") << false;
        QTest::newRow("aggregate exceeds 512 MiB") << true;
    }
    void byteQuotasArePreflightedWithoutHashingOrCopyingSparseSources() {
        QFETCH(bool, aggregate);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        config.artifacts.resize(aggregate ? 5 : 4);
        for (std::size_t index = 0; index < (aggregate ? 4U : 1U); ++index) {
            const auto source = h.sources + QStringLiteral("/sparse-%1.bin").arg(index);
            QVERIFY(sparseFile(source, maximum_artifact_bytes + (aggregate ? 0 : 1)));
            config.artifacts[index].source_path = source;
            // Deliberately not the hash of the sparse data: limits must win
            // during metadata preflight, before any potentially large read.
            config.artifacts[index].sha256 = QByteArray(64, '0');
        }
        if (aggregate) {
            const auto source = h.sources + QStringLiteral("/one-byte.bin");
            QVERIFY(writeFile(source, QByteArray{"x"}));
            config.artifacts[4].source_path = source;
            config.artifacts[4].sha256 = fileHash(source);
        }
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(config, &error));
        QCOMPARE(error, Error::LimitExceeded);
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void preparesOnlyOwnedByteIdenticalCopiesWithExplicitRolePaths() {
        Fixture h;
        QVERIFY(h.initialize());
        Error error{Error::InvalidConfiguration};
        auto lease = WindowsYoutubeRuntime::prepare(h.config, &error);
        QVERIFY(lease);
        QCOMPARE(error, Error::None);
        QVERIFY(lease->unchanged());
        const auto directory = lease->directory();
        QVERIFY(directory.startsWith(h.runtime + '/'));
        QVERIFY(QDir(directory).exists());
        QStringList expected, actual;
        for (std::size_t index = 0; index < h.config.artifacts.size(); ++index) {
            const auto& source = h.config.artifacts[index];
            const auto destination = directory + '/' + source.relative_path;
            QCOMPARE(readFile(destination), readFile(source.source_path));
            QCOMPARE(fileHash(destination), source.sha256);
            QVERIFY(differentFileIdentities(source.source_path, destination));
            if (index < required_roles.size())
                QCOMPARE(lease->artifactPath(required_roles[index]), destination);
            expected.push_back(source.relative_path);
        }
        QDirIterator files(directory, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (files.hasNext())
            actual.push_back(QDir(directory).relativeFilePath(files.next()));
        expected.sort();
        actual.sort();
        QCOMPARE(actual, expected);
        QVERIFY(lease->artifactPath(Role::Support).isEmpty());
        QVERIFY(lease->artifactPath(static_cast<Role>(99)).isEmpty());
        h.verifySources();
        lease.reset();
        QVERIFY(!QDir(directory).exists());
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void exactInventoryQuotasAcceptSmallSyntheticFiles_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("256 files") << 0;
        QTest::newRow("16 segments") << 1;
        QTest::newRow("1024 relative units") << 2;
    }
    void exactInventoryQuotasAcceptSmallSyntheticFiles() {
        QFETCH(int, mode);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config;
        if (mode == 0) {
            while (config.artifacts.size() < youtube_runtime_max_files)
                config.artifacts.push_back(
                    h.support(QStringLiteral("notices/file-%1.bin").arg(config.artifacts.size())));
        } else if (mode == 1)
            config.artifacts.push_back(h.support(deepRelative(youtube_runtime_max_segments)));
        else
            config.artifacts.push_back(h.support(longRelative(youtube_runtime_max_relative_units)));
        auto lease = WindowsYoutubeRuntime::prepare(config);
        QVERIFY(lease);
        QVERIFY(lease->unchanged());
        const auto directory = lease->directory();
        for (const auto& source : config.artifacts)
            QCOMPARE(fileHash(directory + '/' + source.relative_path), source.sha256);
        lease.reset();
        QVERIFY(!QDir(directory).exists());
        h.verifySources();
    }
    void runtimeCopiesDoNotBorrowMutableSourceFiles() {
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsYoutubeRuntime::prepare(h.config);
        QVERIFY(lease);
        const auto original = readFile(h.config.artifacts.front().source_path);
        QFile source{h.config.artifacts.front().source_path};
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray replacement{"changed original fixture after private copy"};
        QCOMPARE(source.write(replacement), replacement.size());
        source.close();
        QVERIFY(lease->unchanged());
        QCOMPARE(readFile(lease->artifactPath(Role::Extractor)), original);
        QCOMPARE(readFile(h.config.artifacts.front().source_path), replacement);
        QVERIFY(QFile::rename(source.fileName(), source.fileName() + QStringLiteral(".previous")));
        QVERIFY(writeFile(source.fileName(), QByteArray{"replacement source fixture"}));
        QVERIFY(lease->unchanged());
        QCOMPARE(readFile(lease->artifactPath(Role::Extractor)), original);
    }
    void aSourceWithAnExistingWriterCannotBeAuthenticated() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto source = h.config.artifacts[2].source_path;
        const OwnedHandle writer{CreateFileW(QDir::toNativeSeparators(source).toStdWString().c_str(), GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        QVERIFY(writer.value != INVALID_HANDLE_VALUE);
        Error error{Error::None};
        QVERIFY(!WindowsYoutubeRuntime::prepare(h.config, &error));
        QCOMPARE(error, Error::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
    }
    void heldRuntimePinsDenyWritesReplacementAndDeletionOfEveryOwnedArtifact() {
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsYoutubeRuntime::prepare(h.config);
        QVERIFY(lease);
        const auto directory = lease->directory();
        for (const auto& artifact : h.config.artifacts) {
            const auto path = directory + '/' + artifact.relative_path;
            QFile writer{path};
            QVERIFY(!writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(!QFile::remove(path));
            QVERIFY(!QFile::rename(path, path + QStringLiteral(".replaced")));
            QCOMPARE(fileHash(path), artifact.sha256);
        }
        QVERIFY(!QDir().rename(directory, directory + QStringLiteral(".replaced")));
        const auto nested = directory + QStringLiteral("/extractor/_internal/yt_dlp_ejs/yt/solver");
        QVERIFY(!QDir().rename(nested, nested + QStringLiteral(".replaced")));
        QVERIFY(lease->unchanged());
        lease.reset();
        QVERIFY(!QDir(directory).exists());
        h.verifySources();
    }
    void theLastSharedLeaseOwnsCleanupAndIndependentPreparationsStaySeparate() {
        Fixture h;
        QVERIFY(h.initialize());
        auto first = WindowsYoutubeRuntime::prepare(h.config);
        auto second = WindowsYoutubeRuntime::prepare(h.config);
        QVERIFY(first && second);
        const auto first_directory = first->directory(), second_directory = second->directory();
        QVERIFY(first_directory != second_directory);
        auto retained = first;
        first.reset();
        QVERIFY(QDir(first_directory).exists());
        QVERIFY(retained->unchanged());
        second.reset();
        QVERIFY(!QDir(second_directory).exists());
        QVERIFY(QDir(first_directory).exists());
        retained.reset();
        QVERIFY(!QDir(first_directory).exists());
        QVERIFY(h.runtimes().isEmpty());
        h.verifySources();
    }
    void unknownFilesInvalidateInventoryAndSurviveIdentityOwnedCleanup_data() {
        QTest::addColumn<QString>("relative");
        QTest::newRow("root loader file") << QStringLiteral("qt.conf");
        QTest::newRow("nested EJS file") << QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/unexpected.txt");
        QTest::newRow("notice file") << QStringLiteral("notices/unknown.txt");
    }
    void unknownFilesInvalidateInventoryAndSurviveIdentityOwnedCleanup() {
        QFETCH(QString, relative);
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsYoutubeRuntime::prepare(h.config);
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto unknown = directory + '/' + relative;
        const QByteArray marker{"unknown owned test marker must be preserved"};
        QVERIFY(writeFile(unknown, marker));
        QVERIFY(!lease->unchanged());
        lease.reset();
        QCOMPARE(readFile(unknown), marker);
        QVERIFY(QDir(directory).exists());
        for (const auto& artifact : h.config.artifacts)
            QVERIFY(!QFile::exists(directory + '/' + artifact.relative_path));
        h.verifySources();
    }
    void unknownDirectoriesInvalidateInventoryAndSurviveIdentityOwnedCleanup_data() {
        QTest::addColumn<QString>("relative");
        QTest::newRow("root directory") << QStringLiteral("unknown directory");
        QTest::newRow("nested directory") << QStringLiteral("extractor/_internal/unknown");
    }
    void unknownDirectoriesInvalidateInventoryAndSurviveIdentityOwnedCleanup() {
        QFETCH(QString, relative);
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsYoutubeRuntime::prepare(h.config);
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto unknown = directory + '/' + relative;
        QVERIFY(QDir().mkdir(unknown));
        QVERIFY(!lease->unchanged());
        lease.reset();
        QVERIFY(QDir(unknown).exists());
        QVERIFY(QDir(directory).exists());
        for (const auto& artifact : h.config.artifacts)
            QVERIFY(!QFile::exists(directory + '/' + artifact.relative_path));
        h.verifySources();
    }
};
QTEST_GUILESS_MAIN(WindowsYoutubeRuntimeTests)
#include "WindowsYoutubeRuntimeTests.moc"
