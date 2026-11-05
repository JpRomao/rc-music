#pragma once

#include "m0/ports/DownloadExecution.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace music_player::m0 {
inline constexpr qsizetype youtube_broker_max_frame_bytes = 16 * 1024 * 1024;
inline constexpr qsizetype youtube_broker_max_player_bytes = 8 * 1024 * 1024;
inline constexpr qsizetype youtube_broker_max_response_bytes = 1024 * 1024;
inline constexpr qsizetype youtube_broker_max_value_bytes = 16 * 1024;
inline constexpr qsizetype youtube_broker_max_script_bytes = 4 * 1024 * 1024;
inline constexpr quint64 youtube_broker_max_traffic_bytes = 64ULL * 1024 * 1024;
inline constexpr quint32 youtube_broker_max_groups = 8;
inline constexpr std::size_t youtube_broker_max_requests = 64;
inline constexpr qsizetype youtube_broker_max_challenges = 256;

struct YoutubeBrokerRequest final {
    QString type{};
    QStringList challenges{};
    bool operator==(const YoutubeBrokerRequest&) const = default;
};
struct YoutubeBrokerChallenge final {
    QString player{};
    std::vector<YoutubeBrokerRequest> requests{};
    bool operator==(const YoutubeBrokerChallenge&) const = default;
};
enum class YoutubeBrokerMessageKind { Challenge, Result, Failure };
struct YoutubeBrokerMessage final {
    YoutubeBrokerMessageKind kind{YoutubeBrokerMessageKind::Failure};
    quint32 sequence{};
    std::optional<YoutubeBrokerChallenge> challenge{};
    QByteArray result_json{};
    DownloadFailure failure{DownloadFailure::None};
};
struct YoutubeBrokerDecodeResult final {
    DownloadFailure error{DownloadFailure::InvalidResult};
    std::optional<YoutubeBrokerMessage> message{};
};

// Data-only, strict UTF-8/JSON codecs. Neither scripts, frames nor a valid
// projection grant process, network or publication authority.
[[nodiscard]] YoutubeBrokerDecodeResult decodeYoutubeBrokerMessage(QByteArray payload, quint32 expected_sequence);
// Normalizes the EJS output to the strict correlated response wire frame.
// Per-request errors become a fixed unavailable code; free-form error text
// is never returned to the extractor or product. Returns length-prefixed JSON.
[[nodiscard]] std::optional<QByteArray>
encodeYoutubeBrokerResponse(quint32 sequence, const YoutubeBrokerChallenge& challenge, QByteArray node_output);
// Trusted caller supplies authenticated EJS resources. Player/challenges are
// serialized as data, output_preprocessed is false, and no path is accepted.
// Returns a complete bounded program for Node stdin, not launch authority.
[[nodiscard]] std::optional<QByteArray> youtubeBrokerNodeInput(const YoutubeBrokerChallenge& challenge,
                                                               QByteArray ejs_core, QByteArray ejs_library);

// One outstanding frame, stop-and-wait protocol. The four-byte little-endian
// length is checked before payload allocation. Pipelined/trailing data fails.
class YoutubeBrokerFrameReader final {
  public:
    [[nodiscard]] bool append(QByteArrayView bytes);
    [[nodiscard]] std::optional<QByteArray> take();
    // Seals input and rejects incomplete header/body. A complete untaken frame
    // may still be taken; no further bytes may be appended after sealing.
    [[nodiscard]] bool finish();
    [[nodiscard]] bool failed() const noexcept;

  private:
    std::array<char, 4> header_{};
    qsizetype header_size_{};
    quint32 expected_bytes_{};
    QByteArray payload_{};
    bool failed_{}, sealed_{};
};
} // namespace music_player::m0
