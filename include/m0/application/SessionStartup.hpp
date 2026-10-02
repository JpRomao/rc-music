#pragma once

#include "m0/ports/SessionStore.hpp"

namespace music_player::m0 {

[[nodiscard]] inline SessionLoadResult loadSessionForStartup(SessionStore& store, const bool restore_last_session,
                                                             const bool preserve_import_receipts = false) {
    // Optional durable import metadata is independent of queue restoration.
    // Keep the complete persisted snapshot for the writer; only the presenter
    // may decide to hydrate its queue, and disabling restore never autoplays.
    return restore_last_session || preserve_import_receipts ? store.load() : SessionLoadResult{};
}

} // namespace music_player::m0
