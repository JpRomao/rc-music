#pragma once

#include "m0/ports/DownloadSubmission.hpp"

namespace music_player::m0 {

// Fast deterministic identity only: no filesystem/network or callbacks into the
// owner. Return 64 lowercase SHA-256 hex digits of the v1 typed batch encoding.
// Borrowed by DownloadService; composition must keep it alive until service teardown.
class DownloadRequestIdentity {
  public:
    virtual ~DownloadRequestIdentity() = default;
    [[nodiscard]] virtual std::string fingerprint(const DownloadRequest& request) = 0;
};

} // namespace music_player::m0
