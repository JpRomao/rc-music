#pragma once

#include "m0/ports/DownloadSubmission.hpp"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <optional>
#include <string_view>

namespace music_player::m0 {

struct NativeDownloadDecodingResult final {
    DownloadSubmissionError error{DownloadSubmissionError::InvalidRequest};
    DownloadRequest request{};
};

// Shared canonical direct-media URL policy; DNS/connection/redirect checks remain separate.
[[nodiscard]] bool isAllowedDirectMediaUrl(const QString& raw);

// Wire payload only: Native Messaging framing and caller authorization belong to the session.
[[nodiscard]] NativeDownloadDecodingResult decodeNativeDownloadRequest(const QByteArray& payload);
// Serialization preserves the validated request strings, including signed URL/query bytes.
[[nodiscard]] QByteArray encodeNativeDownloadRequest(const DownloadRequest& request);
// Invalid identifiers/results cannot produce a protocol response.
[[nodiscard]] QByteArray encodeNativeDownloadResponse(const std::string& request_id,
                                                      const DownloadSubmissionResult& result);
// A malformed or uncorrelated reply is uncertainty, never a fabricated rejection/ACK.
[[nodiscard]] std::optional<DownloadSubmissionResult> decodeNativeDownloadResponse(const QByteArray& payload,
                                                                                   std::string_view expected_request_id,
                                                                                   std::size_t expected_item_count);

} // namespace music_player::m0
