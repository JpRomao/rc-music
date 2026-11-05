#pragma once

#include "m0/ports/DownloadCheckpointStore.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <functional>
#include <memory>

namespace music_player::m0 {

class DownloadMediaProfileLease;

struct WindowsDownloadStoragePaths final {
    QString root{}, state{}, network{}, media{}, youtube{}, staging{}, published{};
};

// Pure fixed layout. Validation and native I/O belong only to preparation.
[[nodiscard]] WindowsDownloadStoragePaths windowsDownloadStoragePaths(const QString& config_root);
// Trusted worker-only opt-in: an existing local parent and seven exact private
// directories. Existing objects are validated, never repaired or deleted.
// Failures preserve every previously existing or newly created object.
[[nodiscard]] bool prepareWindowsDownloadStorage(const QString& config_root);

// Private native primitive for precisely these three fixed documents, not a
// general filesystem API. All calls and callbacks belong to one worker.
enum class WindowsPrivateDownloadDocumentKind { Checkpoint, Ownership, MediaProfile };
enum class WindowsPrivateDownloadFaultPoint { BeforeCommit, AfterCommit };

struct WindowsPrivateDownloadDocumentOptions final {
    std::function<bool(WindowsPrivateDownloadFaultPoint)> inject_fault{};
};

struct WindowsPrivateDownloadDocumentResult final {
    DownloadCheckpointLoadStatus status{DownloadCheckpointLoadStatus::Unreadable};
    QByteArray bytes{};
};

struct WindowsPrivateDownloadDocumentMutation final {
    DownloadCheckpointSaveStatus status{DownloadCheckpointSaveStatus::Failed};
    QByteArray bytes{};
};

class WindowsPrivateDownloadDocument final {
  public:
    using Validator = std::function<DownloadCheckpointLoadStatus(QByteArrayView)>;
    using Mutation = std::function<WindowsPrivateDownloadDocumentMutation(const WindowsPrivateDownloadDocumentResult&)>;

    WindowsPrivateDownloadDocument(QString directory, WindowsPrivateDownloadDocumentKind kind, Validator validator,
                                   WindowsPrivateDownloadDocumentOptions options = {});
    [[nodiscard]] WindowsPrivateDownloadDocumentResult load();
    // The current document is inspected and the mutation runs under the same
    // exclusive native lock as replacement. Only Saved with valid bytes writes.
    [[nodiscard]] DownloadCheckpointSaveStatus update(const Mutation& mutation);
    // The media-profile operation has one live owner, independently of the
    // short document writer lock. Releasing this lease never deletes a file.
    [[nodiscard]] static std::unique_ptr<DownloadMediaProfileLease> acquireMediaProfileLease(QString directory);

  private:
    QString directory_;
    WindowsPrivateDownloadDocumentKind kind_;
    Validator validator_;
    WindowsPrivateDownloadDocumentOptions options_;
};

} // namespace music_player::m0
