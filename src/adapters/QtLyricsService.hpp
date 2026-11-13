#pragma once

#include "m0/domain/Lyrics.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QNetworkAccessManager;

namespace music_player::m0 {
struct LyricFindOptions final {
    QByteArray display_key, lrc_key;
    QString territory;
    bool preferred{};
};

// Local sibling lyrics are read off the UI thread. Online requests are made
// only by searchOnline and contain metadata, never audio or a local path.
class QtLyricsService final : public QObject {
    Q_OBJECT
  public:
    enum class Status { Idle, LoadingLocal, Searching, Ready, NotFound, Instrumental, Error };
    enum class Source { None, Local, Online };
    enum class OnlineProvider { LRCLIB, LyricFind };
    static constexpr qint64 maximum_local_bytes = 256 * 1024;
    static constexpr qint64 maximum_response_bytes = 1024 * 1024;
    explicit QtLyricsService(
        QNetworkAccessManager* borrowed_network = nullptr, QObject* parent = nullptr,
        QByteArray client_identifier = QByteArray("RCMusic/0.1 (+https://github.com/JpRomao/rc-music)"),
        int request_timeout_ms = 10'000);
    ~QtLyricsService() override;
    static LyricFindOptions loadLyricFindOptions(const QString& private_config_root);
    void configureLyricFind(LyricFindOptions options);
    [[nodiscard]] bool lyricFindConfigured() const noexcept;
    [[nodiscard]] OnlineProvider onlineProvider() const noexcept;
    [[nodiscard]] QString onlineAttribution() const;
    // Same-path metadata updates refresh search defaults. A different queue
    // occurrence cancels an in-flight search and revokes licensed lyrics.
    void setTrack(QString path, QString title, QString artist, QString album, qint64 duration_ms,
                  const QString& occurrence_id = {});
    // Metadata-only context for a separately owned acquisition lookup. Revokes
    // earlier results without reading an audio file or its sibling .lrc.
    void setLookupTrack(QString path, QString title, QString artist, QString album, qint64 duration_ms);
    [[nodiscard]] bool searchOnline(QString title_override = {}, QString artist_override = {});
    void cancel();
    // Irreversible exit preparation. Does not join file I/O on the UI thread.
    void beginShutdown();
    [[nodiscard]] bool stopped() const noexcept;
    // Resource observation only. The caller must separately close admission.
    [[nodiscard]] bool drained() const noexcept;
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] Source source() const noexcept;
    [[nodiscard]] const LyricsDocument& document() const noexcept;
    // All distinct online contents for this search, synchronized first. Plain
    // text stays plain; no language, alphabet or synchronization is inferred.
    [[nodiscard]] const std::vector<LyricsDocument>& onlineDocuments() const noexcept;
    [[nodiscard]] quint64 contextRevision() const noexcept;
    [[nodiscard]] QString message() const;
    [[nodiscard]] bool canSearch() const noexcept;
    [[nodiscard]] bool busy() const noexcept;

  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
