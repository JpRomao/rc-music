#include "adapters/WindowsWebView2Options.hpp"

// Keep the SDK implementation before any Qt __has_attribute fallback on MSVC.
#include <WebView2EnvironmentOptions.h>

namespace music_player::m0 {
Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions> makeWindowsWebView2Options() noexcept {
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions2> exclusive;
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions6> extensions;
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions7> channels;
    if (!options || FAILED(options.As(&exclusive)) || FAILED(options.As(&extensions)) ||
        FAILED(options.As(&channels)) || FAILED(options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE)) ||
        FAILED(exclusive->put_ExclusiveUserDataFolderAccess(TRUE)) ||
        FAILED(extensions->put_AreBrowserExtensionsEnabled(FALSE)) ||
        FAILED(channels->put_ReleaseChannels(COREWEBVIEW2_RELEASE_CHANNELS_STABLE)))
        return {};
    return options;
}
} // namespace music_player::m0
