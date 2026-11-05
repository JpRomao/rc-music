#pragma once

#include <QObject>
#include <QString>

namespace music_player::m0 {
class QtLyricsService;
// Application-thread projection. Playback remains authoritative for position;
// lyrics do not load/play audio or change the queue and expose no file paths.
class LyricsBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString currentLine READ currentLine NOTIFY changed)
    Q_PROPERTY(QString previousLine READ previousLine NOTIFY changed)
    Q_PROPERTY(QString nextLine READ nextLine NOTIFY changed)
    Q_PROPERTY(QString plainText READ plainText NOTIFY changed)
    Q_PROPERTY(bool hasSyncedLyrics READ hasSyncedLyrics NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canSearch READ canSearch NOTIFY changed)
  public:
    explicit LyricsBinding(QtLyricsService& service, QObject* parent = nullptr);
    void setTrack(const QString& path, const QString& title, const QString& artist, const QString& album,
                  qint64 duration_ms);
    void setPosition(qint64 confirmed_position_ms);
    void beginShutdown();
    [[nodiscard]] bool prepareExit();
    [[nodiscard]] bool stopped() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString artist() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString currentLine() const;
    [[nodiscard]] QString previousLine() const;
    [[nodiscard]] QString nextLine() const;
    [[nodiscard]] QString plainText() const;
    [[nodiscard]] bool hasSyncedLyrics() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canSearch() const;
    Q_INVOKABLE bool search(const QString& title, const QString& artist);
  signals:
    void changed();

  private:
    QtLyricsService& service_;
    QString path_, title_, artist_;
    qint64 position_ms_{};
    bool shutting_down_{};
    bool exit_preparing_{};
};
} // namespace music_player::m0
