#include "m0/application/LocalTrackSession.hpp"

#include <limits>
#include <utility>
#include <variant>

namespace music_player::m0 {

LocalTrackSession::LocalTrackSession(PlaybackBackend& playback_backend, MetadataReader& metadata_reader)
    : playback_backend_(playback_backend), metadata_reader_(metadata_reader) {}

bool LocalTrackSession::open(const std::filesystem::path& source_path) {
    if (source_path.empty() || playback_backend_.load(source_path) != PlaybackCommandResult::Accepted) {
        return false;
    }

    const auto next_metadata_generation = nextGeneration(metadata_generation_);
    if (metadata_generation_.value != 0) {
        (void)metadata_reader_.cancelRead(metadata_generation_);
    }

    source_path_ = source_path;
    display_title_ = fallbackTitle(source_path);
    artist_.clear();
    album_.clear();
    track_metadata_.reset();
    metadata_generation_ = next_metadata_generation;
    playback_generation_ = playback_backend_.snapshot().generation;
    has_track_ = true;
    autoplay_pending_ = true;

    const auto metadata_result = metadata_reader_.startRead(MetadataReadRequest{metadata_generation_, source_path_});
    metadata_status_ = metadata_result == MetadataCommandResult::Accepted ? LocalTrackMetadataStatus::Pending
                                                                          : LocalTrackMetadataStatus::Failed;

    attemptAutoplay(playback_backend_.snapshot());
    return true;
}

void LocalTrackSession::applyPlaybackEvent(const PlaybackEvent& event) {
    if (!has_track_ || event.snapshot.generation != playback_generation_) {
        return;
    }

    if (event.snapshot.status == PlaybackStatus::Empty || event.snapshot.status == PlaybackStatus::Error) {
        autoplay_pending_ = false;
        return;
    }

    attemptAutoplay(event.snapshot);
}

bool LocalTrackSession::pollMetadata() {
    if (!has_track_ || metadata_generation_.value == 0 || metadata_status_ != LocalTrackMetadataStatus::Pending) {
        return false;
    }

    auto result = metadata_reader_.poll(metadata_generation_);
    switch (result.status) {
    case MetadataPollStatus::Pending:
        return false;
    case MetadataPollStatus::Completed:
        if (!result.completion.has_value() || result.completion->generation != metadata_generation_ ||
            result.completion->source_path != source_path_) {
            metadata_status_ = LocalTrackMetadataStatus::Failed;
            return true;
        }
        applyMetadataCompletion(std::move(*result.completion));
        return true;
    case MetadataPollStatus::Cancelled:
        metadata_status_ = LocalTrackMetadataStatus::Cancelled;
        return true;
    case MetadataPollStatus::Expired:
        metadata_status_ = LocalTrackMetadataStatus::Expired;
        return true;
    case MetadataPollStatus::InvalidArgument:
    case MetadataPollStatus::StaleGeneration:
    case MetadataPollStatus::Shutdown:
    case MetadataPollStatus::Idle:
        metadata_status_ = LocalTrackMetadataStatus::Failed;
        return true;
    }
    metadata_status_ = LocalTrackMetadataStatus::Failed;
    return true;
}

bool LocalTrackSession::cancelMetadata() {
    if (metadata_status_ != LocalTrackMetadataStatus::Pending || metadata_generation_.value == 0) {
        return false;
    }
    if (metadata_reader_.cancelRead(metadata_generation_) != MetadataCommandResult::Accepted) {
        return false;
    }
    metadata_status_ = LocalTrackMetadataStatus::Cancelled;
    return true;
}

bool LocalTrackSession::hasTrack() const noexcept { return has_track_; }

const std::filesystem::path& LocalTrackSession::sourcePath() const noexcept { return source_path_; }

const std::string& LocalTrackSession::displayTitle() const noexcept { return display_title_; }

const std::string& LocalTrackSession::artist() const noexcept { return artist_; }

const std::string& LocalTrackSession::album() const noexcept { return album_; }

LocalTrackMetadataStatus LocalTrackSession::metadataStatus() const noexcept { return metadata_status_; }

MetadataGeneration LocalTrackSession::metadataGeneration() const noexcept { return metadata_generation_; }

std::uint64_t LocalTrackSession::playbackGeneration() const noexcept { return playback_generation_; }

const std::optional<TrackMetadata>& LocalTrackSession::trackMetadata() const noexcept { return track_metadata_; }

std::string LocalTrackSession::fallbackTitle(const std::filesystem::path& source_path) {
    auto fallback = source_path.stem();
    if (fallback.empty()) {
        fallback = source_path.filename();
    }
    const auto utf8 = fallback.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

MetadataGeneration LocalTrackSession::nextGeneration(const MetadataGeneration current) noexcept {
    if (current.value == std::numeric_limits<std::uint64_t>::max()) {
        return MetadataGeneration{1};
    }
    return MetadataGeneration{current.value + 1};
}

void LocalTrackSession::attemptAutoplay(const PlaybackSnapshot& snapshot) {
    if (!autoplay_pending_ || snapshot.generation != playback_generation_ || snapshot.status != PlaybackStatus::Ready) {
        return;
    }
    if (playback_backend_.play() == PlaybackCommandResult::Accepted) {
        autoplay_pending_ = false;
    }
}

void LocalTrackSession::applyMetadataCompletion(MetadataReadCompletion completion) {
    if (auto* metadata = std::get_if<TrackMetadata>(&completion.result); metadata != nullptr) {
        if (metadata->source_path != source_path_) {
            track_metadata_.reset();
            metadata_status_ = LocalTrackMetadataStatus::Failed;
            return;
        }
        track_metadata_ = std::move(*metadata);
        if (!track_metadata_->display_title.empty()) {
            display_title_ = track_metadata_->display_title;
        } else if (track_metadata_->tags.title.has_value() && !track_metadata_->tags.title->empty()) {
            display_title_ = *track_metadata_->tags.title;
        }
        artist_ = track_metadata_->tags.artist.value_or(std::string{});
        album_ = track_metadata_->tags.album.value_or(std::string{});
        metadata_status_ = LocalTrackMetadataStatus::Ready;
        return;
    }

    track_metadata_.reset();
    metadata_status_ = LocalTrackMetadataStatus::Failed;
}

} // namespace music_player::m0
