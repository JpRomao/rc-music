#pragma once

#include "m0/ports/DownloadExecution.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtTypes>

#include <optional>
#include <string>

namespace music_player::m0 {
struct YoutubeResolvedAudioData final {
    QString video_id{}, stream_url{}, title{}, artist{};
    quint64 duration_ms{};
    std::optional<quint64> expected_bytes{};
    qint64 expires_at_seconds{};
    bool operator==(const YoutubeResolvedAudioData&) const = default;
};
struct YoutubeAudioResolutionResult final {
    DownloadFailure error{DownloadFailure::InvalidResult};
    std::optional<YoutubeResolvedAudioData> audio{};
};

// These functions only inspect bounded data and build argument vectors. No
// networking, process launch, filesystem access or capability is produced.
[[nodiscard]] YoutubeAudioResolutionResult decodeYoutubeAudioResolution(QByteArray payload, QString expected_video_id);
[[nodiscard]] bool validYoutubeAudioStreamUrl(QString url);
[[nodiscard]] std::optional<qint64> youtubeAudioStreamExpiry(QString url);
[[nodiscard]] QStringList youtubeAudioResolverArguments(QString video_id, QString private_node_absolute);

class YoutubeResolverSession;
// Only a resolver session may recognize process exit + EOF and mint this
// value. A valid JSON object or URL alone is never acquisition authority.
class ResolvedYoutubeAudioCapability final {
  public:
    ResolvedYoutubeAudioCapability(const ResolvedYoutubeAudioCapability&) = default;
    ResolvedYoutubeAudioCapability(ResolvedYoutubeAudioCapability&&) = default;
    ResolvedYoutubeAudioCapability& operator=(const ResolvedYoutubeAudioCapability&) = delete;
    ResolvedYoutubeAudioCapability& operator=(ResolvedYoutubeAudioCapability&&) = delete;
    [[nodiscard]] const std::string& jobId() const noexcept { return job_id_; }
    [[nodiscard]] const QString& videoId() const noexcept { return data_.video_id; }
    [[nodiscard]] const QString& streamUrl() const noexcept { return data_.stream_url; }
    [[nodiscard]] qint64 expiresAtSeconds() const noexcept { return data_.expires_at_seconds; }
    [[nodiscard]] const YoutubeResolvedAudioData& data() const noexcept { return data_; }

  private:
    friend class YoutubeResolverSession;
    ResolvedYoutubeAudioCapability(std::string job_id, YoutubeResolvedAudioData data);
    std::string job_id_;
    YoutubeResolvedAudioData data_;
};
} // namespace music_player::m0
