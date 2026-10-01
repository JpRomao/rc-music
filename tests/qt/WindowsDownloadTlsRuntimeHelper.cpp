#include "adapters/WindowsDownloadTlsRuntime.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSslSocket>

#include <array>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace music_player::m0;

namespace {
constexpr auto backend = "rcmusic-schannel-offline";

QByteArray requestBytes() {
    QByteArray bytes;
    std::array<char, 4096> block{};
    DWORD count = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), block.data(), static_cast<DWORD>(block.size()), &count, nullptr) &&
           count != 0) {
        if (bytes.size() + count > 256 * 1024)
            return {};
        bytes.append(block.data(), static_cast<qsizetype>(count));
    }
    return bytes;
}

bool writeResult(const QJsonObject& result) {
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    DWORD written = 0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.constData(), static_cast<DWORD>(bytes.size()), &written,
                     nullptr) &&
           static_cast<qsizetype>(written) == bytes.size();
}

WindowsDownloadTlsRuntimeConfig configuration(const QJsonObject& request) {
    return {request.value("core_path").toString(),    request.value("core_sha256").toString().toLatin1(),
            request.value("network_path").toString(), request.value("network_sha256").toString().toLatin1(),
            request.value("plugin_path").toString(),  request.value("plugin_sha256").toString().toLatin1()};
}

bool deniedAccess(const QString& path, DWORD access) {
    const auto native = (QStringLiteral("\\\\?\\") + QString(path).replace('/', '\\')).toStdWString();
    const HANDLE handle = CreateFileW(native.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

bool readShared(const QString& path) {
    const auto native = (QStringLiteral("\\\\?\\") + QString(path).replace('/', '\\')).toStdWString();
    const HANDLE handle = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(handle);
    return true;
}

QJsonObject activate(const WindowsDownloadTlsRuntimeConfig& config, const QString& mode) {
    QPluginLoader preexisting;
    if (mode == QStringLiteral("preexisting")) {
        preexisting.setFileName(config.plugin_path);
        if (!preexisting.instance())
            return {{"helper_failure", "preload"}};
    }
    WindowsDownloadTlsRuntime runtime;
    const auto initial_error = runtime.error();
    const bool accepted = runtime.activate(config);
    QJsonObject result{{"accepted", accepted},
                       {"error", static_cast<int>(runtime.error())},
                       {"initial_error", static_cast<int>(initial_error)}};
    if (QCoreApplication::instance()) {
        result.insert("paths_empty", QCoreApplication::libraryPaths().isEmpty());
        result.insert("only_custom", QSslSocket::availableBackends() == QStringList{QString::fromLatin1(backend)});
        if (accepted) {
            result.insert("active_custom", QSslSocket::activeBackend() == QString::fromLatin1(backend));
            result.insert("supports_ssl", QSslSocket::supportsSsl());
            bool denied = true;
            bool shared = true;
            for (const auto& path : {config.core_path, config.network_path, config.plugin_path}) {
                // Access checks only: never write, delete or rename SDK artifacts.
                denied = denied && deniedAccess(path, GENERIC_WRITE) && deniedAccess(path, DELETE);
                shared = shared && readShared(path);
            }
            result.insert("pins_read_only", denied);
            result.insert("read_shared", shared);
        }
    }
    if (mode == QStringLiteral("repeat")) {
        result.insert("accepted_second", runtime.activate(config));
        result.insert("error_second", static_cast<int>(runtime.error()));
    }
    if (mode == QStringLiteral("second-object")) {
        WindowsDownloadTlsRuntime second;
        result.insert("accepted_second", second.activate(config));
        result.insert("error_second", static_cast<int>(second.error()));
    }
    return result;
}
} // namespace

int main(int argc, char* argv[]) {
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(requestBytes(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return 81;
    const auto request = document.object();
    const auto mode = request.value("mode").toString();
    const auto config = configuration(request);
    if (mode == QStringLiteral("no-application"))
        return writeResult(activate(config, mode)) ? 0 : 82;
    QCoreApplication application(argc, argv);
    return writeResult(activate(config, mode)) ? 0 : 82;
}
