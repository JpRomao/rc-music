#include "adapters/NativeDownloadPeerImages.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <qt_windows.h>

#include <array>
#include <filesystem>

using namespace music_player::m0;

namespace {
QString expectedSibling(const QString& name) {
    std::array<wchar_t, 32'768> buffer{};
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size())
        return {};
    const std::filesystem::path image{std::wstring{buffer.data(), size}};
    return QString::fromStdWString((image.parent_path() / name.toStdWString()).native());
}
} // namespace

class NativeDownloadPeerImagesTests final : public QObject {
    Q_OBJECT

  private slots:
    void resolvesSiblingWithoutApplicationOrExistingSiblingFile() {
        QVERIFY(QCoreApplication::instance() == nullptr);
        const auto name = QStringLiteral("rc_music-nonexistent-peer-fixture.exe");
        const auto expected = expectedSibling(name);
        QVERIFY(!expected.isEmpty());
        QCOMPARE(nativeDownloadSiblingImages({name}), QStringList{expected});
    }

    void preservesOrderOfFourAsciiExecutableBasenames() {
        const QStringList names{QStringLiteral("RCMusic.exe"), QStringLiteral("rc_music_native_host.exe"),
                                QStringLiteral("Audio-player_2.EXE"), QStringLiteral("Test-4.exe")};
        QStringList expected;
        for (const auto& name : names)
            expected.append(expectedSibling(name));
        QCOMPARE(nativeDownloadSiblingImages(names), expected);
    }

    void ignoresCurrentWorkingDirectory() {
        const auto previous = QDir::currentPath();
        QTemporaryDir unrelated;
        QVERIFY(unrelated.isValid());
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(unrelated.path()));
        const auto name = QStringLiteral("rc_music_native_host.exe");
        QCOMPARE(nativeDownloadSiblingImages({name}), QStringList{expectedSibling(name)});
    }

    void rejectsInvalidLists_data() {
        QTest::addColumn<QStringList>("names");
        QTest::newRow("empty-list") << QStringList{};
        QTest::newRow("too-many") << QStringList{QStringLiteral("one.exe"), QStringLiteral("two.exe"),
                                                 QStringLiteral("three.exe"), QStringLiteral("four.exe"),
                                                 QStringLiteral("five.exe")};
        int index = 0;
        for (const auto& invalid : QStringList{QString{},
                                               QStringLiteral(".exe"),
                                               QStringLiteral("owner"),
                                               QStringLiteral("owner.dll"),
                                               QStringLiteral("owner.exe.exe"),
                                               QStringLiteral("../owner.exe"),
                                               QStringLiteral("..\\owner.exe"),
                                               QStringLiteral("folder/owner.exe"),
                                               QStringLiteral("folder\\owner.exe"),
                                               QStringLiteral("C:\\owner.exe"),
                                               QStringLiteral("\\\\server\\share\\owner.exe"),
                                               QStringLiteral("\\\\?\\C:\\owner.exe"),
                                               QStringLiteral("owner.exe:stream"),
                                               QStringLiteral(" owner.exe"),
                                               QStringLiteral("owner .exe"),
                                               QStringLiteral("owner.exe "),
                                               QStringLiteral("\"owner.exe\""),
                                               QStringLiteral("owner?.exe"),
                                               QStringLiteral("owner*.exe"),
                                               QStringLiteral("áudio.exe"),
                                               QStringLiteral("owner") + QChar{0} + QStringLiteral(".exe"),
                                               QStringLiteral("owner") + QChar{0x1f} + QStringLiteral(".exe")}) {
            const auto row = QByteArray::number(index++);
            QTest::newRow(row.constData()) << QStringList{QStringLiteral("valid.exe"), invalid};
        }
    }

    void rejectsInvalidLists() {
        QFETCH(QStringList, names);
        QVERIFY(nativeDownloadSiblingImages(names).isEmpty());
    }
};

QTEST_APPLESS_MAIN(NativeDownloadPeerImagesTests)
#include "NativeDownloadPeerImagesTests.moc"
