#include "adapters/WindowsDownloadTlsRuntime.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <cstddef>
#include <cstring>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>

#ifndef RC_MUSIC_TEST_TLS_PLUGIN_PATH
#define RC_MUSIC_TEST_TLS_PLUGIN_PATH ""
#endif
#ifndef RC_MUSIC_TEST_TLS_PLUGIN_SHA256
#define RC_MUSIC_TEST_TLS_PLUGIN_SHA256 ""
#endif

using namespace music_player::m0;

namespace {
QJsonObject configuration() {
    return {{"core_path", QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH)},
            {"core_sha256", QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_SHA256)},
            {"network_path", QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH)},
            {"network_sha256", QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_SHA256)},
            {"plugin_path", QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH).isEmpty()
                                ? QDir::tempPath() + "/missing-reviewed-plugin.dll"
                                : QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH)},
            {"plugin_sha256", QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_SHA256).isEmpty()
                                  ? QString(64, QLatin1Char('0'))
                                  : QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_SHA256)}};
}

bool realPluginEnabled() { return !QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH).isEmpty(); }

QByteArray fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}

struct Result final {
    bool completed{false};
    QJsonObject value{};
};

Result run(QJsonObject config, const QString& mode = {}, const QString& injected_plugin_path = {}) {
    config.insert("mode", mode);
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("QT_PLUGIN_PATH");
    environment.remove("QT_SSL_BACKEND");
    if (!injected_plugin_path.isEmpty())
        environment.insert("QT_PLUGIN_PATH", injected_plugin_path);
    child.setProcessEnvironment(environment);
    child.setProgram(QString::fromUtf8(RC_MUSIC_TEST_TLS_HELPER_PATH));
    child.setWorkingDirectory(QFileInfo(child.program()).absolutePath());
    child.start();
    if (!child.waitForStarted(5000))
        return {};
    const auto bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
    if (child.write(bytes) != bytes.size()) {
        child.kill();
        child.waitForFinished(5000);
        return {};
    }
    child.closeWriteChannel();
    if (!child.waitForFinished(15000)) {
        child.kill();
        child.waitForFinished(5000);
        return {};
    }
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) {
        qWarning("TLS fixture helper exit status=%d code=%d", static_cast<int>(child.exitStatus()), child.exitCode());
        qWarning().noquote() << child.readAllStandardError().left(4096);
        return {};
    }
    const auto output = child.readAllStandardOutput();
    if (output.size() > 4096)
        return {};
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(output, &error);
    return {error.error == QJsonParseError::NoError && document.isObject(), document.object()};
}

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
    const HANDLE handle = CreateFileW(QDir::toNativeSeparators(directory).toStdWString().c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;
    const bool set = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                                     nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return set;
}

struct OwnedJunction final {
    QTemporaryDir& owner;
    QString path{};
    bool armed{false};
    ~OwnedJunction() { remove(); }
    bool remove() {
        if (!armed)
            return true;
        if (path != owner.path() + QStringLiteral("/alias") || !QDir::isAbsolutePath(owner.path()))
            return false;
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        const auto attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) || !RemoveDirectoryW(native.c_str()))
            return false;
        armed = false;
        owner.setAutoRemove(true);
        return true;
    }
};

void expectFailure(const Result& result, WindowsDownloadTlsRuntimeError error) {
    QVERIFY(result.completed);
    QVERIFY(!result.value.value("accepted").toBool());
    QCOMPARE(result.value.value("error").toInt(), static_cast<int>(error));
    QCOMPARE(result.value.value("initial_error").toInt(), static_cast<int>(WindowsDownloadTlsRuntimeError::None));
    if (error != WindowsDownloadTlsRuntimeError::MissingApplication)
        QVERIFY(result.value.value("paths_empty").toBool());
}
} // namespace

class WindowsDownloadTlsRuntimeTests final : public QObject {
    Q_OBJECT
  private slots:
    void invalidConfigurationsAreRejected_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("value");
        QTest::newRow("empty path") << QStringLiteral("core_path") << QString{};
        QTest::newRow("relative") << QStringLiteral("core_path") << QStringLiteral("Qt6Core.dll");
        QTest::newRow("drive relative") << QStringLiteral("core_path") << QStringLiteral("C:Qt6Core.dll");
        QTest::newRow("UNC") << QStringLiteral("core_path") << QStringLiteral("//server/share/Qt6Core.dll");
        QTest::newRow("device namespace") << QStringLiteral("core_path") << QStringLiteral("//?/C:/Qt6Core.dll");
        QTest::newRow("dot parent") << QStringLiteral("core_path") << QStringLiteral("C:/folder/../Qt6Core.dll");
        QTest::newRow("alternate stream") << QStringLiteral("core_path") << QStringLiteral("C:/Qt6Core.dll:stream");
        QTest::newRow("trailing space") << QStringLiteral("core_path") << QStringLiteral("C:/Qt6Core.dll ");
        QTest::newRow("NUL") << QStringLiteral("core_path") << (QStringLiteral("C:/Qt6") + QChar{u'\0'} + "Core.dll");
        QTest::newRow("long path") << QStringLiteral("core_path") << (QStringLiteral("C:/") + QString(30000, 'x'));
        QTest::newRow("empty pin") << QStringLiteral("core_sha256") << QString{};
        QTest::newRow("short pin") << QStringLiteral("core_sha256") << QString(63, 'a');
        QTest::newRow("long pin") << QStringLiteral("core_sha256") << QString(65, 'a');
        QTest::newRow("nonhex pin") << QStringLiteral("core_sha256") << QString(64, 'g');
        QTest::newRow("uppercase pin") << QStringLiteral("core_sha256") << QString(64, 'A');
        QTest::newRow("network pin") << QStringLiteral("network_sha256") << QString(64, 'g');
        QTest::newRow("plugin pin") << QStringLiteral("plugin_sha256") << QString(64, 'g');
        QTest::newRow("network relative") << QStringLiteral("network_path") << QStringLiteral("Qt6Network.dll");
        QTest::newRow("plugin relative") << QStringLiteral("plugin_path") << QStringLiteral("qbackend.dll");
    }
    void invalidConfigurationsAreRejected() {
        QFETCH(QString, field);
        QFETCH(QString, value);
        auto config = configuration();
        config.insert(field, value);
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::InvalidConfiguration);
    }
    void applicationMustExist() {
        expectFailure(run(configuration(), "no-application"), WindowsDownloadTlsRuntimeError::MissingApplication);
    }
    void missingArtifactsDoNotEnableFallback_data() {
        QTest::addColumn<QString>("field");
        QTest::newRow("core") << QStringLiteral("core_path");
        QTest::newRow("network") << QStringLiteral("network_path");
        QTest::newRow("plugin") << QStringLiteral("plugin_path");
    }
    void missingArtifactsDoNotEnableFallback() {
        QFETCH(QString, field);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration();
        config.insert(field, directory.filePath("missing.dll"));
        const auto result = run(config);
        expectFailure(result, WindowsDownloadTlsRuntimeError::FileAccess);
        QVERIFY(!result.value.value("only_custom").toBool());
    }
    void wrongHashesFailBeforeLoading_data() {
        QTest::addColumn<QString>("field");
        QTest::newRow("core") << QStringLiteral("core_sha256");
        QTest::newRow("network") << QStringLiteral("network_sha256");
        QTest::newRow("plugin") << QStringLiteral("plugin_sha256");
    }
    void wrongHashesFailBeforeLoading() {
        QFETCH(QString, field);
        if (field == QStringLiteral("plugin_sha256") && !realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        auto config = configuration();
        config.insert(field, QString(64, '0'));
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::HashMismatch);
    }
    void sameBytesAtAnotherRuntimePathDoNotProveLoadedIdentity_data() {
        QTest::addColumn<QString>("field");
        QTest::newRow("core") << QStringLiteral("core_path");
        QTest::newRow("network") << QStringLiteral("network_path");
    }
    void sameBytesAtAnotherRuntimePathDoNotProveLoadedIdentity() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        QFETCH(QString, field);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration();
        const auto copied = directory.filePath(QFileInfo(config.value(field).toString()).fileName());
        QVERIFY(QFile::copy(config.value(field).toString(), copied));
        config.insert(field, copied);
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::RuntimeMismatch);
    }
    void oversizedArtifactIsRejectedBeforeHashingOrLoading() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("oversize.dll");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QVERIFY(file.resize(static_cast<qint64>(download_tls_max_artifact_bytes + 1)));
        file.close();
        auto config = configuration();
        config.insert("core_path", path);
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::FileAccess);
    }
    void emptyArtifactIsRejectedBeforeLoading() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("empty.dll");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.close();
        auto config = configuration();
        config.insert("core_path", path);
        config.insert(
            "core_sha256",
            QString::fromLatin1(QCryptographicHash::hash(QByteArrayView{}, QCryptographicHash::Sha256).toHex()));
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::FileAccess);
    }
    void reparseAncestorIsRejected() {
        QTemporaryDir directory;
        QTemporaryDir target;
        QVERIFY(directory.isValid());
        QVERIFY(target.isValid());
        QVERIFY(QDir::isAbsolutePath(directory.path()));
        QVERIFY(QDir::isAbsolutePath(target.path()));
        QVERIFY(target.path() != directory.path());
        QVERIFY(!target.path().startsWith(directory.path() + '/'));
        const auto alias = directory.filePath("alias");
        const auto copied = target.filePath("Qt6Core.dll");
        QVERIFY(QFile::copy(configuration().value("core_path").toString(), copied));
        const auto expected_hash = configuration().value("core_sha256").toString().toLatin1();
        QCOMPARE(fileHash(copied), expected_hash);
        const auto sentinel = target.filePath("survivor.bin");
        const auto sentinel_bytes = QByteArray("owned fixture target must survive alias cleanup");
        QFile sentinel_file(sentinel);
        QVERIFY(sentinel_file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(sentinel_file.write(sentinel_bytes), sentinel_bytes.size());
        sentinel_file.close();
        QVERIFY(QDir().mkdir(alias));
        OwnedJunction junction{directory, alias};
        // Fail closed even if native unlink later fails: never let Qt recurse
        // over an armed reparse fixture. Preserve only this owned TEMP instead.
        directory.setAutoRemove(false);
        if (!setSyntheticJunction(alias, target.path()))
            QSKIP("Host filesystem did not permit this synthetic junction.");
        junction.armed = true;
        auto config = configuration();
        config.insert("core_path", alias + "/Qt6Core.dll");
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::FileAccess);
        // Unlink only the owned junction before QTemporaryDir visits its root.
        QVERIFY(junction.remove());
        QVERIFY(directory.remove());
        QVERIFY(QFileInfo::exists(copied));
        QCOMPARE(fileHash(copied), expected_hash);
        QVERIFY(sentinel_file.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel_file.readAll(), sentinel_bytes);
    }
    void fakePluginWithAnAuthenticHashIsNotAccepted() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto copied = directory.filePath("not-a-tls-plugin.dll");
        auto config = configuration();
        QVERIFY(QFile::copy(config.value("core_path").toString(), copied));
        config.insert("plugin_path", copied);
        config.insert("plugin_sha256", config.value("core_sha256"));
        expectFailure(run(config), WindowsDownloadTlsRuntimeError::PluginLoad);
    }
    void successfulActivationIsExclusiveAndHoldsReadOnlyPins() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        const auto result = run(configuration());
        QVERIFY(result.completed);
        QVERIFY(result.value.value("accepted").toBool());
        QCOMPARE(result.value.value("error").toInt(), static_cast<int>(WindowsDownloadTlsRuntimeError::None));
        QVERIFY(result.value.value("paths_empty").toBool());
        QVERIFY(result.value.value("only_custom").toBool());
        QVERIFY(result.value.value("active_custom").toBool());
        QVERIFY(result.value.value("supports_ssl").toBool());
        QVERIFY(result.value.value("pins_read_only").toBool());
        QVERIFY(result.value.value("read_shared").toBool());
    }
    void injectedPluginSearchPathIsRemovedAndUnicodePluginPathWorks() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music TLS 日本-XXXXXX")};
        QVERIFY(directory.isValid());
        const auto copied = directory.filePath("qrcmusicschanneloffline.dll");
        auto config = configuration();
        QVERIFY(QFile::copy(config.value("plugin_path").toString(), copied));
        config.insert("plugin_path", copied);
        const auto injected = QFileInfo(config.value("network_path").toString()).absolutePath() + "/../plugins";
        const auto result = run(config, {}, injected);
        QVERIFY(result.completed);
        QVERIFY(result.value.value("accepted").toBool());
        QVERIFY(result.value.value("paths_empty").toBool());
        QVERIFY(result.value.value("only_custom").toBool());
        QVERIFY(result.value.value("pins_read_only").toBool());
    }
    void previouslyInstantiatedBackendIsRejected() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        expectFailure(run(configuration(), "preexisting"), WindowsDownloadTlsRuntimeError::PreexistingBackend);
    }
    void activationIsOneShot_data() {
        QTest::addColumn<bool>("valid");
        QTest::newRow("successful first attempt") << true;
        QTest::newRow("failed first attempt") << false;
    }
    void activationIsOneShot() {
        QFETCH(bool, valid);
        if (valid && !realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        auto config = configuration();
        if (!valid)
            config.insert("core_sha256", "invalid");
        const auto result = run(config, "repeat");
        QVERIFY(result.completed);
        QCOMPARE(result.value.value("accepted").toBool(), valid);
        QVERIFY(!result.value.value("accepted_second").toBool());
        QCOMPARE(result.value.value("error_second").toInt(),
                 static_cast<int>(WindowsDownloadTlsRuntimeError::AlreadyAttempted));
    }
    void anotherRuntimeCannotAdoptAnAlreadyActiveBackend() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        const auto result = run(configuration(), "second-object");
        QVERIFY(result.completed);
        QVERIFY(result.value.value("accepted").toBool());
        QVERIFY(!result.value.value("accepted_second").toBool());
        QCOMPARE(result.value.value("error_second").toInt(),
                 static_cast<int>(WindowsDownloadTlsRuntimeError::PreexistingBackend));
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadTlsRuntimeTests)
#include "WindowsDownloadTlsRuntimeTests.moc"
