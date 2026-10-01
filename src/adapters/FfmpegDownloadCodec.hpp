#pragma once

#include "m0/ports/DownloadExecution.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QSet>
#include <QStringList>
#include <optional>

namespace music_player::m0 {

enum class DownloadContainer { Mp3, Mp4, Wav, Flac, Ogg, Matroska };
struct DownloadAudioInspection final {
    DownloadContainer container{DownloadContainer::Mp3};
    QString codec{};
    std::optional<quint64> duration_us{};
};
struct DownloadAudioProbe final {
    std::optional<DownloadAudioInspection> audio{};
    DownloadFailure error{DownloadFailure::UnsupportedMedia};
};

// Trusted local, sealed job paths only. Returned values are argument vectors,
// never shell text. Inspection selects one audio stream and excludes tags/paths.
[[nodiscard]] QStringList downloadProbeArguments(const QString& local_input);
[[nodiscard]] QStringList downloadConversionArguments(const QString& local_input, DownloadContainer container);
[[nodiscard]] QStringList downloadValidationArguments(const QString& local_mp3);
[[nodiscard]] DownloadAudioProbe decodeDownloadAudioProbe(QByteArrayView json, bool require_mp3 = false);

struct DownloadConversionProgress final {
    std::optional<quint64> processed_us{};
    bool ended{false};
};

// A bounded -progress pipe:2 decoder. End is only progress metadata, not proof
// of successful conversion or validated audio. The owner also checks process exit.
class DownloadProgressCodec final {
  public:
    bool feed(QByteArrayView bytes);
    bool finishEof();
    [[nodiscard]] bool failed() const;
    [[nodiscard]] DownloadConversionProgress snapshot() const;

  private:
    bool consumeLine();
    QByteArray line_{};
    QSet<QByteArray> block_keys_{};
    std::optional<quint64> block_time_{};
    DownloadConversionProgress snapshot_{};
    quint64 received_bytes_{0};
    qsizetype block_bytes_{0};
    bool failed_{false};
};

} // namespace music_player::m0
