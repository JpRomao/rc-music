#pragma once

#include "m0/ports/SessionStore.hpp"

namespace music_player::m0 {

[[nodiscard]] inline SessionLoadResult loadSessionForStartup(SessionStore& store, const bool restore_last_session) {
    return restore_last_session ? store.load() : SessionLoadResult{};
}

} // namespace music_player::m0
