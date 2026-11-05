#pragma once

#include "adapters/YoutubeResolverSession.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace music_player::m0::test {
inline QByteArray
youtubeResolverFixtureJson(const QString& video_id = QStringLiteral("aB0-_Cd1EF2"),
                           const QString& url = QStringLiteral(
                               "https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A%2fb%2F&x=1")) {
    return QJsonDocument(QJsonObject{{"_type", "video"},
                                     {"id", video_id},
                                     {"extractor_key", "Youtube"},
                                     {"ext", "m4a"},
                                     {"acodec", "mp4a.40.2"},
                                     {"vcodec", "none"},
                                     {"protocol", "https"},
                                     {"duration", 123.456},
                                     {"filesize", 1024},
                                     {"availability", "public"},
                                     {"is_live", false},
                                     {"live_status", "not_live"},
                                     {"has_drm", false},
                                     {"url", url},
                                     {"title", "Synthetic song"},
                                     {"artist", "Synthetic artist"},
                                     {"http_headers", QJsonObject{}}})
        .toJson(QJsonDocument::Compact);
}
class FixtureYoutubeResolverProcess final : public YoutubeResolverProcess {
  public:
    QByteArray output{youtubeResolverFixtureJson()};
    bool start(const QString&, qint64) override { return true; }
    YoutubeResolverProcessUpdate poll(qint64) override {
        return {.state = YoutubeResolverProcessState::Exited, .stdout_chunk = output, .exit_code = 0};
    }
    void cancel() override {}
};
// Tests obtain capabilities through the same session and exit/EOF contract as
// production. This is synthetic evidence, never qualification of a live source.
inline std::shared_ptr<const ResolvedYoutubeAudioCapability>
resolvedYoutubeFixture(const std::string& job_id = "youtube_job",
                       const QString& video_id = QStringLiteral("aB0-_Cd1EF2"),
                       const QString& url = QStringLiteral(
                           "https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A%2fb%2F&x=1")) {
    FixtureYoutubeResolverProcess process;
    process.output = youtubeResolverFixtureJson(video_id, url);
    YoutubeResolverSession session(process);
    if (!session.start(job_id, video_id, 0, 1'999'999'000))
        return {};
    return session.poll(1, 1'999'999'000).audio;
}
} // namespace music_player::m0::test
