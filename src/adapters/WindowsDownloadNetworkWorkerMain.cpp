#include "DownloadWorkerProtocol.hpp"
#include "WindowsDownloadNetworkWorker.hpp"

#include <QCoreApplication>
#include <QHostInfo>
#include <QTimer>

#include <array>
#include <cstdlib>
#include <optional>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace music_player::m0;

namespace {
constexpr int failureExitCode = 17;
constexpr int brokenChannelExitCode = 18;

// No free-form Qt diagnostics can echo request data, pins, names or URLs into
// the controller's stderr channel. Only typed binary frames leave this worker.
void discardMessage(QtMsgType, const QMessageLogContext&, const QString&) {}

bool writeFrame(const DownloadWorkerFrame& frame) noexcept {
    try {
        const auto bytes = encodeDownloadWorkerFrame(frame);
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (bytes.isEmpty() || bytes.size() > download_worker_max_frame_bytes + 4 || output == nullptr ||
            output == INVALID_HANDLE_VALUE || GetFileType(output) != FILE_TYPE_PIPE)
            return false;
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            const auto count = static_cast<DWORD>(bytes.size() - offset);
            // This synchronous private-pipe write is intentional backpressure.
            // The owner's Job/deadline can kill this diskless process if blocked.
            if (!WriteFile(output, bytes.constData() + offset, count, &written, nullptr) || written == 0 ||
                written > count)
                return false;
            offset += static_cast<qsizetype>(written);
        }
        return true;
    } catch (...) {
        return false;
    }
}

int terminalResult(DownloadWorkerError error, bool authenticated_eof = false) noexcept {
    DownloadWorkerFrame frame;
    frame.error = error;
    frame.authenticated_eof = error == DownloadWorkerError::None && authenticated_eof;
    if (!writeFrame(frame))
        return brokenChannelExitCode;
    return error == DownloadWorkerError::None ? 0 : failureExitCode;
}

std::optional<QByteArray> readRequest() {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE || GetFileType(input) != FILE_TYPE_PIPE)
        return std::nullopt;
    std::array<char, 4096> block{};
    QByteArray bytes;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(input, block.data(), static_cast<DWORD>(block.size()), &count, nullptr))
            return GetLastError() == ERROR_BROKEN_PIPE ? std::optional<QByteArray>{std::move(bytes)} : std::nullopt;
        if (count == 0)
            return bytes;
        if (count > block.size() || static_cast<qsizetype>(count) > download_worker_max_request_bytes - bytes.size())
            return std::nullopt;
        bytes.append(block.data(), static_cast<qsizetype>(count));
    }
}

bool validAddresses(const QList<QHostAddress>& addresses) {
    if (addresses.isEmpty() || addresses.size() > 32)
        return false;
    for (const auto& address : addresses) {
        if (address.isNull() || !address.scopeId().isEmpty() ||
            (address.protocol() != QAbstractSocket::IPv4Protocol &&
             address.protocol() != QAbstractSocket::IPv6Protocol))
            return false;
    }
    return true;
}

DownloadWorkerError streamFailure(PinnedDownloadStreamError error) {
    if (error == PinnedDownloadStreamError::Tls)
        return DownloadWorkerError::Tls;
    if (error == PinnedDownloadStreamError::InvalidEndpoint)
        return DownloadWorkerError::InvalidRequest;
    return DownloadWorkerError::Connection;
}

class NetworkWorker final : public QObject {
  public:
    explicit NetworkWorker(QCoreApplication& application) : application_(application), stream_(this), timer_(this) {
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { pump(); });
    }

    void start(const DownloadWorkerRequest& request) {
        try {
            if (request.mode == DownloadWorkerMode::Resolve) {
                QHostAddress numeric;
                if (numeric.setAddress(request.host)) {
                    // QHostInfo's literal-IP path performs reverse lookup/PTR.
                    // A numeric input is already resolved and needs no DNS I/O.
                    resolved({numeric});
                    return;
                }
                const auto lookup = QHostInfo::lookupHost(request.host, this, [this](const QHostInfo& information) {
                    try {
                        if (information.error() != QHostInfo::NoError)
                            finish(DownloadWorkerError::Dns);
                        else
                            resolved(information.addresses());
                    } catch (...) {
                        finish(DownloadWorkerError::Dns);
                    }
                });
                if (lookup < 0)
                    finish(DownloadWorkerError::Dns);
                return;
            }
            if (!stream_.open(request.endpoint)) {
                finish(streamFailure(stream_.poll().error));
                return;
            }
            timer_.start(1);
        } catch (...) {
            finish(request.mode == DownloadWorkerMode::Resolve ? DownloadWorkerError::Dns
                                                               : DownloadWorkerError::Connection);
        }
    }

  private:
    void finish(DownloadWorkerError error, bool authenticated_eof = false) noexcept {
        if (finished_)
            return;
        finished_ = true;
        timer_.stop();
        application_.exit(terminalResult(error, authenticated_eof));
    }

    void resolved(const QList<QHostAddress>& addresses) {
        if (finished_)
            return;
        if (!validAddresses(addresses)) {
            finish(DownloadWorkerError::Dns);
            return;
        }
        DownloadWorkerFrame frame;
        frame.kind = DownloadWorkerFrameKind::Addresses;
        frame.addresses = addresses;
        if (!writeFrame(frame)) {
            finish(DownloadWorkerError::Dns);
            return;
        }
        finish(DownloadWorkerError::None);
    }

    void pump() {
        if (finished_)
            return;
        try {
            const auto update = stream_.poll();
            if (update.state == PinnedDownloadStreamState::Failed) {
                finish(streamFailure(update.error));
                return;
            }
            if (update.state == PinnedDownloadStreamState::Idle ||
                update.state == PinnedDownloadStreamState::Cancelled) {
                finish(DownloadWorkerError::Connection);
                return;
            }
            if (update.available_bytes > 0) {
                DownloadWorkerFrame frame;
                frame.kind = DownloadWorkerFrameKind::Data;
                frame.data = stream_.read(download_worker_max_data_bytes);
                if (frame.data.isEmpty() || frame.data.size() > download_worker_max_data_bytes || !writeFrame(frame))
                    finish(DownloadWorkerError::Connection);
                // At most one data frame per event-loop tick. A Finished stream
                // is not acknowledged until a later tick proves it fully drained.
                return;
            }
            if (update.state == PinnedDownloadStreamState::Finished)
                finish(DownloadWorkerError::None, update.authenticated_eof);
        } catch (...) {
            finish(DownloadWorkerError::Connection);
        }
    }

    QCoreApplication& application_;
    PinnedDownloadStream stream_;
    QTimer timer_;
    bool finished_{false};
};
} // namespace

int music_player::m0::runWindowsDownloadNetworkWorker(int argc, char* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    qInstallMessageHandler(discardMessage);
    try {
        if (argc != 1)
            return terminalResult(DownloadWorkerError::InvalidRequest);
        QCoreApplication application(argc, argv);
        QCoreApplication::setLibraryPaths({});
        const auto bytes = readRequest();
        const auto request = bytes ? decodeDownloadWorkerRequest(*bytes) : std::nullopt;
        if (!request)
            return terminalResult(DownloadWorkerError::InvalidRequest);
        // Required even for DNS/plain HTTP: no fallback path can use an
        // unreviewed runtime, and these pins remain held until the worker exits.
        WindowsDownloadTlsRuntime runtime;
        if (!runtime.activate(request->runtime))
            return terminalResult(DownloadWorkerError::Runtime);
        NetworkWorker worker(application);
        QTimer::singleShot(0, &worker, [&] { worker.start(*request); });
        return application.exec();
    } catch (...) {
        return terminalResult(DownloadWorkerError::InvalidRequest);
    }
}

#ifndef RC_MUSIC_DOWNLOAD_NETWORK_PAYLOAD
// Historical worker/test executable, not the authenticated product bootstrap.
int main(int argc, char* argv[]) { return music_player::m0::runWindowsDownloadNetworkWorker(argc, argv); }
#endif
