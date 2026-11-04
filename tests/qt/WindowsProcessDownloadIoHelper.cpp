#include "adapters/DownloadWorkerProtocol.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QtEndian>

#include <array>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace music_player::m0;

namespace {
bool writeBytes(const QByteArray& bytes, DWORD handle = STD_OUTPUT_HANDLE) {
    DWORD written = 0;
    return WriteFile(GetStdHandle(handle), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
           static_cast<qsizetype>(written) == bytes.size();
}
bool emitFrame(DownloadWorkerFrameKind kind, const QByteArray& data = {}, bool eof = false) {
    DownloadWorkerFrame frame;
    frame.kind = kind;
    frame.data = data;
    frame.authenticated_eof = eof;
    return writeBytes(encodeDownloadWorkerFrame(frame));
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QByteArray bytes;
    std::array<char, 4096> block{};
    DWORD count = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), block.data(), static_cast<DWORD>(block.size()), &count, nullptr) &&
           count != 0) {
        if (bytes.size() + count > download_worker_max_request_bytes)
            return 80;
        bytes.append(block.data(), count);
    }
    const auto request = decodeDownloadWorkerRequest(bytes);
    if (!request)
        return 81;
    const auto scenario = request->mode == DownloadWorkerMode::Resolve
                              ? request->host
                              : QString::fromLatin1(request->endpoint.request_target);
    const bool stall_after_data = request->mode == DownloadWorkerMode::Stream && scenario.contains("stall-after-data");
    if ((!stall_after_data && scenario.contains("stall")) || scenario.contains("timeout")) {
        QTimer keep_alive;
        keep_alive.start(10);
        return app.exec();
    }
    if (scenario.contains("stderr"))
        static_cast<void>(writeBytes("PRIVATE_DIAGNOSTIC_SENTINEL", STD_ERROR_HANDLE));
    if (request->mode == DownloadWorkerMode::Resolve) {
        DownloadWorkerFrame reply;
        reply.kind = DownloadWorkerFrameKind::Addresses;
        reply.addresses = {QHostAddress{"93.184.215.14"}};
        if (scenario.contains("mixed"))
            reply.addresses.push_back(QHostAddress{"127.0.0.1"});
        if (scenario.contains("bad-order"))
            static_cast<void>(emitFrame(DownloadWorkerFrameKind::Data, "unexpected"));
        else
            static_cast<void>(writeBytes(encodeDownloadWorkerFrame(reply)));
        if (scenario.contains("duplicate"))
            static_cast<void>(writeBytes(encodeDownloadWorkerFrame(reply)));
    } else {
        if (scenario.contains("oversize-frame")) {
            QByteArray length(4, '\0');
            qToLittleEndian<quint32>(quint32(download_worker_max_frame_bytes + 1), length.data());
            static_cast<void>(writeBytes(length));
            return 0;
        }
        const QByteArray content = scenario.contains("synthetic") ? [&] {
            QFile fixture(
                QString::fromUtf8(scenario.contains("mp4") ? RC_MUSIC_TEST_MP4_FIXTURE : RC_MUSIC_TEST_MP3_FIXTURE));
            return fixture.open(QIODevice::ReadOnly) ? fixture.read(256 * 1024) : QByteArray{};
        }()
                                                                  : QByteArray("audio");
        const auto response =
            scenario.contains("eof")
                ? QByteArray("HTTP/1.1 200 OK\r\n\r\n") + content
                : "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(content.size()) + "\r\n\r\n" + content;
        if (stall_after_data) {
            // Deliver a real partial body before staying alive. Public progress
            // then proves transfer began; no terminal/EOF can authorize sealing,
            // conversion or publication before the owner's explicit cancellation.
            const auto partial = response.first(response.size() - 1);
            for (qsizetype offset = 0; offset < partial.size(); offset += download_worker_max_data_bytes)
                if (!emitFrame(DownloadWorkerFrameKind::Data, partial.mid(offset, download_worker_max_data_bytes)))
                    return 84;
            QTimer keep_alive;
            keep_alive.start(10);
            return app.exec();
        }
        if (scenario.contains("flood")) {
            for (int index = 0; index < 256; ++index)
                if (!emitFrame(DownloadWorkerFrameKind::Data, QByteArray(download_worker_max_data_bytes, 'x')))
                    return 83;
        } else
            for (qsizetype offset = 0; offset < response.size(); offset += download_worker_max_data_bytes)
                if (!emitFrame(DownloadWorkerFrameKind::Data, response.mid(offset, download_worker_max_data_bytes)))
                    return 84;
    }
    if (!scenario.contains("missing-terminal"))
        static_cast<void>(emitFrame(DownloadWorkerFrameKind::Terminal, {}, scenario.contains("eof-true")));
    if (scenario.contains("trailing"))
        static_cast<void>(emitFrame(DownloadWorkerFrameKind::Data, "unexpected"));
    if (scenario.contains("delayed")) {
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
        return app.exec();
    }
    return scenario.contains("exit-nonzero") ? 9 : 0;
}
