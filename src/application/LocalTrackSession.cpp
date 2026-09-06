#include "m0/application/LocalTrackSession.hpp"

#include <limits>
#include <utility>
#include <variant>

namespace music_player::m0 {

LocalTrackSession::LocalTrackSession(PlaybackBackend& playback_backend, MetadataReader& metadata_reader)
    : playback_backend_(playback_backend), metadata_reader_(metadata_reader) {}

bool LocalTrackSession::open(const std::filesystem::path& source_path, const LocalTrackStartIntent start_intent) {
    return openWithResult(source_path, start_intent) == PlaybackCommandResult::Accepted;
}

PlaybackCommandResult LocalTrackSession::openWithResult(const std::filesystem::path& source_path,
                                                        const LocalTrackStartIntent start_intent) {
    if (source_path.empty())
        return PlaybackCommandResult::InvalidArgument;
    const PlaybackCommandResult load_result = playback_backend_.load(source_path);
    if (load_result != PlaybackCommandResult::Accepted)
        return load_result;

    const auto next_metadata_generation = nextGeneration(metadata_generation_);
    if (metadata_generation_.value != 0) {
        (void)metadata_reader_.cancelRead(metadata_generation_);
    }

    source_path_ = source_path;
    display_title_ = fallbackTitle(source_path);
    artist_.clear();
    album_.clear();
    track_metadata_.reset();
    metadata_error_code_.reset();
    metadata_generation_ = next_metadata_generation;
    playback_generation_ = playback_backend_.snapshot().generation;
    has_track_ = true;
    start_intent_ = start_intent;
    start_pending_ = true;

    const auto metadata_result = metadata_reader_.startRead(MetadataReadRequest{metadata_generation_, source_path_});
    metadata_status_ = metadata_result == MetadataCommandResult::Accepted ? LocalTrackMetadataStatus::Pending
                                                                          : LocalTrackMetadataStatus::Failed;

    attemptStart(playback_backend_.snapshot());
    return PlaybackCommandResult::Accepted;
}

bool LocalTrackSession::unload() {
    if (!has_track_) {
        metadata_error_code_.reset();
        return true;
    }
    if (playback_backend_.unload() != PlaybackCommandResult::Accepted) {
        return false;
    }
    if (metadata_status_ == LocalTrackMetadataStatus::Pending && metadata_generation_.value != 0) {
        (void)metadata_reader_.cancelRead(metadata_generation_);
    }
    source_path_.clear();
    display_title_.clear();
    artist_.clear();
    album_.clear();
    track_metadata_.reset();
    metadata_error_code_.reset();
    metadata_status_ = LocalTrackMetadataStatus::Idle;
    playback_generation_ = playback_backend_.snapshot().generation;
    has_track_ = false;
    start_intent_ = LocalTrackStartIntent::Autoplay;
    start_pending_ = false;
    return true;
}

bool LocalTrackSession::promotePendingStartToAutoplay() noexcept {
    if (!has_track_ || !start_pending_) {
        return false;
    }
    start_intent_ = LocalTrackStartIntent::Autoplay;
    return true;
}

LocalTrackPlaybackEventResult LocalTrackSession::applyPlaybackEvent(const PlaybackEvent& event) {
    if (!has_track_ || event.snapshot.generation != playback_generation_) {
        return LocalTrackPlaybackEventResult::Ignored;
    }

    if (event.type == PlaybackEventType::Ended) {
        start_pending_ = false;
        return LocalTrackPlaybackEventResult::Ended;
    }

    if (event.snapshot.status == PlaybackStatus::Empty) {
        start_pending_ = false;
        return LocalTrackPlaybackEventResult::Applied;
    }

    if (event.snapshot.status == PlaybackStatus::Error) {
        start_pending_ = false;
        switch (event.snapshot.error) {
        case PlaybackErrorCode::SourceUnavailable:
        case PlaybackErrorCode::InvalidMedia:
        case PlaybackErrorCode::AccessDenied:
            return LocalTrackPlaybackEventResult::OccurrenceUnavailable;
        case PlaybackErrorCode::None:
        case PlaybackErrorCode::NetworkFailure:
        case PlaybackErrorCode::BackendFailure:
        case PlaybackErrorCode::AudioOutputUnavailable:
        case PlaybackErrorCode::AudioOutputChanged:
            return LocalTrackPlaybackEventResult::Applied;
        }
        return LocalTrackPlaybackEventResult::Applied;
    }

    attemptStart(event.snapshot);
    return LocalTrackPlaybackEventResult::Applied;
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
        if (!result.completion.has_value()) {
            metadata_status_ = LocalTrackMetadataStatus::Failed;
            return true;
        }
        if (result.completion->generation != metadata_generation_ || result.completion->source_path != source_path_) {
            return false;
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

std::optional<MetadataReadErrorCode> LocalTrackSession::metadataErrorCode() const noexcept {
    return metadata_error_code_;
}

MetadataGeneration LocalTrackSession::metadataGeneration() const noexcept { return metadata_generation_; }

std::uint64_t LocalTrackSession::playbackGeneration() const noexcept { return playback_generation_; }

LocalTrackStartIntent LocalTrackSession::startIntent() const noexcept { return start_intent_; }

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

void LocalTrackSession::attemptStart(const PlaybackSnapshot& snapshot) {
    if (!start_pending_ || snapshot.generation != playback_generation_ || snapshot.status != PlaybackStatus::Ready) {
        return;
    }
    if (start_intent_ == LocalTrackStartIntent::StayReady) {
        start_pending_ = false;
        return;
    }
    const PlaybackCommandResult result =
        start_intent_ == LocalTrackStartIntent::RestorePaused ? playback_backend_.pause() : playback_backend_.play();
    if (result == PlaybackCommandResult::Accepted) {
        start_pending_ = false;
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
        metadata_error_code_.reset();
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
    metadata_error_code_ = std::get<MetadataReadError>(completion.result).code;
    metadata_status_ = LocalTrackMetadataStatus::Failed;
}

} // namespace music_player::m0
