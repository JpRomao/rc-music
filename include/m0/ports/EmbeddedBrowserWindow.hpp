#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace music_player::m0 {
class AsyncDownloadSubmission;
// Trusted composition supplies the owner window. Pages never select this handle
// or obtain access to the player, its files, downloads or application objects.
class EmbeddedBrowserWindow {
  public:
    virtual ~EmbeddedBrowserWindow() = default;
    [[nodiscard]] virtual bool show(std::uintptr_t owner_native_handle) = 0;
    // Trusted UI-thread composition only. A null port revokes admission.
    virtual void setDownloadSubmission(AsyncDownloadSubmission* submission) = 0;
    virtual void shutdown() noexcept = 0;
};

using EmbeddedBrowserWindowFactory = std::function<std::unique_ptr<EmbeddedBrowserWindow>()>;
} // namespace music_player::m0
