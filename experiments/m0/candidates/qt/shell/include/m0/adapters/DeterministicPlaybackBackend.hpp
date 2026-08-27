#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <cstdint>
#include <optional>

namespace music_player::m0 {

struct DeterministicPlaybackConfig final {
    PlaybackSnapshot initial_snapshot{PlaybackStatus::Playing, 84'000, 248'000, true};
    std::int64_t tick_interval_ms{200};
    double initial_volume{0.8};
    bool initially_muted{false};
};

struct DeterministicTickResult final {
    PlaybackSnapshot snapshot{};
    bool seek_rejected{false};
};

class DeterministicPlaybackBackend final : public PlaybackBackend {
  public:
    explicit DeterministicPlaybackBackend(DeterministicPlaybackConfig config = {});

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept;
    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] bool hasPendingSeek() const noexcept;

    void play() override;
    void pause() override;
    void seekTo(std::int64_t position_ms) override;
    void setVolume(double volume) override;
    void setMuted(bool muted) override;

    void rejectNextSeek() noexcept;
    [[nodiscard]] DeterministicTickResult tick();
    [[nodiscard]] bool enterUnavailableScenario() noexcept;
    [[nodiscard]] bool restoreInitialScenario() noexcept;

  private:
    DeterministicPlaybackConfig config_;
    PlaybackSnapshot snapshot_{};
    double volume_{0.0};
    bool muted_{false};
    bool reject_next_seek_{false};
    std::optional<std::int64_t> pending_seek_{};
};

} // namespace music_player::m0
