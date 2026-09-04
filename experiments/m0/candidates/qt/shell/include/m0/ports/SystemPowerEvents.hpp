#pragma once

#include <functional>

namespace music_player::m0 {

enum class SystemPowerEvent {
    Suspending,
    Resumed,
};

using SystemPowerEventHandler = std::function<void(SystemPowerEvent)>;

class SystemPowerEvents {
  public:
    virtual ~SystemPowerEvents() = default;

    [[nodiscard]] virtual bool available() const noexcept = 0;
    // Native notifications are delivered synchronously on the application/UI thread.
    virtual void setEventHandler(SystemPowerEventHandler handler) = 0;
    virtual void shutdown() noexcept = 0;
};

} // namespace music_player::m0
