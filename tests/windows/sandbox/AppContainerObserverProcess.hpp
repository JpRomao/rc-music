#pragma once

#include "AppContainerObserverIdentity.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace music_player::sandbox_probe {
// Probe-only witness for an explicitly opened process object. Its lifetime is
// independent of the peer: destruction closes this witness and never kills it.
class AppContainerObserverProcess final {
  public:
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t maximum_image_path_units = 32767;
    struct Snapshot final {
        bool complete{false};
        std::uint32_t error{not_called}, pid{};
        std::uint64_t creation_filetime{};
        std::wstring image_path{};
        AppContainerObserverIdentity::Snapshot token{};
    };
    enum class State { Running, Stopped, Unknown };
    struct Poll final {
        State state{State::Unknown};
        std::uint32_t error{not_called};
        std::optional<std::uint32_t> exit_code{};
    };

    explicit AppContainerObserverProcess(std::uint32_t pid) noexcept;
    ~AppContainerObserverProcess();
    AppContainerObserverProcess(const AppContainerObserverProcess&) = delete;
    AppContainerObserverProcess& operator=(const AppContainerObserverProcess&) = delete;
    AppContainerObserverProcess(AppContainerObserverProcess&&) = delete;
    AppContainerObserverProcess& operator=(AppContainerObserverProcess&&) = delete;
    [[nodiscard]] const Snapshot& open() noexcept;
    [[nodiscard]] const Snapshot& snapshot() const noexcept;
    [[nodiscard]] Poll poll() const noexcept;

  private:
    std::uintptr_t handle_{};
    bool attempted_{false};
    Snapshot snapshot_{};
};
} // namespace music_player::sandbox_probe
