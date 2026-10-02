#include "adapters/WindowsNativeDownloadLease.hpp"

#include <QCryptographicHash>
#include <QScopeGuard>
#include <qt_windows.h>

#include <Sddl.h>

#include <algorithm>
#include <vector>

namespace music_player::m0 {
WindowsNativeDownloadLease::~WindowsNativeDownloadLease() { reset(); }
bool WindowsNativeDownloadLease::acquire(const QString& endpoint) {
    if (marker_ || endpoint.isEmpty() || endpoint.size() > 96 ||
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
        const auto sid = QString::fromWCharArray(text);
        // Pipes are case-insensitive; kernel object names are case-sensitive.
        // No origin, edition, config directory or CWD can separate contenders.
        const auto identity = sid.toUtf8() + QByteArray(1, '\0') + endpoint.toLatin1().toLower();
        const auto name = QStringLiteral("Global\\RCMusic.NativeDownloadOwner.v1.") +
                          QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
        const auto sddl = QStringLiteral("D:P(A;;GA;;;SY)(A;;GA;;;") + sid + QLatin1Char(')');
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1,
                                                                  &descriptor, nullptr))
            return false;
        const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        SetLastError(ERROR_SUCCESS);
        const auto marker = CreateMutexExW(&attributes, name.toStdWString().c_str(), 0, MUTEX_ALL_ACCESS);
        const auto error = GetLastError();
        if (!marker)
            return false;
        if (error == ERROR_ALREADY_EXISTS) {
            CloseHandle(marker);
            return false;
        }
        // Atomic creation, not thread-owned mutex locking: no recursive wait,
        // abandoned-lock adoption, retry, inherited handles or stale files.
        marker_ = marker;
        return true;
    } catch (...) {
        return false;
    }
}
void WindowsNativeDownloadLease::reset() noexcept {
    if (marker_)
        CloseHandle(marker_);
    marker_ = nullptr;
}
} // namespace music_player::m0
