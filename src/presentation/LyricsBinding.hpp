#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace music_player::m0 {
class QtLyricsService;
class LyricsVariantsStore;
struct LyricsDocument;
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
    Q_PROPERTY(QVariantList variants READ variants NOTIFY changed)
    Q_PROPERTY(QString selectedVariantId READ selectedVariantId NOTIFY changed)
    Q_PROPERTY(bool variantsBusy READ variantsBusy NOTIFY changed)
    Q_PROPERTY(bool variantsReady READ variantsReady NOTIFY changed)
    Q_PROPERTY(QString variantsStatusText READ variantsStatusText NOTIFY changed)
    Q_PROPERTY(bool variantsCanEdit READ variantsCanEdit NOTIFY changed)
    Q_PROPERTY(bool variantsCanRetrySave READ variantsCanRetrySave NOTIFY changed)
    Q_PROPERTY(bool variantsHasPendingChange READ variantsHasPendingChange NOTIFY changed)
    Q_PROPERTY(bool canSaveCurrentLyrics READ canSaveCurrentLyrics NOTIFY changed)
  public:
    explicit LyricsBinding(QtLyricsService& service, QObject* parent = nullptr);
    LyricsBinding(QtLyricsService& service, std::shared_ptr<LyricsVariantsStore> variants_store,
                  QObject* parent = nullptr);
    ~LyricsBinding() override;
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
    [[nodiscard]] QVariantList variants() const;
    [[nodiscard]] QString selectedVariantId() const;
    [[nodiscard]] bool variantsBusy() const;
    [[nodiscard]] bool variantsReady() const;
    [[nodiscard]] QString variantsStatusText() const;
    [[nodiscard]] bool variantsCanEdit() const;
    [[nodiscard]] bool variantsCanRetrySave() const;
    [[nodiscard]] bool variantsHasPendingChange() const;
    [[nodiscard]] bool canSaveCurrentLyrics() const;
    Q_INVOKABLE bool search(const QString& title, const QString& artist);
    Q_INVOKABLE bool selectVariant(const QString& id);
    Q_INVOKABLE bool addVariant(const QString& name, const QString& language, const QString& script,
                                const QString& source, const QString& kind, const QString& content, bool generated);
    Q_INVOKABLE bool saveCurrentLyrics(const QString& name, const QString& language, const QString& script,
                                       const QString& kind);
    Q_INVOKABLE bool removeVariant(const QString& id);
    Q_INVOKABLE bool retryVariantsSave();
    // After explicit UI consent, drops an unconfirmed candidate and reloads
    // actual disk state. With no current track it only abandons memory, without
    // I/O. Neither action promises a rollback of an ambiguous save.
    Q_INVOKABLE bool reloadVariants();
  signals:
    void changed();

  private:
    struct VariantsState;
    [[nodiscard]] const LyricsDocument& displayedDocument() const;
    std::unique_ptr<VariantsState> variants_;
    QtLyricsService& service_;
    QString path_, title_, artist_;
    qint64 position_ms_{};
    bool shutting_down_{};
    bool exit_preparing_{};
};
} // namespace music_player::m0
