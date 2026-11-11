#pragma once

#include "adapters/DownloadLyricsLookup.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace music_player::m0 {
class LyricsBinding;
// One published acquisition preview. Only trusted C++ supplies publication
// identity; QML can correct lookup metadata and explicitly save online or pasted lyrics.
// Lookup and the shared serialized writer both outlive this projection.
class DownloadLyricsBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasRequest READ hasRequest NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool canSavePasted READ canSavePasted NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(bool canRetrySave READ canRetrySave NOTIFY changed)
    Q_PROPERTY(QString pendingDiscardToken READ pendingDiscardToken NOTIFY changed)
    Q_PROPERTY(bool canDismiss READ canDismiss NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(bool hasLyricsPage READ hasLyricsPage NOTIFY changed)
    Q_PROPERTY(QString requestToken READ requestToken NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
  public:
    DownloadLyricsBinding(DownloadLyricsLookup& lookup, LyricsBinding& writer, QObject* parent = nullptr);
    ~DownloadLyricsBinding() override;
    [[nodiscard]] bool offerPublished(PublishedLyricsRequest request);
    [[nodiscard]] bool hasRequest() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool canSavePasted() const;
    [[nodiscard]] bool canRetry() const;
    [[nodiscard]] bool canRetrySave() const;
    [[nodiscard]] QString pendingDiscardToken() const;
    [[nodiscard]] bool canDismiss() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString artist() const;
    [[nodiscard]] bool hasLyricsPage() const;
    [[nodiscard]] QString requestToken() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QVariantList candidates() const;
    Q_INVOKABLE bool openLyricsPage(const QString& expected_request_token);
    Q_INVOKABLE bool retry(const QString& title, const QString& artist);
    Q_INVOKABLE bool saveCandidate(int index, const QString& name, const QString& language, const QString& script,
                                   const QString& kind);
    Q_INVOKABLE bool savePasted(const QString& expected_request_token, const QString& content, const QString& name,
                                const QString& language, const QString& script, const QString& kind,
                                bool from_lyrics_page = false);
    Q_INVOKABLE bool retrySave();
    Q_INVOKABLE bool discardPendingSave(const QString& expected_request_token, const QString& expected_discard_token);
    Q_INVOKABLE bool dismiss();
    [[nodiscard]] bool prepareExit();
    void resume();
    void beginShutdown();
    // Only lookup resources. The presenter independently drains the writer.
    [[nodiscard]] bool drained() const;
  signals:
    void changed();
    void reviewFinished(const QString& job_id);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
