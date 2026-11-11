#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace music_player::m0 {
class AudioFileLocator;
class AudioFilesPicker;
class PlaylistStore;
class PlaylistsBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool interactionAllowed READ interactionAllowed NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString selectedPlaylistId READ selectedPlaylistId NOTIFY changed)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool canAddFiles READ canAddFiles NOTIFY changed)
    Q_PROPERTY(bool canReplaceFile READ canReplaceFile NOTIFY changed)
    Q_PROPERTY(bool canRetrySave READ canRetrySave NOTIFY changed)
    Q_PROPERTY(bool hasPendingChange READ hasPendingChange NOTIFY changed)
  public:
    using Paths = std::vector<std::filesystem::path>;
    using QueueReader = std::function<Paths()>;
    using QueueAppender = std::function<bool(Paths)>;
    // Passive construction. The caller retains the dedicated picker and locator.
    // Each file-I/O job retains the shared store.
    PlaylistsBinding(std::shared_ptr<PlaylistStore> store, QueueReader read_queue, QueueAppender append_queue,
                     QObject* parent = nullptr, AudioFilesPicker* picker = nullptr,
                     AudioFileLocator* locator = nullptr);
    ~PlaylistsBinding() override;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool interactionAllowed() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString selectedPlaylistId() const;
    [[nodiscard]] QVariantList playlists() const;
    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] bool canAddFiles() const;
    [[nodiscard]] bool canReplaceFile() const;
    [[nodiscard]] bool canRetrySave() const;
    [[nodiscard]] bool hasPendingChange() const;
    Q_INVOKABLE bool selectPlaylist(const QString& id);
    Q_INVOKABLE bool createPlaylist(const QString& name);
    Q_INVOKABLE bool renamePlaylist(const QString& id, const QString& name);
    Q_INVOKABLE bool deletePlaylist(const QString& id);
    Q_INVOKABLE bool saveQueueAsPlaylist(const QString& name);
    Q_INVOKABLE bool addQueueToPlaylist(const QString& id);
    Q_INVOKABLE bool requestAddFiles(const QString& id);
    Q_INVOKABLE bool requestReplaceFile(const QString& playlist_id, const QString& entry_id);
    Q_INVOKABLE bool removeEntry(const QString& playlist_id, const QString& entry_id);
    Q_INVOKABLE bool moveEntry(const QString& playlist_id, const QString& entry_id, int destination);
    Q_INVOKABLE bool enqueuePlaylist(const QString& id);
    Q_INVOKABLE bool retrySave();
    // Reloads the document after an uncertain write; never claims a rollback.
    Q_INVOKABLE void discardPending();
    Q_INVOKABLE bool discardUnsavedChange();
    Q_INVOKABLE bool retryLoad();
    [[nodiscard]] bool prepareExit();
    void resume();
    void beginShutdown();
  signals:
    void unsavedChangeDiscarded();
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
