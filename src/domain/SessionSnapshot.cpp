#include "m0/domain/SessionSnapshot.hpp"

namespace music_player::m0 {

bool isValidSessionSnapshot(const SessionSnapshot& snapshot) {
    if (!isValidLocalFileQueueSnapshot(snapshot.queue) || snapshot.position_ms < 0) {
        return false;
    }
    return snapshot.queue.current_occurrence_id.has_value() || snapshot.position_ms == 0;
}

} // namespace music_player::m0
