#pragma once

#include <cstdint>
#include <optional>

namespace music_player::m0 {
// The host calls this only for top-level navigations. Cancellation retires the
// pending navigation so its later completion cannot invalidate the current page.
class BrowserNavigationTracker final {
  public:
    void begin(const std::uint64_t navigation) noexcept { pending_ = navigation; }
    void cancel() noexcept { pending_.reset(); }
    [[nodiscard]] bool complete(const std::uint64_t navigation) noexcept {
        if (pending_ != navigation)
            return false;
        pending_.reset();
        return true;
    }
    [[nodiscard]] bool pending() const noexcept { return pending_.has_value(); }

  private:
    std::optional<std::uint64_t> pending_{};
};
} // namespace music_player::m0
