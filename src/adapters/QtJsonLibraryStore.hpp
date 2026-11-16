#pragma once

#include "m0/ports/LibraryStore.hpp"

#include <QString>
#include <QtTypes>

#include <functional>

namespace music_player::m0 {
enum class LibraryStoreFaultPoint { BeforeBackupCommit, AfterBackupCommit, BeforePrimaryCommit, AfterPrimaryCommit };
struct QtJsonLibraryStoreOptions final {
    std::function<bool(LibraryStoreFaultPoint)> inject_fault{};
};

// ADR-0019: a bounded v1 document in library.json, with one known-good .bak and a
// cooperative .lock, following the playlist store. No audio path is opened,
// rewritten, renamed or removed. Construction is passive.
class QtJsonLibraryStore final : public LibraryStore {
  public:
    static constexpr qsizetype maximum_file_size = 48 * 1024 * 1024;
    explicit QtJsonLibraryStore(QString config_directory, QtJsonLibraryStoreOptions options = {});
    [[nodiscard]] LibraryLoadResult load() override;
    [[nodiscard]] LibrarySaveStatus save(const LibrarySnapshot& snapshot) override;

  private:
    QString config_directory_;
    QtJsonLibraryStoreOptions options_;
};
} // namespace music_player::m0
