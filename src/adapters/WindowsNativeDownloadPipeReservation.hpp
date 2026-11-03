#pragma once

#include <QString>

namespace music_player::m0 {
// Reserves the first native instance before Qt opens its serving instances.
// A private self-connected pair keeps the reservation unavailable to clients,
// including while ingress is stopped and its borrowed owner is still draining.
// This is namespace ownership, not authentication of other same-user processes.
class WindowsNativeDownloadPipeReservation final {
  public:
    WindowsNativeDownloadPipeReservation() = default;
    ~WindowsNativeDownloadPipeReservation();
    WindowsNativeDownloadPipeReservation(const WindowsNativeDownloadPipeReservation&) = delete;
    WindowsNativeDownloadPipeReservation& operator=(const WindowsNativeDownloadPipeReservation&) = delete;

    [[nodiscard]] bool acquire(const QString& endpoint);
    void reset() noexcept;

  private:
    void* server_{nullptr};
    void* client_{nullptr};
};
} // namespace music_player::m0
