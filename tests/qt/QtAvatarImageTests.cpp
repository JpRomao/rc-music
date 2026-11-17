#include "adapters/QtAvatarImage.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;
namespace {
// 100x300: top third red, middle third green, bottom third blue.
QImage tall() {
    QImage image(100, 300, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(0, 0, 100, 100, QColor(255, 0, 0));
    painter.fillRect(0, 100, 100, 100, QColor(0, 255, 0));
    painter.fillRect(0, 200, 100, 100, QColor(0, 0, 255));
    return image;
}
bool writeImage(const QImage& image, const QString& path, const char* format) {
    QImageWriter writer(path, format);
    writer.setQuality(100);
    return writer.write(image);
}
bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
void verifyCentered(const std::optional<QByteArray>& png) {
    QVERIFY(png.has_value());
    QImage out;
    QVERIFY(out.loadFromData(*png, "PNG"));
    QCOMPARE(out.width(), static_cast<int>(avatar_pixels));
    QCOMPARE(out.height(), static_cast<int>(avatar_pixels));
    // The central 100x100 square is the green band; nothing red or blue remains.
    for (const auto y : {2, 128, 253}) {
        const auto color = out.pixelColor(128, y);
        QVERIFY(color.green() > 200 && color.red() < 60 && color.blue() < 60);
    }
}
} // namespace

class QtAvatarImageTests final : public QObject {
    Q_OBJECT
  private slots:
    void normalizesPngAndJpegToACenteredSquare() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeImage(tall(), dir.filePath("a.png"), "PNG"));
        QVERIFY(writeImage(tall(), dir.filePath("b.jpg"), "JPEG"));
        verifyCentered(normalizeAvatarImage(dir.filePath("a.png")));
        verifyCentered(normalizeAvatarImage(dir.filePath("b.jpg")));
    }
    void detectsTheFormatByContentNotExtension() {
        QTemporaryDir dir;
        QVERIFY(writeImage(tall(), dir.filePath("img.dat"), "PNG"));
        QVERIFY(normalizeAvatarImage(dir.filePath("img.dat")).has_value());
    }
    void rejectsGifEvenWithAPngName() {
        QTemporaryDir dir;
        const auto path = dir.filePath("a.png");
        // Minimal 1x1 GIF89a, crafted by hand because the gif writer plugin may not be deployed.
        const QByteArray gif = QByteArray::fromHex("47494638396101000100800000000000ffffff21f90401000000002c00000000"
                                                   "010001000002024401003b");
        QVERIFY(writeBytes(path, gif));
        QVERIFY(!normalizeAvatarImage(path).has_value());
    }
    void rejectsFilesOverTheInputLimitBeforeDecoding() {
        QTemporaryDir dir;
        const auto path = dir.filePath("big.png");
        QVERIFY(writeImage(tall(), path, "PNG"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll();
        file.close();
        bytes.append(QByteArray(maximum_custom_avatar_input_bytes + 1 - bytes.size(), '\0'));
        QCOMPARE(bytes.size(), static_cast<qsizetype>(maximum_custom_avatar_input_bytes + 1));
        QVERIFY(writeBytes(path, bytes));
        QVERIFY(!normalizeAvatarImage(path).has_value());
        // Exactly at the limit is still accepted (trailing bytes are ignored by the PNG decoder).
        bytes.chop(1);
        QVERIFY(writeBytes(path, bytes));
        QVERIFY(normalizeAvatarImage(path).has_value());
    }
    void rejectsTruncatedPngAndMissingFile() {
        QTemporaryDir dir;
        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(tall().save(&buffer, "PNG"));
        const auto path = dir.filePath("cut.png");
        QVERIFY(writeBytes(path, buffer.data().left(buffer.data().size() / 2)));
        QVERIFY(!normalizeAvatarImage(path).has_value());
        QVERIFY(!normalizeAvatarImage(dir.filePath("missing.png")).has_value());
    }
    void savesTheCustomAvatarAndReturnsItsSha256() {
        QTemporaryDir dir;
        QVERIFY(writeImage(tall(), dir.filePath("a.png"), "PNG"));
        const auto png = normalizeAvatarImage(dir.filePath("a.png"));
        QVERIFY(png);
        const auto config = dir.filePath("cfg");
        const auto hash = saveCustomAvatar(config, *png);
        QCOMPARE(hash, QString::fromLatin1(QCryptographicHash::hash(*png, QCryptographicHash::Sha256).toHex()));
        QCOMPARE(hash, hash.toLower());
        QCOMPARE(hash.size(), 64);
        QFile file(QDir(config).filePath("avatars/custom.png"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), *png);
        QVERIFY(saveCustomAvatar(config, {}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(QtAvatarImageTests)
#include "QtAvatarImageTests.moc"
