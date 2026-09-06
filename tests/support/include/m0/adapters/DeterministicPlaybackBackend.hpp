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

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override;
    void setEventCallback(PlaybackEventCallback callback) override;
    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] bool hasPendingSeek() const noexcept;

    PlaybackCommandResult load(const std::filesystem::path& source_path) override;
    PlaybackCommandResult play() override;
    PlaybackCommandResult pause() override;
    PlaybackCommandResult seekTo(std::int64_t position_ms) override;
    PlaybackCommandResult setVolume(double volume) override;
    PlaybackCommandResult setMuted(bool muted) override;
    PlaybackCommandResult unload() override;
    PlaybackCommandResult shutdown() override;

    [[nodiscard]] bool selectTrack(std::int64_t duration_ms, PlaybackStatus status);
    void rejectNextSeek() noexcept;
    [[nodiscard]] DeterministicTickResult tick();
    [[nodiscard]] bool enterUnavailableScenario();
    [[nodiscard]] bool restoreInitialScenario();

  private:
    void publishSnapshot(const PlaybackSnapshot& next);
    void finishCurrentGenerationAtEnd();
    void publishEvent(PlaybackEventType type);

    DeterministicPlaybackConfig config_;
    PlaybackSnapshot snapshot_{};
    double volume_{0.0};
    bool muted_{false};
    bool reject_next_seek_{false};
    std::optional<std::int64_t> pending_seek_{};
    std::uint64_t generation_{0};
    PlaybackEventCallback event_callback_{};
    bool ended_emitted_{false};
    bool shutdown_{false};
};

} // namespace music_player::m0
