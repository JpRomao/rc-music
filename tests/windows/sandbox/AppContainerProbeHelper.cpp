#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winsock2.h>

#include "AppContainerProbeProtocol.hpp"

#include <algorithm>

using namespace music_player::sandbox_probe;
namespace {
class Handle final {
  public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() {
        if (valid())
            CloseHandle(value_);
    }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }

  private:
    HANDLE value_;
};
bool writeAll(HANDLE handle, std::span<const std::uint8_t> bytes) {
    while (!bytes.empty()) {
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || !written)
            return false;
        bytes = bytes.subspan(written);
    }
    return true;
}
std::vector<std::uint8_t> tokenInfo(HANDLE token, TOKEN_INFORMATION_CLASS type) {
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token, type, nullptr, 0, &size));
    if (!size || size > 64 * 1024)
        return {};
    std::vector<std::uint8_t> bytes(size);
    return GetTokenInformation(token, type, bytes.data(), size, &size) ? bytes : std::vector<std::uint8_t>{};
}
void inspectToken(const Request& request, Report& report) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        report.errors[0] = GetLastError();
        return;
    }
    Handle token{raw};
    DWORD app_container = 0, count = 0;
    if (GetTokenInformation(raw, TokenIsAppContainer, &app_container, sizeof(app_container), &count) && app_container)
        report.passed |= Check::AppContainer;
    const auto package = tokenInfo(raw, TokenAppContainerSid);
    if (package.size() >= sizeof(TOKEN_APPCONTAINER_INFORMATION)) {
        const auto sid = reinterpret_cast<const TOKEN_APPCONTAINER_INFORMATION*>(package.data())->TokenAppContainer;
        if (sid && IsValidSid(sid) && IsValidSid(const_cast<std::uint8_t*>(request.sid.data())) &&
            GetLengthSid(sid) == request.sid.size() && EqualSid(sid, const_cast<std::uint8_t*>(request.sid.data())))
            report.passed |= Check::PackageSid;
    }
    const auto capabilities = tokenInfo(raw, TokenCapabilities);
    if (capabilities.size() >= sizeof(DWORD)) {
        report.capability_count = reinterpret_cast<const TOKEN_GROUPS*>(capabilities.data())->GroupCount;
        if (report.capability_count == 0)
            report.passed |= Check::ZeroCapabilities;
    }
    const auto integrity = tokenInfo(raw, TokenIntegrityLevel);
    if (integrity.size() >= sizeof(TOKEN_MANDATORY_LABEL)) {
        const auto sid = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid;
        if (sid && IsValidSid(sid) && *GetSidSubAuthorityCount(sid)) {
            report.integrity_rid = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
            if (report.integrity_rid == SECURITY_MANDATORY_LOW_RID)
                report.passed |= Check::LowIntegrity;
        }
    }
}
DWORD deniedOpen(const std::wstring& path, DWORD access, DWORD creation) {
    Handle file{CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            creation, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    return file.valid() ? ERROR_SUCCESS : GetLastError();
}
DWORD runtimeAuthority(const std::wstring& path, DWORD access) {
    Handle directory{CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    return directory.valid() ? ERROR_SUCCESS : GetLastError();
}
DWORD childAttempt() {
    std::array<wchar_t, 32768> image{};
    const auto length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (!length || length >= image.size())
        return ERROR_INVALID_NAME;
    auto command = std::wstring{L"\""} + image.data() + L"\" --unexpected-child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(image.data(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process))
        return GetLastError();
    Handle thread{process.hThread};
    Handle child{process.hProcess};
    // Unexpected success is a failed check. Never resume this child.
    static_cast<void>(TerminateProcess(process.hProcess, 90));
    return WaitForSingleObject(process.hProcess, 2000) == WAIT_OBJECT_0 ? ERROR_SUCCESS : ERROR_TIMEOUT;
}
DWORD networkAttempt(std::uint16_t port) {
    WSADATA data{};
    const auto startup = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup)
        return static_cast<DWORD>(startup);
    const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    DWORD error = ERROR_SUCCESS;
    if (socket == INVALID_SOCKET)
        error = static_cast<DWORD>(WSAGetLastError());
    else {
        u_long nonblocking = 1;
        static_cast<void>(ioctlsocket(socket, FIONBIO, &nonblocking));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            error = static_cast<DWORD>(WSAGetLastError());
            if (error == WSAEWOULDBLOCK) {
                fd_set writes, errors;
                FD_ZERO(&writes);
                FD_ZERO(&errors);
                FD_SET(socket, &writes);
                FD_SET(socket, &errors);
                timeval timeout{2, 0};
                const auto ready = select(0, nullptr, &writes, &errors, &timeout);
                int result = 0, size = sizeof(result);
                if (ready > 0 && getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&result), &size) == 0)
                    error = static_cast<DWORD>(result);
                else
                    error = ready == 0 ? WSAETIMEDOUT : static_cast<DWORD>(WSAGetLastError());
            }
        }
        closesocket(socket);
    }
    WSACleanup();
    return error;
}
} // namespace

int wmain(int argc, wchar_t**) {
    if (argc != 1 || !SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) || !SetDllDirectoryW(L""))
        return 64;
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> chunk{};
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE)
                return 65;
            break;
        }
        if (!read)
            break;
        if (bytes.size() + read > maximum_request_bytes)
            return 66;
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
    }
    const auto request = decodeRequest(bytes);
    if (!request)
        return 67;
    Report report;
    inspectToken(*request, report);
    const auto input = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(request->input_handle));
    std::array<std::uint8_t, 32> challenge{};
    DWORD read = 0;
    if (GetFileType(input) == FILE_TYPE_DISK && ReadFile(input, challenge.data(), 32, &read, nullptr) && read == 32 &&
        challenge == request->challenge)
        report.passed |= Check::InputRead;
    DWORD written = 0;
    const std::uint8_t marker = 0xff;
    report.errors[1] = WriteFile(input, &marker, 1, &written, nullptr) ? ERROR_SUCCESS : GetLastError();
    if (report.errors[1] == ERROR_ACCESS_DENIED)
        report.passed |= Check::InputWriteDenied;
    report.errors[2] = deniedOpen(request->sentinel, GENERIC_READ, OPEN_EXISTING);
    report.errors[3] = deniedOpen(request->sentinel, GENERIC_WRITE, OPEN_EXISTING);
    report.errors[4] = deniedOpen(request->runtime + L"\\denied-write.tmp", GENERIC_WRITE, CREATE_NEW);
    report.errors[5] = childAttempt();
    report.errors[6] = networkAttempt(request->port);
    report.errors[8] = runtimeAuthority(request->runtime, WRITE_DAC);
    report.errors[9] = runtimeAuthority(request->runtime, WRITE_OWNER);
    report.errors[10] = runtimeAuthority(request->runtime, DELETE);
    constexpr std::array<Check, 3> authority_checks{Check::RuntimeDaclDenied, Check::RuntimeOwnerDenied,
                                                    Check::RuntimeDeleteDenied};
    for (std::size_t index = 0; index < authority_checks.size(); ++index)
        if (report.errors[index + 8] == ERROR_ACCESS_DENIED)
            report.passed |= authority_checks[index];
    constexpr std::array<Check, 5> checks{Check::AmbientReadDenied, Check::AmbientWriteDenied,
                                          Check::RuntimeWriteDenied, Check::JobChildDenied, Check::LoopbackDenied};
    for (std::size_t index = 0; index < checks.size(); ++index)
        if (report.errors[index + 2] ==
            (index == 4 ? static_cast<DWORD>(WSAEACCES) : static_cast<DWORD>(ERROR_ACCESS_DENIED)))
            report.passed |= checks[index];
    constexpr std::array<std::uint8_t, 23> notice{'s', 'a', 'n', 'd', 'b', 'o', 'x', '-', 'p', 'r', 'o', 'b',
                                                  'e', '=', 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', '\n'};
    if (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_PIPE &&
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_PIPE &&
        GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_PIPE &&
        writeAll(GetStdHandle(STD_ERROR_HANDLE), notice))
        report.passed |= Check::Stdio;
    const auto output = encode(report);
    return writeAll(GetStdHandle(STD_OUTPUT_HANDLE), output) ? 0 : 68;
}
