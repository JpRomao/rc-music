#pragma once

#include "m0/domain/Lyrics.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {
class QtLyricsService;

// Trusted C++ composition supplies the already-published exact target. Neither
// a browser message nor QML may turn a path into publication authority.
struct PublishedLyricsRequest final {
    std::string job_id;
    QString final_mp3, title, artist, lyrics_page, album;
    qint64 duration_ms{};
};

// Lookup only: no playback, admission, audio transfer, file writer or Saved ACK.
// The borrowed service is exclusive to this coordinator and outlives it. Keep
// the same service across requests so its provider cooldown cannot be bypassed.
class DownloadLyricsLookup final : public QObject {
    Q_OBJECT
  public:
    enum class State { Idle, NeedsMetadata, Searching, Ready, NotFound, Instrumental, Error, Cancelled };
    struct Result final {
        quint64 generation{};
        std::string job_id;
        QString final_mp3;
        std::vector<LyricsDocument> candidates;
        QString source{QStringLiteral("LRCLIB")};
    };
    explicit DownloadLyricsLookup(QtLyricsService& exclusive_service, QObject* parent = nullptr);
    ~DownloadLyricsLookup() override;
    [[nodiscard]] static bool validPublication(const PublishedLyricsRequest& request);
    // True admits a valid target, including NeedsMetadata/cooldown. Reentrant
    // revocation or closed/invalid ingress returns false. No implicit retry.
    [[nodiscard]] bool request(const PublishedLyricsRequest& request);
    [[nodiscard]] bool retry(QString title, QString artist);
    void clear();
    [[nodiscard]] bool prepareExit();
    void resume();
    void beginShutdown();
    [[nodiscard]] bool drained() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] const std::optional<Result>& result() const;

  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
