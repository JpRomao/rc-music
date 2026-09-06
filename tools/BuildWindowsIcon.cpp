#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QStringList>
#include <QSvgRenderer>

#include <array>

// Build-only conversion of the already authenticated, project-owned tray SVG.
// PNG icon frames are supported by the Windows versions targeted by this candidate.
int main(int argc, char** argv) {
    const QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 3)
        return 1;
    QSvgRenderer renderer{arguments.at(1)};
    if (!renderer.isValid() || renderer.defaultSize() != QSize{256, 256})
        return 2;

    constexpr std::array sizes{16, 24, 32, 48, 64, 128, 256};
    std::array<QByteArray, sizes.size()> frames;
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const int size = sizes.at(index);
        QImage image{size, size, QImage::Format_ARGB32};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        renderer.render(&painter);
        painter.end();
        QBuffer buffer{&frames.at(index)};
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            return 3;
    }

    QSaveFile output{arguments.at(2)};
    if (!output.open(QIODevice::WriteOnly))
        return 4;
    QDataStream stream{&output};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16{0} << quint16{1} << static_cast<quint16>(sizes.size());
    quint32 offset = 6 + 16 * static_cast<quint32>(sizes.size());
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const auto dimension = static_cast<quint8>(sizes.at(index) == 256 ? 0 : sizes.at(index));
        const auto frame_size = static_cast<quint32>(frames.at(index).size());
        stream << dimension << dimension << quint8{0} << quint8{0} << quint16{1} << quint16{32} << frame_size << offset;
        offset += frame_size;
    }
    for (const QByteArray& frame : frames) {
        if (stream.writeRawData(frame.constData(), frame.size()) != frame.size())
            return 5;
    }
    if (stream.status() != QDataStream::Ok || !output.commit())
        return 6;
    return 0;
}
