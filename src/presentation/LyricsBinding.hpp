#pragma once

#include "m0/domain/LyricsVariants.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>
#include <memory>

namespace music_player::m0 {
enum class AcquiredLyricsSaveState {
    Idle,
    Loading,
    Saving,
    Saved,
    Unchanged,
    Conflict,
    LoadFailed,
    SaveFailed,
    BlockedByFutureVersion,
    Invalid,
    Abandoned
};
struct AcquiredLyricsSaveSnapshot final {
    std::string ticket{};
    QString target{};
    AcquiredLyricsSaveState state{AcquiredLyricsSaveState::Idle};
};
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
    Q_PROPERTY(bool queueVariantAvailable READ queueVariantAvailable NOTIFY changed)
    Q_PROPERTY(QString queuedVariantStatusText READ queuedVariantStatusText NOTIFY changed)
    Q_PROPERTY(bool canSaveCurrentLyrics READ canSaveCurrentLyrics NOTIFY changed)
    Q_PROPERTY(QVariantList onlineLyricsCandidates READ onlineLyricsCandidates NOTIFY onlineLyricsCandidatesChanged)
    Q_PROPERTY(int selectedOnlineLyricsCandidate READ selectedOnlineLyricsCandidate NOTIFY changed)
    Q_PROPERTY(QString currentLyricsRevision READ currentLyricsRevision NOTIFY changed)
    Q_PROPERTY(bool canGenerateRomaji READ canGenerateRomaji NOTIFY changed)
    Q_PROPERTY(QString romajiSourceRevision READ romajiSourceRevision NOTIFY changed)
    Q_PROPERTY(QString romajiStatusText READ romajiStatusText NOTIFY changed)
    Q_PROPERTY(bool lyricsCopyProtected READ lyricsCopyProtected NOTIFY changed)
    Q_PROPERTY(QString lyricsAttribution READ lyricsAttribution NOTIFY changed)
    Q_PROPERTY(bool lyricFindConfigured READ lyricFindConfigured CONSTANT)
  public:
    explicit LyricsBinding(QtLyricsService& service, QObject* parent = nullptr);
    LyricsBinding(QtLyricsService& service, std::shared_ptr<LyricsVariantsStore> variants_store,
                  QObject* parent = nullptr);
    ~LyricsBinding() override;
    void setTrack(const QString& path, const QString& title, const QString& artist, const QString& album,
                  qint64 duration_ms, const QString& occurrence_id = {});
    void setPosition(qint64 confirmed_position_ms);
    // Resolves queue occurrence IDs in trusted C++. QML receives only a form token.
    void setQueueLyricsSource(std::function<QString(const QString&)> source);
    using JapaneseRomanizer = std::function<std::optional<std::vector<std::string>>(const std::vector<std::string>&)>;
    void setJapaneseRomanizer(JapaneseRomanizer romanizer);
    // Trusted publication/consent owner only, never a QML/browser path. Uses
    // the same serialized writer as manual edits and never changes playback.
    [[nodiscard]] bool appendAcquiredLyrics(std::string ticket, QString target, LyricsVariants drafts,
                                            bool keep_distinct_versions = false);
    [[nodiscard]] AcquiredLyricsSaveSnapshot acquiredLyricsSave() const;
    [[nodiscard]] bool discardInvalidAcquiredLyrics(const std::string& ticket, const QString& target);
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
    [[nodiscard]] bool queueVariantAvailable() const;
    [[nodiscard]] QString queuedVariantStatusText() const;
    [[nodiscard]] bool canSaveCurrentLyrics() const;
    [[nodiscard]] QVariantList onlineLyricsCandidates() const;
    [[nodiscard]] int selectedOnlineLyricsCandidate() const;
    [[nodiscard]] QString currentLyricsRevision() const;
    [[nodiscard]] bool canGenerateRomaji() const;
    [[nodiscard]] QString romajiSourceRevision() const;
    [[nodiscard]] QString romajiStatusText() const;
    [[nodiscard]] bool lyricsCopyProtected() const;
    [[nodiscard]] QString lyricsAttribution() const;
    [[nodiscard]] bool lyricFindConfigured() const;
    Q_INVOKABLE bool search(const QString& title, const QString& artist);
    Q_INVOKABLE bool selectVariant(const QString& id);
    Q_INVOKABLE bool selectOnlineLyricsCandidate(int index);
    Q_INVOKABLE bool addVariant(const QString& name, const QString& language, const QString& script,
                                const QString& source, const QString& kind, const QString& content, bool generated);
    Q_INVOKABLE QString prepareQueuedVariant(const QString& occurrence_id);
    Q_INVOKABLE bool addQueuedVariant(const QString& token, const QString& name, const QString& language,
                                      const QString& script, const QString& source, const QString& kind,
                                      const QString& content, bool generated);
    Q_INVOKABLE void cancelQueuedVariant(const QString& token);
    Q_INVOKABLE bool saveCurrentLyrics(const QString& name, const QString& language, const QString& script,
                                       const QString& kind, const QString& expected_revision);
    Q_INVOKABLE QString generateRomaji();
    Q_INVOKABLE bool saveRomaji(const QString& token, const QString& name, const QString& content);
    Q_INVOKABLE void discardRomaji(const QString& token);
    Q_INVOKABLE bool removeVariant(const QString& id);
    Q_INVOKABLE bool retryVariantsSave();
    // After explicit UI consent, drops an unconfirmed candidate and reloads
    // actual disk state. With no current track it only abandons memory, without
    // I/O. Neither action promises a rollback of an ambiguous save.
    Q_INVOKABLE bool reloadVariants();
  signals:
    void changed();
    void onlineLyricsCandidatesChanged();
    void romajiDraftReady(const QString& token, const QString& content);

  private:
    struct VariantsState;
    struct RomajiState;
    [[nodiscard]] const LyricsDocument& displayedDocument() const;
    void updateOnlineLyricsCandidates();
    std::unique_ptr<VariantsState> variants_;
    std::unique_ptr<RomajiState> romaji_;
    QtLyricsService& service_;
    QString path_, title_, artist_, occurrence_;
    QVariantList online_candidates_;
    qint64 position_ms_{};
    quint64 online_candidate_context_{};
    quint64 selection_revision_{};
    int online_candidate_{};
    bool shutting_down_{};
    bool exit_preparing_{};
};
} // namespace music_player::m0
