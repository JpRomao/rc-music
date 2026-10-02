#pragma once

#include "m0/application/DownloadCheckpoint.hpp"

#include <QString>
#include <QtTypes>

#include <functional>
#include <optional>

namespace music_player::m0 {

enum class WindowsDownloadCheckpointLoadStatus { Missing, Loaded, Invalid, Unreadable, FutureVersion };
enum class WindowsDownloadCheckpointSaveStatus { Saved, Failed, BlockedByExistingData };
enum class WindowsDownloadCheckpointFaultPoint { BeforeCommit, AfterCommit };

struct WindowsDownloadCheckpointLoadResult final {
    std::optional<DownloadCheckpoint> checkpoint{};
    WindowsDownloadCheckpointLoadStatus status{WindowsDownloadCheckpointLoadStatus::Missing};
};

struct WindowsDownloadCheckpointStoreOptions final {
    std::function<bool(WindowsDownloadCheckpointFaultPoint)> inject_fault{};
};

// Synchronous worker-only adapter. The existing directory must be app-owned,
// private and supplied by trusted composition, never a page or journal field.
// A protected DACL with exactly current-user + SYSTEM full-access ACEs is required.
// No directory is created or repaired. Pins, a native writer lock and protected
// files bound this local transaction; they do not defeat a compromised host or
// a malicious same-user process. The leaf permits native write/delete sharing;
// revalidation is not protection against a concurrent malicious reparse race.
// Failed after commit is deliberately ambiguous.
// The document is recovery evidence only, never execute/import/delete authority.
class WindowsDownloadCheckpointStore final {
  public:
    static constexpr qsizetype maximum_file_size = 2 * 1024 * 1024;

    explicit WindowsDownloadCheckpointStore(QString config_directory,
                                            WindowsDownloadCheckpointStoreOptions options = {});
    [[nodiscard]] WindowsDownloadCheckpointLoadResult load();
    [[nodiscard]] WindowsDownloadCheckpointSaveStatus save(const DownloadCheckpoint& checkpoint);

  private:
    QString config_directory_;
    WindowsDownloadCheckpointStoreOptions options_;
};

} // namespace music_player::m0
