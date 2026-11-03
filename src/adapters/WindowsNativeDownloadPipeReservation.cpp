#include "adapters/WindowsNativeDownloadPipeReservation.hpp"

#include <QScopeGuard>
#include <qt_windows.h>

#include <Sddl.h>

#include <algorithm>
#include <vector>

namespace music_player::m0 {
WindowsNativeDownloadPipeReservation::~WindowsNativeDownloadPipeReservation() { reset(); }
bool WindowsNativeDownloadPipeReservation::acquire(const QString& endpoint) {
    if (server_ || client_ || endpoint.isEmpty() || endpoint.size() > 96 ||
        !std::all_of(endpoint.cbegin(), endpoint.cend(), [](const QChar character) {
            const auto unit = character.unicode();
            return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
                   unit == '_' || unit == '-';
        }))
        return false;
    try {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        const auto close_token = qScopeGuard([&] { CloseHandle(token); });
        DWORD bytes = 0;
        static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0, &bytes));
        if (bytes < sizeof(TOKEN_USER) || bytes > 4'096)
            return false;
        std::vector<unsigned char> storage(bytes);
        if (!GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes))
            return false;
        const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
        if (!IsValidSid(user->User.Sid))
            return false;
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(user->User.Sid, &text))
            return false;
        const auto free_text = qScopeGuard([&] { LocalFree(text); });
        const auto sddl = QStringLiteral("D:P(A;;GA;;;SY)(A;;GA;;;") + QString::fromWCharArray(text) + QLatin1Char(')');
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1,
                                                                  &descriptor, nullptr))
            return false;
        const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        const auto name = (QStringLiteral("\\\\.\\pipe\\") + endpoint).toStdWString();
        // Match Qt 6.11.2's serving-instance attributes. Only this first,
        // permanently occupied instance uses FIRST_PIPE_INSTANCE.
        const auto server =
            CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                             PIPE_UNLIMITED_INSTANCES, 0, 0, 3'000, &attributes);
        if (server == INVALID_HANDLE_VALUE)
            return false;
        auto close_server = qScopeGuard([&] { CloseHandle(server); });
        // No WaitNamedPipe, ConnectNamedPipe or read/write is issued. Keep the
        // private client attached so ordinary clients cannot land on the anchor.
        // Anonymous SQOS also prevents impersonation if a hostile instance races
        // this open; both peer PIDs must match before the reservation is published.
        const auto client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
        if (client == INVALID_HANDLE_VALUE)
            return false;
        auto close_client = qScopeGuard([&] { CloseHandle(client); });
        ULONG server_pid = 0, client_pid = 0;
        const auto self = GetCurrentProcessId();
        if (!GetNamedPipeServerProcessId(client, &server_pid) || server_pid != self ||
            !GetNamedPipeClientProcessId(server, &client_pid) || client_pid != self)
            return false;
        server_ = server;
        client_ = client;
        close_server.dismiss();
        close_client.dismiss();
        return true;
    } catch (...) {
        return false;
    }
}
void WindowsNativeDownloadPipeReservation::reset() noexcept {
    // No I/O was started on either handle, so no outstanding OVERLAPPED or
    // callback may outlive them. The server keeps the name until the last close.
    if (client_)
        CloseHandle(client_);
    client_ = nullptr;
    if (server_)
        CloseHandle(server_);
    server_ = nullptr;
}
} // namespace music_player::m0
