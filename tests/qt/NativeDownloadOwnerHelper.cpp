#include "adapters/QtNativeDownloadServer.hpp"
#include "adapters/WindowsNativeHostWatchdog.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTimer>
#include <qt_windows.h>

#include <memory>
#include <optional>
#include <string_view>

using namespace music_player::m0;
namespace {
constexpr qsizetype input_limit = 256;

bool marker(const char* value, const DWORD size) {
    DWORD written = 0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value, size, &written, nullptr) && written == size;
}

class Submission final : public AsyncDownloadSubmission {
  public:
    DownloadAdmissionResult begin(const DownloadRequest&) override {
        if (!marker("ADMITTED\n", 9))
            return {};
        return {.state = DownloadAdmissionState::Completed,
                .result = {.error = DownloadSubmissionError::None, .job_ids = {"owner-job-1"}}};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override { return std::nullopt; }
};
} // namespace

int main(int argc, char* argv[]) {
    // Only this synthetic helper gets argv-controlled identity. No production
    // host/app hook or page input can construct an owner configuration.
    WindowsNativeHostWatchdog watchdog{15'000};
    if (!watchdog.valid())
        return 2;
    QCoreApplication app{argc, argv};
    const auto arguments = app.arguments();
    const QRegularExpression endpoint_pattern{QStringLiteral("^rc-music-native-owner-[0-9a-f]{32}$"),
                                              QRegularExpression::CaseInsensitiveOption};
    if (arguments.size() != 4 || !endpoint_pattern.match(arguments.at(1)).hasMatch() ||
        (arguments.at(2) != QStringLiteral("primary") && arguments.at(2) != QStringLiteral("alternate")) ||
        (arguments.at(3) != QStringLiteral("valid") && arguments.at(3) != QStringLiteral("invalid")) ||
        GetFileType(GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_PIPE ||
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_PIPE)
        return 2;

    auto origin = arguments.at(2) == QStringLiteral("primary")
                      ? QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/")
                      : QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/");
    if (arguments.at(3) == QStringLiteral("invalid"))
        origin = QStringLiteral("https://invalid.example.test/");
    Submission submission;
    auto server =
        std::make_unique<QtNativeDownloadServer>(NativeHostConfiguration{origin, arguments.at(1)}, submission);
    const auto started = server->start();
    if (!(started ? marker("READY\n", 6) : marker("REFUSED\n", 8)))
        return 2;

    // A refused contender deliberately remains alive with its server object.
    // The process tests can detect a leaked lease handle, not merely rely on
    // process termination to clean up a rejected acquisition.
    QByteArray input;
    qsizetype consumed = 0;
    bool shutdown_requested = false;
    QTimer commands;
    commands.setInterval(5);
    QObject::connect(&commands, &QTimer::timeout, &app, [&] {
        DWORD available = 0;
        const auto stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
        if (!PeekNamedPipe(stdin_handle, nullptr, 0, nullptr, &available, nullptr)) {
            server.reset();
            app.exit(2);
            return;
        }
        if (available > static_cast<DWORD>(input_limit - consumed)) {
            server.reset();
            app.exit(2);
            return;
        }
        if (available != 0) {
            char bytes[input_limit];
            DWORD read = 0;
            if (!ReadFile(stdin_handle, bytes, available, &read, nullptr) || read != available) {
                server.reset();
                app.exit(2);
                return;
            }
            input.append(bytes, static_cast<qsizetype>(read));
            consumed += static_cast<qsizetype>(read);
        }
        while (true) {
            const auto newline = input.indexOf('\n');
            if (newline < 0)
                break;
            const auto command = input.first(newline);
            input.remove(0, newline + 1);
            if (command == "shutdown" && server && !shutdown_requested) {
                shutdown_requested = true;
                server->beginShutdown();
            } else if (command == "release" && server) {
                server.reset();
                if (!marker("RELEASED\n", 9))
                    app.exit(2);
            } else if (command == "quit") {
                server.reset();
                app.quit();
                return;
            } else {
                server.reset();
                app.exit(2);
                return;
            }
        }
        if (shutdown_requested && server && server->stopped()) {
            shutdown_requested = false;
            if (!marker("STOPPED\n", 8))
                app.exit(2);
        }
    });
    commands.start();
    const auto result = app.exec();
    server.reset();
    return result;
}
