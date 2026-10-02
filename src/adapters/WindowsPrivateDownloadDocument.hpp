#pragma once

#include "m0/ports/DownloadCheckpointStore.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <functional>

namespace music_player::m0 {

// Private native primitive for precisely these two fixed documents, not a
// general filesystem API. All calls and callbacks belong to one worker.
enum class WindowsPrivateDownloadDocumentKind { Checkpoint, Ownership };
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

  private:
    QString directory_;
    WindowsPrivateDownloadDocumentKind kind_;
    Validator validator_;
    WindowsPrivateDownloadDocumentOptions options_;
};

} // namespace music_player::m0
