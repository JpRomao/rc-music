#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <fcntl.h>
#include <io.h>

// Test-only converter: a valid synthetic MP3 does not waive the final decoder.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (_setmode(_fileno(stdout), _O_BINARY) == -1 || _setmode(_fileno(stderr), _O_BINARY) == -1)
        return 2;
    if (!arguments.contains("-progress"))
        return 17; // Controlled full-decode failure.
    if (QFileInfo(app.applicationFilePath()).baseName() == "conversion-hang") {
        for (;;)
            QThread::msleep(10);
    }
    QFile fixture(QString::fromUtf8(RC_MUSIC_TEST_MP3_FIXTURE));
    if (!fixture.open(QIODevice::ReadOnly))
        return 2;
    const auto bytes = fixture.readAll();
    if (bytes.size() != 24966 || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
                                     "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000")
        return 2;
    QFile output, progress;
    if (!output.open(stdout, QIODevice::WriteOnly) || !progress.open(stderr, QIODevice::WriteOnly))
        return 2;
    return output.write(bytes) == bytes.size() && output.flush() &&
                   progress.write("out_time_us=2000000\nprogress=end\n") > 0 && progress.flush()
               ? 0
               : 2;
}
