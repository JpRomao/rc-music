#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

bool writeBytes(DWORD channel, const QByteArray& bytes) {
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto count = static_cast<DWORD>(std::min<qsizetype>(4096, bytes.size() - offset));
        if (!::WriteFile(::GetStdHandle(channel), bytes.constData() + offset, count, &written, nullptr) || written == 0)
            return false;
        offset += written;
    }
    return true;
}

bool writeFill(DWORD channel, quint64 count, char value) {
    const QByteArray block(4096, value);
    while (count > 0) {
        const auto length = static_cast<qsizetype>(std::min<quint64>(count, 4096));
        if (!writeBytes(channel, block.first(length)))
            return false;
        count -= static_cast<quint64>(length);
    }
    return true;
}

int inspect() {
    BOOL in_job = FALSE;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
    if (!::IsProcessInJob(::GetCurrentProcess(), nullptr, &in_job) ||
        !::QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &information, sizeof(information),
                                     nullptr))
        return 73;
    QJsonObject object{{QStringLiteral("console"), ::GetConsoleWindow() != nullptr},
                       {QStringLiteral("in_job"), in_job != FALSE},
                       {QStringLiteral("kill_on_close"),
                        (information.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0},
                       {QStringLiteral("active_process_limit"),
                        static_cast<int>(information.BasicLimitInformation.ActiveProcessLimit)},
                       {QStringLiteral("memory_limit"), QString::number(information.ProcessMemoryLimit)}};
    return writeBytes(STD_OUTPUT_HANDLE, QJsonDocument(object).toJson(QJsonDocument::Compact)) ? 0 : 74;
}

int spawnProbe(const QString& executable) {
    std::wstring command = L"\"" + executable.toStdWString() + L"\" hang";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    const bool created = ::CreateProcessW(executable.toStdWString().c_str(), command.data(), nullptr, nullptr, FALSE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE;
    if (created) {
        // Even a broken supervisor test must not leak the helper's own child.
        ::TerminateProcess(child.hProcess, 77);
        ::WaitForSingleObject(child.hProcess, 5000);
        ::CloseHandle(child.hThread);
        ::CloseHandle(child.hProcess);
    }
    return writeBytes(STD_OUTPUT_HANDLE,
                      created ? QByteArrayLiteral("SPAWN_ALLOWED") : QByteArrayLiteral("SPAWN_BLOCKED"))
               ? 0
               : 75;
}

int readInput() {
    std::array<char, 4093> block{};
    for (;;) {
        DWORD count = 0;
        if (!::ReadFile(::GetStdHandle(STD_INPUT_HANDLE), block.data(), static_cast<DWORD>(block.size()), &count,
                        nullptr))
            return ::GetLastError() == ERROR_BROKEN_PIPE ? 0 : 86;
        if (count == 0)
            return 0;
        if (!writeBytes(STD_OUTPUT_HANDLE, QByteArray(block.data(), static_cast<qsizetype>(count))))
            return 87;
        ::Sleep(1);
    }
}

int readFileHandle(const QString& encoded_handle, const QString& encoded_canary) {
    bool converted = false;
    const auto number = encoded_handle.toULongLong(&converted);
    if (!converted)
        return 89;
    const auto input = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(number));
    if (::GetFileType(input) != FILE_TYPE_DISK)
        return 90;
    DWORD read = 0;
    std::array<char, 4096> bytes{};
    if (!::ReadFile(input, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
        return 91;
    DWORD written = 0;
    const bool wrote = ::WriteFile(input, "X", 1, &written, nullptr) != FALSE;
    const auto write_error = ::GetLastError();
    const bool truncated = ::SetEndOfFile(input) != FALSE;
    const auto truncate_error = ::GetLastError();
    FILE_DISPOSITION_INFO disposition{TRUE};
    const bool deleted =
        ::SetFileInformationByHandle(input, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
    const auto delete_error = ::GetLastError();
    const auto canary_number = encoded_canary.toULongLong(&converted);
    if (converted && canary_number)
        (void)::SetEvent(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(canary_number)));
    const QJsonObject result{{QStringLiteral("data"), QString::fromLatin1(QByteArray(bytes.data(), read).toBase64())},
                             {QStringLiteral("write_denied"), !wrote && write_error == ERROR_ACCESS_DENIED},
                             {QStringLiteral("truncate_denied"), !truncated && truncate_error == ERROR_ACCESS_DENIED},
                             {QStringLiteral("delete_denied"), !deleted && delete_error == ERROR_ACCESS_DENIED}};
    return writeBytes(STD_OUTPUT_HANDLE, QJsonDocument(result).toJson(QJsonDocument::Compact)) ? 0 : 92;
}

} // namespace

int main(int argc, char* argv[]) {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() < 2)
        return 70;
    const auto mode = arguments.at(1);
    if (mode == QStringLiteral("echo"))
        return writeBytes(STD_OUTPUT_HANDLE,
                          QJsonDocument(QJsonArray::fromStringList(arguments.mid(2))).toJson(QJsonDocument::Compact))
                   ? 0
                   : 71;
    if (mode == QStringLiteral("inspect"))
        return inspect();
    if (mode == QStringLiteral("environment")) {
        const QJsonObject values{{QStringLiteral("sentinel"), qEnvironmentVariable("RC_MUSIC_PROCESS_TEST_SECRET")},
                                 {QStringLiteral("allowed"), qEnvironmentVariable("RC_MUSIC_PROCESS_TEST_ALLOWED")},
                                 {QStringLiteral("qt_plugin_path"), qEnvironmentVariable("QT_PLUGIN_PATH")},
                                 {QStringLiteral("path"), qEnvironmentVariable("PATH")},
                                 {QStringLiteral("ffreport"), qEnvironmentVariable("FFREPORT")}};
        return writeBytes(STD_OUTPUT_HANDLE, QJsonDocument(values).toJson(QJsonDocument::Compact)) ? 0 : 88;
    }
    if (mode == QStringLiteral("spawn-probe"))
        return spawnProbe(application.applicationFilePath());
    if (mode == QStringLiteral("read-stdin"))
        return readInput();
    if (mode == QStringLiteral("read-file-handle") && arguments.size() == 4)
        return readFileHandle(arguments.at(2), arguments.at(3));
    if (mode == QStringLiteral("fill") && arguments.size() == 4) {
        bool stdout_ok = false;
        bool stderr_ok = false;
        const auto stdout_count = arguments.at(2).toULongLong(&stdout_ok);
        const auto stderr_count = arguments.at(3).toULongLong(&stderr_ok);
        if (!stdout_ok || !stderr_ok || stdout_count > 1024ULL * 1024 * 1024 || stderr_count > 1024ULL * 1024 * 1024)
            return 72;
        return writeFill(STD_OUTPUT_HANDLE, stdout_count, 'A') && writeFill(STD_ERROR_HANDLE, stderr_count, 'B') ? 0
                                                                                                                 : 76;
    }
    if (mode == QStringLiteral("hang") || mode == QStringLiteral("crash")) {
        const QJsonObject ready{{QStringLiteral("ready"), true},
                                {QStringLiteral("pid"), static_cast<qint64>(::GetCurrentProcessId())}};
        if (!writeBytes(STD_OUTPUT_HANDLE, QJsonDocument(ready).toJson(QJsonDocument::Compact) + '\n'))
            return 78;
        if (mode == QStringLiteral("crash"))
            ::RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        ::Sleep(INFINITE);
        return 79;
    }
    if (mode == QStringLiteral("nonzero")) {
        if (!writeBytes(STD_OUTPUT_HANDLE, QByteArrayLiteral("OUT")) ||
            !writeBytes(STD_ERROR_HANDLE, QByteArrayLiteral("ERR")))
            return 80;
        return 17;
    }
    return 81;
}
