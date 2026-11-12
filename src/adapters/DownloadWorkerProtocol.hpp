#pragma once

#include "PinnedDownloadStream.hpp"
#include "WindowsDownloadTlsRuntime.hpp"

#include <QList>

#include <optional>

namespace music_player::m0 {

inline constexpr qsizetype download_worker_max_request_bytes = 256 * 1024;
inline constexpr qsizetype download_worker_max_data_bytes = 32 * 1024;
inline constexpr qsizetype download_worker_max_frame_bytes = download_worker_max_data_bytes + 1;

enum class DownloadWorkerMode : quint8 { Resolve = 1, Stream = 2 };
enum class DownloadWorkerFrameKind : quint8 { Addresses = 1, Data = 2, Terminal = 3 };
enum class DownloadWorkerError : quint8 { None = 0, InvalidRequest = 1, Runtime = 2, Dns = 3, Connection = 4, Tls = 5 };

// One private bounded binary request + EOF. No request data is exposed in argv,
// environment, diagnostics or logs. Pins are trusted composition, never page input.
// RCMD1 remains byte-identical for minimal requests. RCMD2 is Stream-only and
// adds one validated nonempty User-Agent field before port/TLS, never a header map.
struct DownloadWorkerRequest final {
    DownloadWorkerMode mode{DownloadWorkerMode::Resolve};
    WindowsDownloadTlsRuntimeConfig runtime{};
    QString host{};
    PinnedDownloadEndpoint endpoint{};
};

struct DownloadWorkerFrame final {
    DownloadWorkerFrameKind kind{DownloadWorkerFrameKind::Terminal};
    QList<QHostAddress> addresses{};
    QByteArray data{};
    DownloadWorkerError error{DownloadWorkerError::None};
    // Explicit transport observation only; exit code 0 never implies this bit.
    bool authenticated_eof{false};
};

[[nodiscard]] QByteArray encodeDownloadWorkerRequest(const DownloadWorkerRequest& request);
[[nodiscard]] std::optional<DownloadWorkerRequest> decodeDownloadWorkerRequest(const QByteArray& bytes);
[[nodiscard]] QByteArray encodeDownloadWorkerFrame(const DownloadWorkerFrame& frame);
[[nodiscard]] std::optional<DownloadWorkerFrame> decodeDownloadWorkerFrame(const QByteArray& body);

// Frames are little-endian uint32 body length + uint8 kind + payload. Body is
// <=32769 bytes. Decoder consumes only through one frame, retaining at most
// 32769 body bytes + 4 length bytes (excluding allocator overhead);
// caller owns mode/ordering validation and backpressure between frames.
class DownloadWorkerFrameDecoder final {
  public:
    [[nodiscard]] qsizetype append(QByteArrayView bytes);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::optional<DownloadWorkerFrame> take();
    [[nodiscard]] bool incomplete() const noexcept;

  private:
    QByteArray length_{};
    QByteArray body_{};
    quint32 expected_{0};
    bool failed_{false};
};

} // namespace music_player::m0
