#pragma once

#include "m0/ports/DownloadExecution.hpp"

namespace music_player::m0::download_execution_detail {
// Internal shared validation for worker composition and its outer executor.
// A malformed update never acknowledges native termination.
[[nodiscard]] bool validUpdate(const DownloadExecutionUpdate& update, const std::string& job_id);
} // namespace music_player::m0::download_execution_detail
