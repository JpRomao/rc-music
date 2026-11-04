#include "adapters/WindowsMediaFoundationLaunch.hpp"

#include <QCryptographicHash>
#include <QDir>
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

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace music_player::m0;
namespace {
const QByteArray synthetic_bytes{"trusted synthetic worker bytes, not an executable"};

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
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music MF launch 日本-XXXXXX")};
    QString source{parent.filePath(QStringLiteral("source com espaços.bin"))};
    QString runtime{parent.filePath(QStringLiteral("private runtimes"))};
    bool initialize() { return parent.isValid() && QDir().mkdir(runtime) && writeFile(source, synthetic_bytes); }
    WindowsMediaFoundationLaunchConfig config() const { return {runtime, source, fileHash(source)}; }
    QStringList runtimes() const { return QDir(runtime).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
};
struct OwnedHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~OwnedHandle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};

// This creates only an owned temporary directory junction. It neither requires
// developer-mode file symlinks nor invokes a shell or changes host settings.
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
    const auto print = QDir::toNativeSeparators(target).toStdWString();
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
    const OwnedHandle handle{CreateFileW(QDir::toNativeSeparators(directory).toStdWString().c_str(), GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;
    return DeviceIoControl(handle.value, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                           nullptr, 0, &returned, nullptr) != FALSE;
}
struct OwnedJunction final {
    QTemporaryDir& owner;
    const QString path;
    bool armed{false};
    ~OwnedJunction() { static_cast<void>(remove()); }
    void retain() {
        armed = true;
        owner.setAutoRemove(false);
    }
    bool remove() {
        if (!armed)
            return true;
        if (!QDir::isAbsolutePath(owner.path()) || !path.startsWith(owner.path() + '/'))
            return false;
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        const auto attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !RemoveDirectoryW(native.c_str()))
            return false;
        armed = false;
        owner.setAutoRemove(true);
        return true;
    }
};
} // namespace

class WindowsMediaFoundationLaunchTests final : public QObject {
    Q_OBJECT
  private slots:
    void refusesMissingAndMalformedTrust_data() {
        QTest::addColumn<int>("mutation");
        QTest::newRow("empty configuration") << 0;
        QTest::newRow("missing parent") << 1;
        QTest::newRow("missing source") << 2;
        QTest::newRow("missing hash") << 3;
        QTest::newRow("short hash") << 4;
        QTest::newRow("uppercase hash") << 5;
        QTest::newRow("relative parent") << 6;
        QTest::newRow("relative source") << 7;
        QTest::newRow("URL source") << 8;
        QTest::newRow("UNC parent") << 9;
    }
    void refusesMissingAndMalformedTrust() {
        QFETCH(int, mutation);
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        switch (mutation) {
        case 0:
            config = {};
            break;
        case 1:
            config.runtime_parent.clear();
            break;
        case 2:
            config.source_executable.clear();
            break;
        case 3:
            config.source_sha256.clear();
            break;
        case 4:
            config.source_sha256 = QByteArray(63, 'a');
            break;
        case 5:
            config.source_sha256 = QByteArray(64, 'A');
            break;
        case 6:
            config.runtime_parent = QStringLiteral("relative/runtime");
            break;
        case 7:
            config.source_executable = QStringLiteral("relative/source.exe");
            break;
        case 8:
            config.source_executable = QStringLiteral("https://media.example.test/worker.exe");
            break;
        case 9:
            config.runtime_parent = QStringLiteral("//server/share/runtime");
            break;
        default:
            QFAIL("Unknown test mutation");
        }
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
        QCOMPARE(readFile(h.source), synthetic_bytes);
    }
    void refusesParentWithoutLaunchPathHeadroom() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.runtime_parent = QStringLiteral("C:/") + QString(8180, QLatin1Char('a'));
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::InvalidConfiguration);
        QVERIFY(h.runtimes().isEmpty());
    }
    void refusesMismatchBeforeCreatingPrivateDirectory() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.source_sha256 = QByteArray(64, '0');
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::HashMismatch);
        QVERIFY(h.runtimes().isEmpty());
        QCOMPARE(readFile(h.source), synthetic_bytes);
    }
    void refusesMissingSourceBeforeCreatingPrivateDirectory() {
        Fixture h;
        QVERIFY(h.initialize());
        auto config = h.config();
        config.source_executable = h.parent.filePath(QStringLiteral("missing.exe"));
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
    }
    void preparesOneAuthenticatedCopyWithFixedProcessConfiguration() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto config = h.config();
        WindowsMediaFoundationLaunchError error{};
        auto lease = WindowsMediaFoundationLaunch::prepare(config, &error);
        QVERIFY(lease);
        QCOMPARE(error, WindowsMediaFoundationLaunchError::None);
        QVERIFY(lease->unchanged());
        const auto directory = lease->directory();
        QVERIFY(directory.startsWith(h.runtime + '/'));
        const auto process = lease->processConfig();
        QCOMPARE(process.executable, directory + QStringLiteral("/RCMusicMediaWorker.exe"));
        QCOMPARE(process.working_directory, directory);
        QVERIFY(process.arguments.isEmpty());
        QVERIFY(process.standard_input.isEmpty());
        QVERIFY(process.environment);
        QCOMPARE(process.environment->keys().size(), 2);
        QVERIFY(process.environment->contains(QStringLiteral("PATH")));
        QCOMPARE(process.environment->value(QStringLiteral("PATH")), QString{});
        QVERIFY(!process.environment->value(QStringLiteral("SystemRoot")).isEmpty());
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("RCMusicMediaWorker.exe")});
        QCOMPARE(readFile(process.executable), synthetic_bytes);
        QCOMPARE(fileHash(process.executable), config.source_sha256);
        QCOMPARE(readFile(h.source), synthetic_bytes);
        lease.reset();
        QVERIFY(!QDir(directory).exists());
        QCOMPARE(readFile(h.source), synthetic_bytes);
        QVERIFY(h.runtimes().isEmpty());
    }
    void copiedRuntimeDoesNotReopenTheMutableSource() {
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto destination = lease->processConfig().executable;
        QFile source{h.source};
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray replacement{"new source bytes after authenticated copy"};
        QCOMPARE(source.write(replacement), replacement.size());
        source.close();
        QVERIFY(lease->unchanged());
        QCOMPARE(readFile(destination), synthetic_bytes);
        QCOMPARE(readFile(h.source), replacement);
    }
    void retainsReadOnlyDestinationAndDirectoryPins() {
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto executable = lease->processConfig().executable;
        QFile writer{executable};
        QVERIFY(!writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(!QFile::rename(executable, executable + QStringLiteral(".replaced")));
        QVERIFY(!QFile::remove(executable));
        QVERIFY(!QDir().rename(directory, directory + QStringLiteral(".replaced")));
        QVERIFY(lease->unchanged());
        QCOMPARE(readFile(executable), synthetic_bytes);
        lease.reset();
        QVERIFY(!QDir(directory).exists());
    }
    void refusesSourceWithAnExistingWriter() {
        Fixture h;
        QVERIFY(h.initialize());
        const auto config = h.config();
        const OwnedHandle writer{CreateFileW(QDir::toNativeSeparators(h.source).toStdWString().c_str(), GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        QVERIFY(writer.value != INVALID_HANDLE_VALUE);
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
    }
    void refusesExtraLoaderControlAndPreservesUnknownFiles_data() {
        QTest::addColumn<QString>("leaf");
        QTest::newRow("DLL") << QStringLiteral("unexpected.dll");
        QTest::newRow("DLL redirection") << QStringLiteral("RCMusicMediaWorker.exe.local");
        QTest::newRow("external manifest") << QStringLiteral("RCMusicMediaWorker.exe.manifest");
        QTest::newRow("Qt configuration") << QStringLiteral("qt.conf");
    }
    void refusesExtraLoaderControlAndPreservesUnknownFiles() {
        QFETCH(QString, leaf);
        Fixture h;
        QVERIFY(h.initialize());
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto extra = directory + '/' + leaf;
        const QByteArray marker{"unknown file must remain untouched"};
        QVERIFY(writeFile(extra, marker));
        QVERIFY(!lease->unchanged());
        lease.reset();
        QCOMPARE(readFile(extra), marker);
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{leaf});
        QCOMPARE(readFile(h.source), synthetic_bytes);
        QVERIFY(QFile::remove(extra));
        QVERIFY(QDir().rmdir(directory));
    }
    void refusesReparseSourceAncestorsAndRuntimeParents_data() {
        QTest::addColumn<bool>("source_alias");
        QTest::newRow("source ancestor") << true;
        QTest::newRow("runtime parent") << false;
    }
    void refusesReparseSourceAncestorsAndRuntimeParents() {
        QFETCH(bool, source_alias);
        Fixture h;
        QVERIFY(h.initialize());
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const auto target = outside.filePath(QStringLiteral("source.bin"));
        QVERIFY(writeFile(target, synthetic_bytes));
        const auto alias = h.parent.filePath(QStringLiteral("alias"));
        QVERIFY(QDir().mkdir(alias));
        OwnedJunction junction{h.parent, alias};
        if (!setSyntheticJunction(alias, outside.path()))
            QSKIP("Owned temporary junction creation is not available on this host");
        junction.retain();
        auto config = h.config();
        if (source_alias)
            config.source_executable = alias + QStringLiteral("/source.bin");
        else
            config.runtime_parent = alias;
        WindowsMediaFoundationLaunchError error{};
        QVERIFY(!WindowsMediaFoundationLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsMediaFoundationLaunchError::FileAccess);
        QVERIFY(h.runtimes().isEmpty());
        QCOMPARE(QDir(outside.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("source.bin")});
        QCOMPARE(readFile(target), synthetic_bytes);
        QVERIFY(junction.remove());
    }
    void cleanupPreservesAnUnknownJunctionWithoutTraversingIt() {
        Fixture h;
        QVERIFY(h.initialize());
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const auto target = outside.filePath(QStringLiteral("sentinel.bin"));
        const QByteArray sentinel{"outside fixture must not be traversed or removed"};
        QVERIFY(writeFile(target, sentinel));
        auto lease = WindowsMediaFoundationLaunch::prepare(h.config());
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto alias = directory + QStringLiteral("/unknown-junction");
        QVERIFY(QDir().mkdir(alias));
        OwnedJunction junction{h.parent, alias};
        if (!setSyntheticJunction(alias, outside.path()))
            QSKIP("Owned temporary junction creation is not available on this host");
        junction.retain();
        QVERIFY(!lease->unchanged());
        lease.reset();
        QCOMPARE(readFile(target), sentinel);
        const auto attributes = GetFileAttributesW(QDir::toNativeSeparators(alias).toStdWString().c_str());
        QVERIFY(attributes != INVALID_FILE_ATTRIBUTES);
        QVERIFY(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("unknown-junction")});
        QVERIFY(junction.remove());
        QVERIFY(QDir().rmdir(directory));
    }
};
QTEST_GUILESS_MAIN(WindowsMediaFoundationLaunchTests)
#include "WindowsMediaFoundationLaunchTests.moc"
