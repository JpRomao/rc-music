#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <QtEndian>

#include <qt_windows.h>
#include <winver.h>

#include <utility>

namespace music_player::m0::test {
namespace {

struct ResourceEnumeration {
    unsigned int count = 0;
    HRSRC first = nullptr;
};

BOOL CALLBACK collectResource(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR parameter) {
    auto& result = *reinterpret_cast<ResourceEnumeration*>(parameter);
    ++result.count;
    if (result.first == nullptr)
        result.first = FindResourceW(module, name, type);
    return TRUE;
}

[[nodiscard]] QString versionString(const QByteArray& bytes, const QString& name) {
    void* translation = nullptr;
    UINT translation_size = 0;
    if (!VerQueryValueW(bytes.constData(), L"\\VarFileInfo\\Translation", &translation, &translation_size) ||
        translation == nullptr || translation_size < 4)
        return {};

    const auto* const language = static_cast<const uchar*>(translation);
    const QString key = QStringLiteral("\\StringFileInfo\\%1%2\\%3")
                            .arg(qFromLittleEndian<quint16>(language), 4, 16, QLatin1Char('0'))
                            .arg(qFromLittleEndian<quint16>(language + 2), 4, 16, QLatin1Char('0'))
                            .arg(name);
    void* value = nullptr;
    UINT value_size = 0;
    if (!VerQueryValueW(bytes.constData(), key.toStdWString().c_str(), &value, &value_size) || value == nullptr ||
        value_size == 0)
        return {};
    const auto* const text = static_cast<const wchar_t*>(value);
    if (text[value_size - 1] != L'\0')
        return {};
    return QString::fromWCharArray(text, static_cast<qsizetype>(value_size - 1));
}

} // namespace

class WindowsExecutableIdentityTests final : public QObject {
    Q_OBJECT

  public:
    explicit WindowsExecutableIdentityTests(QString executable) : executable_(std::move(executable)) {}

    ~WindowsExecutableIdentityTests() override {
        if (module_ != nullptr)
            FreeLibrary(module_);
    }

  private slots:
    void initTestCase() {
        QVERIFY(QFileInfo(executable_).isFile());
        // Resource mapping does not run the executable or resolve its imports.
        module_ = LoadLibraryExW(executable_.toStdWString().c_str(), nullptr,
                                 LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        QVERIFY(module_ != nullptr);
    }

    void containsOneLoadableMultiresolutionIcon() {
        ResourceEnumeration groups;
        QVERIFY(EnumResourceNamesW(module_, RT_GROUP_ICON, collectResource, reinterpret_cast<LONG_PTR>(&groups)));
        QCOMPARE(groups.count, 1U);
        QVERIFY(groups.first != nullptr);
        const QByteArray group = resourceBytes(groups.first);
        QVERIFY(group.size() >= 6);
        QCOMPARE(qFromLittleEndian<quint16>(group.constData()), quint16{0});
        QCOMPARE(qFromLittleEndian<quint16>(group.constData() + 2), quint16{1});
        const auto image_count = qFromLittleEndian<quint16>(group.constData() + 4);
        QCOMPARE(image_count, quint16{7});
        QCOMPARE(group.size(), qsizetype{6} + qsizetype{14} * image_count);

        const QSet<int> expected_sizes{16, 24, 32, 48, 64, 128, 256};
        QSet<int> actual_sizes;
        QSet<quint16> image_ids;
        for (quint16 index = 0; index < image_count; ++index) {
            const auto* const entry = reinterpret_cast<const uchar*>(group.constData() + 6 + 14 * index);
            const int width = entry[0] == 0 ? 256 : entry[0];
            const int height = entry[1] == 0 ? 256 : entry[1];
            QCOMPARE(width, height);
            QVERIFY(expected_sizes.contains(width));
            QVERIFY(!actual_sizes.contains(width));
            actual_sizes.insert(width);
            const auto resource_size = qFromLittleEndian<quint32>(entry + 8);
            const auto resource_id = qFromLittleEndian<quint16>(entry + 12);
            QVERIFY(!image_ids.contains(resource_id));
            image_ids.insert(resource_id);
            const HRSRC resource = FindResourceW(module_, MAKEINTRESOURCEW(resource_id), RT_ICON);
            QVERIFY(resource != nullptr);
            QByteArray image = resourceBytes(resource);
            QCOMPARE(image.size(), static_cast<qsizetype>(resource_size));
            QVERIFY(!image.isEmpty());
            const HICON icon = CreateIconFromResourceEx(reinterpret_cast<PBYTE>(image.data()), resource_size, TRUE,
                                                        0x00030000, width, height, LR_DEFAULTCOLOR);
            QVERIFY(icon != nullptr);
            const auto release_icon = qScopeGuard([icon] { DestroyIcon(icon); });
        }
        QCOMPARE(actual_sizes, expected_sizes);
    }

    void publishesOnlyTheKnownProductIdentity() {
        const QByteArray version = versionInformation();
        QVERIFY(!version.isEmpty());
        QCOMPARE(versionString(version, QStringLiteral("ProductName")), QStringLiteral("RC Music"));
        QCOMPARE(versionString(version, QStringLiteral("FileDescription")), QStringLiteral("RC Music"));
        QCOMPARE(versionString(version, QStringLiteral("InternalName")), QStringLiteral("MusicPlayer"));
        QCOMPARE(versionString(version, QStringLiteral("OriginalFilename")), QStringLiteral("m0_qt_shell.exe"));
        QVERIFY(versionString(version, QStringLiteral("CompanyName")).isEmpty());
        QVERIFY(versionString(version, QStringLiteral("LegalCopyright")).isEmpty());
        QVERIFY(versionString(version, QStringLiteral("LegalTrademarks")).isEmpty());
    }

    void marksTheVersionAsPrivateAndExperimentalWithoutInventingARelease() {
        const QByteArray version = versionInformation();
        QVERIFY(!version.isEmpty());
        void* fixed_value = nullptr;
        UINT fixed_size = 0;
        QVERIFY(VerQueryValueW(version.constData(), L"\\", &fixed_value, &fixed_size));
        QVERIFY(fixed_value != nullptr);
        QVERIFY(fixed_size >= sizeof(VS_FIXEDFILEINFO));
        const auto& fixed = *static_cast<const VS_FIXEDFILEINFO*>(fixed_value);
        QCOMPARE(fixed.dwSignature, DWORD{VS_FFI_SIGNATURE});
        QCOMPARE(fixed.dwFileVersionMS, DWORD{0});
        QCOMPARE(fixed.dwFileVersionLS, DWORD{0});
        QCOMPARE(fixed.dwProductVersionMS, DWORD{0});
        QCOMPARE(fixed.dwProductVersionLS, DWORD{0});
        constexpr DWORD private_experimental = VS_FF_PRERELEASE | VS_FF_PRIVATEBUILD;
        QCOMPARE(fixed.dwFileFlagsMask & private_experimental, private_experimental);
        QCOMPARE(fixed.dwFileFlags & private_experimental, private_experimental);
        QCOMPARE(versionString(version, QStringLiteral("FileVersion")), QStringLiteral("M0 experimental"));
        QCOMPARE(versionString(version, QStringLiteral("ProductVersion")), QStringLiteral("M0 experimental"));
        QVERIFY(!versionString(version, QStringLiteral("PrivateBuild")).trimmed().isEmpty());
    }

  private:
    [[nodiscard]] QByteArray resourceBytes(HRSRC resource) const {
        const DWORD size = SizeofResource(module_, resource);
        const HGLOBAL loaded = LoadResource(module_, resource);
        const void* const data = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (data == nullptr || size == 0)
            return {};
        return QByteArray(static_cast<const char*>(data), static_cast<qsizetype>(size));
    }

    [[nodiscard]] QByteArray versionInformation() const {
        const auto path = executable_.toStdWString();
        DWORD unused = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
        if (size == 0)
            return {};
        QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()))
            return {};
        return bytes;
    }

    QString executable_;
    HMODULE module_ = nullptr;
};

} // namespace music_player::m0::test

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QStringList arguments = application.arguments();
    if (arguments.size() < 2)
        return 2;
    const QString executable = QFileInfo(arguments.takeAt(1)).absoluteFilePath();
    music_player::m0::test::WindowsExecutableIdentityTests tests(executable);
    return QTest::qExec(&tests, arguments);
}

#include "WindowsExecutableIdentityTests.moc"
