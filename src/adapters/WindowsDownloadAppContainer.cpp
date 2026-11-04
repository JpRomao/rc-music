#include "WindowsDownloadAppContainer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <utility>

namespace music_player::m0 {
WindowsDownloadAppContainer::WindowsDownloadAppContainer(QByteArray sid,
                                                         std::shared_ptr<WindowsDownloadNativeUseFence> fence)
    : sid_(std::move(sid)), native_use_fence_(std::move(fence)) {}
std::shared_ptr<const WindowsDownloadAppContainer>
WindowsDownloadAppContainer::fromTrustedSid(QByteArray binary_sid,
                                            std::shared_ptr<WindowsDownloadNativeUseFence> fence) {
    try {
        constexpr auto size = 8 + SECURITY_APP_PACKAGE_RID_COUNT * sizeof(DWORD);
        if (binary_sid.size() != static_cast<qsizetype>(size) ||
            static_cast<unsigned char>(binary_sid.at(0)) != SID_REVISION ||
            static_cast<unsigned char>(binary_sid.at(1)) != SECURITY_APP_PACKAGE_RID_COUNT)
            return {};
        // A QByteArray may borrow external storage. Copy even that representation
        // so the SID pointer remains immutable throughout CreateProcess attrs.
        QByteArray owned{binary_sid.constData(), binary_sid.size()};
        auto* sid = static_cast<PSID>(owned.data());
        const SID_IDENTIFIER_AUTHORITY authority = SECURITY_APP_PACKAGE_AUTHORITY;
        if (!::IsValidSid(sid) || ::GetLengthSid(sid) != size ||
            std::memcmp(::GetSidIdentifierAuthority(sid), &authority, sizeof(authority)) != 0 ||
            *::GetSidSubAuthority(sid, 0) != SECURITY_APP_PACKAGE_BASE_RID)
            return {};
        return std::shared_ptr<const WindowsDownloadAppContainer>(
            new WindowsDownloadAppContainer(std::move(owned), std::move(fence)));
    } catch (...) {
        return {};
    }
}
const QByteArray& WindowsDownloadAppContainer::sidBytes() const noexcept { return sid_; }
std::shared_ptr<WindowsDownloadNativeUseFence> WindowsDownloadAppContainer::nativeUseFence() const noexcept {
    return native_use_fence_;
}
} // namespace music_player::m0
