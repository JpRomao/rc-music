#pragma once

#include "m0/domain/LocalLibrary.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace music_player::m0 {
class LibraryArtworkIndex;
class AudioFileLocator;
class LibraryStore;
class MetadataExtractor;

// ADR-0019: owns the permanent "Minha música" catalog for the UI thread. Store
// I/O runs on a worker. Admissions arrive at any time; the confirmed catalog
// only advances when the store reports Saved, and later admissions are saved
// in a follow-up write. A failed save keeps the change pending without
// blocking playback or exit.
class LibraryBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool hasPendingChange READ hasPendingChange NOTIFY changed)
    Q_PROPERTY(bool canRetrySave READ canRetrySave NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QObject* model READ model CONSTANT)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY changed)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY changed)
    Q_PROPERTY(bool canLocate READ canLocate NOTIFY changed)
    // Download jobs whose audio entered the confirmed library (for Activity).
    Q_PROPERTY(QStringList downloadJobs READ downloadJobs NOTIFY changed)
    // Cover image URL prefix (append the record ID); empty without a cover reader.
    Q_PROPERTY(QString artworkBase READ artworkBase NOTIFY changed)
  public:
    using Paths = std::vector<std::filesystem::path>;
    // Appends to the queue without playback (the playlist path of the presenter).
    using QueueAppender = std::function<bool(Paths)>;
    // Appends to a saved playlist by its ID (the playlists binding).
    using PlaylistAppender = std::function<bool(const QString&, Paths)>;
    // Passive construction; start() begins loading.
    explicit LibraryBinding(std::shared_ptr<LibraryStore> store, QueueAppender append_queue = {},
                            QObject* parent = nullptr);
    ~LibraryBinding() override;
    [[nodiscard]] bool start();
    // Optional: reads tags and availability in the background, a bounded batch at a
    // time, for records never read or currently unavailable. Each record is tried
    // at most once per session. Set before start().
    void setMetadataExtractor(std::shared_ptr<const MetadataExtractor> extractor);
    // Optional, borrowed for the binding's lifetime: the dialog used to locate a moved file.
    void setFileLocator(AudioFileLocator* locator);
    void setPlaylistAppender(PlaylistAppender appender);
    // Explicit local ingress (open, add, drop, forwarding, playlist). Idempotent by exact path.
    void admit(Paths paths);
    // First-use seed from existing queue and playlist references. Ignored once
    // library.json exists, so a removed record never comes back this way.
    void seed(Paths paths);
    // A published download, admitted once per job (a replay never resurrects a removal).
    void admitDownload(std::filesystem::path path, std::string job_id);
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool busy() const;
    // Confirmed (Saved) records only.
    [[nodiscard]] int count() const;
    [[nodiscard]] bool hasPendingChange() const;
    [[nodiscard]] bool canRetrySave() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] LibrarySnapshot confirmedSnapshot() const;
    // List view projection of the confirmed catalog (a LibraryListModel).
    [[nodiscard]] QObject* model() const;
    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString& text);
    [[nodiscard]] int visibleCount() const;
    // Adds the record's file to the end of the queue without playback.
    Q_INVOKABLE bool enqueue(const QString& record_id);
    // Removes only the catalog record; the audio file, queue, playlists and
    // playback are untouched.
    Q_INVOKABLE bool remove(const QString& record_id);
    // Adds the record's file to the end of a saved playlist; the library is unchanged.
    Q_INVOKABLE bool addToPlaylist(const QString& record_id, const QString& playlist_id);
    [[nodiscard]] bool canLocate() const;
    [[nodiscard]] QStringList downloadJobs() const;
    [[nodiscard]] QString artworkBase() const;
    // UI thread, before start. The index follows every confirmed change.
    void setArtworkIndex(std::shared_ptr<LibraryArtworkIndex> index, QString source_base);
    // Asks the user for the record's new location. The ID, tags and playlists stay
    // as they are; a path that belongs to another record is reported as a conflict.
    Q_INVOKABLE bool locate(const QString& record_id);
    // Revokes a pending dialog (page closed); a late answer is ignored.
    Q_INVOKABLE void cancelLocate();
    // True while a write is running or queued; a failed save does not hold exit.
    [[nodiscard]] bool savingForExit() const;
    Q_INVOKABLE bool retrySave();
    Q_INVOKABLE bool retryLoad();
    void beginShutdown();
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
