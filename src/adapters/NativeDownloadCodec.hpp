#pragma once

#include "m0/ports/DownloadSubmission.hpp"

#include <QByteArray>
#include <QString>

namespace music_player::m0 {

struct NativeDownloadDecodingResult final {
    DownloadSubmissionError error{DownloadSubmissionError::InvalidRequest};
    DownloadRequest request{};
};

// Shared canonical direct-media URL policy; DNS/connection/redirect checks remain separate.
[[nodiscard]] bool isAllowedDirectMediaUrl(const QString& raw);

// Wire payload only: Native Messaging framing and caller authorization belong to the session.
[[nodiscard]] NativeDownloadDecodingResult decodeNativeDownloadRequest(const QByteArray& payload);
// Invalid identifiers/results cannot produce a protocol response.
[[nodiscard]] QByteArray encodeNativeDownloadResponse(const std::string& request_id,
                                                      const DownloadSubmissionResult& result);

} // namespace music_player::m0
