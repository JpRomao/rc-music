#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "adapters/QtNativeDownloadServer.hpp"
#include "adapters/WindowsNativeHostWatchdog.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTimer>
#include <qt_windows.h>

#include <memory>
#include <optional>
#include <string_view>

using namespace music_player::m0;
namespace {
constexpr qsizetype input_limit = 256;
const QString origin = QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/");
constexpr std::string_view request_id = "01234567-89ab-4cde-8fab-0123456789ab";

bool marker(const char* value, const DWORD size) {
    DWORD written = 0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value, size, &written, nullptr) && written == size;
}

QByteArray requestFrame() {
    const QByteArray payload =
        R"({"items":[{"kind":"audio","title":"Synthetic","url":"https://fixture.invalid/synthetic.mp3"}],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"type":"download.request"})";
    return native_download_ipc::preamble(origin) + encodeNativeMessageFrame(payload);
}

QByteArray acceptedFrame() {
    return encodeNativeMessageFrame(
        R"({"jobIds":["peer-job-1"],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"status":"accepted","type":"download.response"})");
}

class Submission final : public AsyncDownloadSubmission {
  public:
    int begins{0};
    std::optional<DownloadSubmissionResult> completion;
    DownloadAdmissionResult begin(const DownloadRequest&) override {
        ++begins;
        if (!marker("ADMITTED\n", 9))
            return {};
        return {.state = DownloadAdmissionState::Pending};
    }
    std::optional<DownloadSubmissionResult> poll(const std::string_view id) override {
        return id == request_id ? completion : std::nullopt;
    }
};
} // namespace

int main(int argc, char* argv[]) {
    WindowsNativeHostWatchdog watchdog{15'000};
    if (!watchdog.valid())
        return 2;
    QCoreApplication app{argc, argv};
    const auto arguments = app.arguments();
    const QRegularExpression endpoint_pattern{QStringLiteral("^rc-music-native-peer-[0-9a-f]{32}$")};
    const QStringList modes{QStringLiteral("owner"), QStringLiteral("raw-owner"), QStringLiteral("raw-host"),
                            QStringLiteral("exiting-owner")};
    // Only this test helper accepts a trusted driver-selected expected image.
    // The real host/app have no argv, environment, page or message override.
    if (arguments.size() != 4 || !endpoint_pattern.match(arguments.at(1)).hasMatch() ||
        !modes.contains(arguments.at(2)) || arguments.at(3).size() > 30'000 ||
        !QFileInfo{arguments.at(3)}.isAbsolute() || GetFileType(GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_PIPE ||
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_PIPE)
        return 2;
    const auto mode = arguments.at(2);
    NativeHostConfiguration configuration{origin, arguments.at(1)};
    configuration.expected_owner_images = {arguments.at(3)};
    configuration.expected_host_images = {arguments.at(3)};
    Submission submission;
    std::unique_ptr<QtNativeDownloadServer> owner;
    QLocalServer raw_owner;
    std::unique_ptr<QLocalSocket> raw_socket;
    QByteArray received;
    bool received_reported = false;
    bool replied = false;
    bool closed_reported = false;
    const auto read_raw_owner = [&] {
        if (!raw_socket)
            return;
        const auto remaining =
            native_message_max_payload_bytes + native_download_ipc::preamble_bytes + 5 - received.size();
        if (remaining <= 0) {
            app.exit(2);
            return;
        }
        received += raw_socket->read(remaining);
        if (!received.isEmpty() && !received_reported) {
            received_reported = true;
            if (!marker("RECEIVED\n", 9))
                app.exit(2);
        }
        const auto expected = requestFrame();
        if (!replied && received.size() >= expected.size()) {
            if (received != expected) {
                app.exit(2);
                return;
            }
            replied = true;
            const auto response = acceptedFrame();
            if (raw_socket->write(response) != response.size()) {
                app.exit(2);
                return;
            }
            raw_socket->disconnectFromServer();
        }
    };
    if (mode == QStringLiteral("owner")) {
        owner = std::make_unique<QtNativeDownloadServer>(configuration, submission);
        if (!owner->start()) {
            static_cast<void>(marker("REFUSED\n", 8));
            return 2;
        }
    } else if (mode == QStringLiteral("raw-host")) {
        raw_socket = std::make_unique<QLocalSocket>();
        raw_socket->setReadBufferSize(native_download_ipc::response_max_payload_bytes + 5);
        QObject::connect(raw_socket.get(), &QLocalSocket::connected, &app, [&] {
            if (!marker("CONNECTED\n", 10) || raw_socket->write(requestFrame()) != requestFrame().size())
                app.exit(2);
        });
        QObject::connect(raw_socket.get(), &QLocalSocket::readyRead, &app, [&] {
            if (!received_reported && raw_socket->bytesAvailable() > 0) {
                received_reported = true;
                if (!marker("REPLIED\n", 8))
                    app.exit(2);
            }
            static_cast<void>(raw_socket->read(native_download_ipc::response_max_payload_bytes + 5));
        });
        const auto closed = [&] {
            if (!closed_reported) {
                closed_reported = true;
                if (!marker("CLOSED\n", 7))
                    app.exit(2);
            }
        };
        QObject::connect(raw_socket.get(), &QLocalSocket::disconnected, &app, closed);
        QObject::connect(raw_socket.get(), &QLocalSocket::errorOccurred, &app,
                         [closed](QLocalSocket::LocalSocketError) { closed(); });
        raw_socket->connectToServer(configuration.endpoint_name);
    } else {
        raw_owner.setSocketOptions(QLocalServer::UserAccessOption);
        raw_owner.setMaxPendingConnections(1);
        QObject::connect(&raw_owner, &QLocalServer::newConnection, &app, [&] {
            auto* socket = raw_owner.nextPendingConnection();
            if (!socket || raw_socket) {
                delete socket;
                app.exit(2);
                return;
            }
            raw_socket.reset(socket);
            raw_socket->setParent(nullptr);
            raw_socket->setReadBufferSize(native_message_max_payload_bytes + native_download_ipc::preamble_bytes + 5);
            if (!marker("CONNECTED\n", 10)) {
                app.exit(2);
                return;
            }
            if (mode == QStringLiteral("exiting-owner")) {
                app.quit();
                return;
            }
            QObject::connect(raw_socket.get(), &QLocalSocket::readyRead, &app, read_raw_owner);
            read_raw_owner();
        });
        if (!raw_owner.listen(configuration.endpoint_name))
            return 2;
    }
    if (!marker("READY\n", 6))
        return 2;

    QByteArray input;
    qsizetype consumed = 0;
    QTimer commands;
    commands.setInterval(5);
    QObject::connect(&commands, &QTimer::timeout, &app, [&] {
        DWORD available = 0;
        const auto input_handle = GetStdHandle(STD_INPUT_HANDLE);
        if (!PeekNamedPipe(input_handle, nullptr, 0, nullptr, &available, nullptr) ||
            available > static_cast<DWORD>(input_limit - consumed)) {
            app.exit(2);
            return;
        }
        if (available != 0) {
            char bytes[input_limit];
            DWORD read = 0;
            if (!ReadFile(input_handle, bytes, available, &read, nullptr) || read != available) {
                app.exit(2);
                return;
            }
            input.append(bytes, static_cast<qsizetype>(read));
            consumed += static_cast<qsizetype>(read);
        }
        while (true) {
            const auto newline = input.indexOf('\n');
            if (newline < 0)
                return;
            const auto command = input.first(newline);
            input.remove(0, newline + 1);
            if (command == "quit") {
                app.quit();
                return;
            }
            if (command == "complete" && owner && submission.begins == 1 && !submission.completion) {
                submission.completion =
                    DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"peer-job-1"}};
                if (!marker("COMPLETED\n", 10))
                    app.exit(2);
            } else if (command == "probe" && mode == QStringLiteral("raw-owner")) {
                read_raw_owner();
                if (!(received.isEmpty() ? marker("ZERO\n", 5) : marker("NONZERO\n", 8)))
                    app.exit(2);
            } else {
                app.exit(2);
                return;
            }
        }
    });
    commands.start();
    const auto result = app.exec();
    owner.reset();
    return result;
}
