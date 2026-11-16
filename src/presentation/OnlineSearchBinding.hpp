#pragma once

#include "m0/ports/AsyncYoutubeAudioSubmission.hpp"
#include "m0/ports/OnlineSearch.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace music_player::m0 {
// ADR-0019 online search page. Results are metadata only; choosing one sends an
// ordinary YouTube audio request by video ID through the download admission.
// Only IDs from the current result list can be requested. UI thread only.
class OnlineSearchBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool requesting READ requesting NOTIFY changed)
    Q_PROPERTY(bool searched READ searched NOTIFY changed)
    Q_PROPERTY(QString query READ query NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)

  public:
    explicit OnlineSearchBinding(QObject* parent = nullptr);
    ~OnlineSearchBinding() override;
    // Owns the search; borrows the submission, which must outlive this binding
    // or be revoked with a null backend. Null ports make the page unavailable.
    void setBackend(std::unique_ptr<OnlineSearch> search, AsyncYoutubeAudioSubmission* submission);

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool requesting() const;
    [[nodiscard]] bool searched() const;
    [[nodiscard]] QString query() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QVariantList results() const;

    Q_INVOKABLE bool search(const QString& text);
    Q_INVOKABLE void cancel();
    // Requests the audio of one listed result. Each result can be sent once.
    Q_INVOKABLE bool download(const QString& video_id);
    // Opens a listed video on YouTube in the default browser, so the user can
    // check it before downloading. Nothing is downloaded.
    Q_INVOKABLE bool openPreview(const QString& video_id);

  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void pollBackend();
    void finishAdmission(const DownloadSubmissionResult& result);
};
} // namespace music_player::m0
