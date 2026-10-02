#include "NativeHostBuildConfiguration.hpp"
#include "NativeHostInputStep.hpp"
#include "adapters/NativeDownloadSession.hpp"
#include "adapters/QtNativeDownloadClient.hpp"
#include "adapters/WindowsNativeHostWatchdog.hpp"
#include "adapters/WindowsNativeMessagingIo.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>

namespace {
using namespace music_player::m0;

int runHost(int argc, char** argv, const NativeHostConfiguration& config, const QString& caller) {
    QCoreApplication application{argc, argv};
    WindowsNativeMessagingIo io;
    if (!io.valid())
        return 2;
    QtNativeDownloadClient submission{config};
    QElapsedTimer elapsed;
    elapsed.start();
    NativeDownloadSession session{caller, config.allowed_origin, submission};
    enum class Phase { Receiving, Writing, Closing };
    Phase phase{Phase::Receiving};
    int result = 2;
    const auto close = [&](const int code) {
        phase = Phase::Closing;
        result = code;
        submission.shutdown();
        io.beginShutdown();
    };
    QTimer timer;
    timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, &application, [&] {
        try {
            if (phase == Phase::Receiving) {
                const auto response =
                    readNativeHostInput(session, [&] { return io.readAvailable(); }, [&] { return elapsed.elapsed(); });
                if (!response)
                    close(2);
                else {
                    if (response->status == NativeSessionStatus::Replied) {
                        if (io.beginWrite(response->response_frame, elapsed.elapsed()))
                            phase = Phase::Writing;
                        else
                            close(2);
                    } else if (response->status != NativeSessionStatus::Awaiting)
                        close(2);
                }
            }
            const auto now = elapsed.elapsed();
            if (phase == Phase::Writing) {
                io.poll(now);
                if (io.outputState() == NativeStdioOutputState::Completed)
                    close(0);
                else if (io.outputState() == NativeStdioOutputState::Failed)
                    close(2);
            }
            if (phase == Phase::Closing) {
                io.poll(now); // Repeat cancellation until the writer physically stops.
                if (submission.stopped() && io.stopped()) {
                    timer.stop();
                    application.exit(result);
                }
            }
        } catch (...) {
            close(2);
        }
    });
    timer.start();
    return application.exec();
}
} // namespace

int main(int argc, char** argv) {
    // stdout is exclusively one binary Native Messaging reply. Even Qt's
    // startup/plugin diagnostics cannot contaminate it or expose request URLs.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) {});
    try {
        const auto configuration = music_player::m0::nativeHostBuildConfiguration();
        QStringList raw_arguments;
        for (int index = 0; index < argc; ++index)
            raw_arguments.append(QString::fromLocal8Bit(argv[index]));
        const auto launch = music_player::m0::parseNativeHostLaunch(raw_arguments, configuration);
        if (launch.error != music_player::m0::NativeHostLaunchError::None)
            return 2; // Disabled/unauthorized before Qt, I/O or local connection.
        music_player::m0::WindowsNativeHostWatchdog watchdog;
        if (!watchdog.valid())
            return 2;
        // Declared first, destroyed last, including exceptional teardown. The
        // independent deadline covers synchronous Peek/Read and writer drain.
        return runHost(argc, argv, configuration, launch.caller_origin);
    } catch (...) {
        return 2;
    }
}
