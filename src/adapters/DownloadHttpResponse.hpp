#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>

#include <optional>

namespace music_player::m0 {

inline constexpr qsizetype download_http_max_header_bytes = 32 * 1024;
inline constexpr qsizetype download_http_max_header_fields = 100;
inline constexpr qsizetype download_http_max_chunk_line_bytes = 1024;
inline constexpr quint64 download_http_max_chunk_framing_bytes = 1024 * 1024;

enum class DownloadHttpError { None, Malformed, LimitExceeded };

struct DownloadHttpUpdate final {
    QByteArray body{};
    bool metadataAvailable{false};
    bool finished{false};
    bool failed{false};
};

// One identity-encoded GET response; no pipelining, decompression or network access.
// The caller inspects metadata/failed before writing body, and deletes all partial
// output on failure. Only status 200 and 206 emit body (the caller validates Content-Range); other final statuses
// finish at headers and require closing the connection (their bodies are discarded). Retention is bounded framing plus
// headers; returned body never exceeds the supplied feed. EOF is successful for close-delimited bodies only when
// transport has independently proved an orderly end, including authenticated TLS closure.
class DownloadHttpResponse final {
  public:
    explicit DownloadHttpResponse(quint64 maxBodyBytes);

    [[nodiscard]] DownloadHttpUpdate feed(QByteArrayView bytes);
    [[nodiscard]] DownloadHttpUpdate finishEof();
    [[nodiscard]] int statusCode() const;
    [[nodiscard]] QByteArray header(QByteArrayView name) const;
    [[nodiscard]] std::optional<quint64> contentLength() const;
    [[nodiscard]] bool failed() const;
    [[nodiscard]] bool finished() const;
    [[nodiscard]] DownloadHttpError error() const;

  private:
    enum class State { Status, Headers, FixedBody, CloseBody, ChunkSize, ChunkBody, ChunkCrlf, Trailers, Done, Failed };
    [[nodiscard]] DownloadHttpUpdate update(QByteArray body = {}) const;
    void reject(DownloadHttpError error = DownloadHttpError::Malformed);
    void consumeLine(const QByteArray& line);
    void consumeHeader(const QByteArray& line, bool trailer);
    void completeHeaders();

    State state_{State::Status};
    DownloadHttpError error_{DownloadHttpError::None};
    quint64 max_body_bytes_;
    quint64 body_bytes_{0};
    quint64 remaining_{0};
    quint64 chunk_framing_bytes_{0};
    qsizetype header_bytes_{0};
    qsizetype header_fields_{0};
    int informational_count_{0};
    int status_code_{0};
    int chunk_crlf_bytes_{0};
    bool http_1_0_{false};
    bool metadata_available_{false};
    QByteArray line_{};
    QHash<QByteArray, QByteArray> headers_{};
    std::optional<quint64> content_length_{};
};

} // namespace music_player::m0
