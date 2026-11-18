#include "presentation/PresenterRoomHostPlayer.hpp"

#include "m0/presentation/LocalQueueListModel.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QObject>

#include <limits>

namespace music_player::m0 {

PresenterRoomHostPlayer::PresenterRoomHostPlayer(PlayerPresenter& presenter) : presenter_(presenter) {
    state_connection_ = QObject::connect(&presenter_, &PlayerPresenter::stateChanged, [this] { changed(); });
    playback_connection_ = QObject::connect(&presenter_, &PlayerPresenter::playbackChanged, [this] { changed(); });
}

PresenterRoomHostPlayer::~PresenterRoomHostPlayer() {
    QObject::disconnect(state_connection_);
    QObject::disconnect(playback_connection_);
}

RoomHostPlayerState PresenterRoomHostPlayer::state() const {
    const auto& playback = presenter_.confirmedPlayback();
    return {presenter_.playlistQueueSnapshot(), playback.status, playback.position_ms};
}

bool PresenterRoomHostPlayer::next() { return presenter_.nextTrack(); }

bool PresenterRoomHostPlayer::previous() { return presenter_.previousTrack(); }

bool PresenterRoomHostPlayer::seek(const std::int64_t position_ms) {
    return presenter_.beginSeekPreview(position_ms) && presenter_.commitSeekPreview();
}

bool PresenterRoomHostPlayer::pause() { return presenter_.pause(); }

bool PresenterRoomHostPlayer::play() { return presenter_.play(); }

std::optional<LocalQueueOccurrenceId> PresenterRoomHostPlayer::enqueue(const std::filesystem::path& source) {
    return presenter_.appendRoomSource(source);
}

bool PresenterRoomHostPlayer::remove(const LocalQueueOccurrenceId occurrence) {
    return presenter_.removeQueueOccurrence(LocalQueueListModel::occurrenceId(occurrence));
}

bool PresenterRoomHostPlayer::move(const LocalQueueOccurrenceId occurrence, const std::size_t destination_index) {
    if (destination_index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    return presenter_.moveQueueOccurrence(LocalQueueListModel::occurrenceId(occurrence),
                                          static_cast<int>(destination_index));
}

void PresenterRoomHostPlayer::changed() const {
    if (onChanged) {
        const auto notify = onChanged;
        notify();
    }
}

} // namespace music_player::m0
