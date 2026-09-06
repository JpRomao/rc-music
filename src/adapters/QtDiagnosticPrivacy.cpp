#include "adapters/QtDiagnosticPrivacy.hpp"

#include <QtCore/qlogging.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#endif

namespace music_player::m0 {
namespace {

void discardQtDiagnostic(QtMsgType, const QMessageLogContext&, const QString&) noexcept {}

bool installDiagnosticPrivacy() noexcept {
#ifdef Q_OS_WIN
    // Keep a separate Win32 sink alive until process teardown, independent of
    // the CRT's stderr lifetime. Do not inherit it into unrelated child processes.
    const HANDLE sink = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sink == INVALID_HANDLE_VALUE)
        return false;

    FILE* redirected_stderr = nullptr;
    if (_wfreopen_s(&redirected_stderr, L"NUL", L"w", stderr) != 0 || redirected_stderr == nullptr) {
        ::CloseHandle(sink);
        return false;
    }
    if (::SetStdHandle(STD_ERROR_HANDLE, sink) == FALSE) {
        ::CloseHandle(sink);
        return false;
    }

    // Never forward to or restore a previous handler: external diagnostics may
    // contain user media data, including during destructors and codec teardown.
    static_cast<void>(qInstallMessageHandler(discardQtDiagnostic));
    return true;
#else
    // The complete native stderr policy is Windows-only in this increment.
    return false;
#endif
}

} // namespace

bool installProcessDiagnosticPrivacy() noexcept {
    // Startup installs once; a failure is permanent and requires the caller to
    // stop before reading user files. No process-lifetime state is restored.
    static const bool installed = installDiagnosticPrivacy();
    return installed;
}

} // namespace music_player::m0
