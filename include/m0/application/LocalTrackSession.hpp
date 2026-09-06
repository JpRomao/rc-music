#pragma once

#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace music_player::m0 {

enum class LocalTrackMetadataStatus {
    Idle,
    Pending,
    Ready,
    Failed,
    Cancelled,
    Expired,
};

enum class LocalTrackStartIntent {
    Autoplay,
    StayReady,
    RestorePaused,
};

enum class LocalTrackPlaybackEventResult {
    Ignored,
    Applied,
    Ended,
    OccurrenceUnavailable,
};

class LocalTrackSession final {
  public:
    LocalTrackSession(PlaybackBackend& playback_backend, MetadataReader& metadata_reader);

    [[nodiscard]] bool open(const std::filesystem::path& source_path,
                            LocalTrackStartIntent start_intent = LocalTrackStartIntent::Autoplay);
    [[nodiscard]] PlaybackCommandResult
    openWithResult(const std::filesystem::path& source_path,
                   LocalTrackStartIntent start_intent = LocalTrackStartIntent::Autoplay);
    [[nodiscard]] bool unload();
    [[nodiscard]] bool promotePendingStartToAutoplay() noexcept;
    [[nodiscard]] LocalTrackPlaybackEventResult applyPlaybackEvent(const PlaybackEvent& event);
    [[nodiscard]] bool pollMetadata();
    [[nodiscard]] bool cancelMetadata();

    [[nodiscard]] bool hasTrack() const noexcept;
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept;
    [[nodiscard]] const std::string& displayTitle() const noexcept;
    [[nodiscard]] const std::string& artist() const noexcept;
    [[nodiscard]] const std::string& album() const noexcept;
    [[nodiscard]] LocalTrackMetadataStatus metadataStatus() const noexcept;
    [[nodiscard]] std::optional<MetadataReadErrorCode> metadataErrorCode() const noexcept;
    [[nodiscard]] MetadataGeneration metadataGeneration() const noexcept;
    [[nodiscard]] std::uint64_t playbackGeneration() const noexcept;
    [[nodiscard]] LocalTrackStartIntent startIntent() const noexcept;
    [[nodiscard]] const std::optional<TrackMetadata>& trackMetadata() const noexcept;

  private:
    [[nodiscard]] static std::string fallbackTitle(const std::filesystem::path& source_path);
    [[nodiscard]] static MetadataGeneration nextGeneration(MetadataGeneration current) noexcept;
    void attemptStart(const PlaybackSnapshot& snapshot);
    void applyMetadataCompletion(MetadataReadCompletion completion);

    PlaybackBackend& playback_backend_;
    MetadataReader& metadata_reader_;
    std::filesystem::path source_path_{};
    std::string display_title_{};
    std::string artist_{};
    std::string album_{};
    std::optional<TrackMetadata> track_metadata_{};
    std::optional<MetadataReadErrorCode> metadata_error_code_{};
    LocalTrackMetadataStatus metadata_status_{LocalTrackMetadataStatus::Idle};
    MetadataGeneration metadata_generation_{};
    std::uint64_t playback_generation_{0};
    bool has_track_{false};
    LocalTrackStartIntent start_intent_{LocalTrackStartIntent::Autoplay};
    bool start_pending_{false};
};

} // namespace music_player::m0
