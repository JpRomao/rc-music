#pragma once

#include "m0/ports/YoutubeAudioSubmission.hpp"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <optional>
#include <string_view>

namespace music_player::m0 {
inline constexpr std::size_t youtube_audio_max_items = 20;
inline constexpr std::size_t youtube_audio_max_payload_bytes = 64 * 1024;

struct YoutubeAudioDecodingResult final {
    DownloadSubmissionError error{DownloadSubmissionError::InvalidRequest};
    YoutubeAudioRequest request{};
};

// Bounded wire codecs only. No filesystem, networking, media resolution,
// Native Messaging registration, process launch or caller authorization.
[[nodiscard]] YoutubeAudioDecodingResult decodeYoutubeAudioRequest(const QByteArray& payload);
[[nodiscard]] QByteArray encodeYoutubeAudioRequest(const YoutubeAudioRequest& request);
[[nodiscard]] QByteArray encodeYoutubeAudioResponse(const std::string& request_id,
                                                    const DownloadSubmissionResult& result);
[[nodiscard]] std::optional<DownloadSubmissionResult> decodeYoutubeAudioResponse(const QByteArray& payload,
                                                                                 std::string_view expected_request_id,
                                                                                 std::size_t expected_item_count);
// Empty for an invalid opaque ID; no caller-controlled host, query or shell.
[[nodiscard]] QString canonicalYoutubeWatchUrl(std::string_view video_id);
// The lyrics page rule of a request item: an https Letras page, without query.
[[nodiscard]] bool validYoutubeLyricsPage(const QString& page);
} // namespace music_player::m0
