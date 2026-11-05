#pragma once

#include "m0/ports/PlaylistStore.hpp"

#include <QString>
#include <QtTypes>

#include <functional>

namespace music_player::m0 {
enum class PlaylistStoreFaultPoint { BeforeBackupCommit, AfterBackupCommit, BeforePrimaryCommit, AfterPrimaryCommit };
struct QtJsonPlaylistStoreOptions final {
    std::function<bool(PlaylistStoreFaultPoint)> inject_fault{};
};

// A bounded v1 document in playlists.json, with one known-good .bak and a
// cooperative .lock. No audio path is opened, rewritten, renamed or removed.
// Construction is passive and owns the absolute configuration directory.
class QtJsonPlaylistStore final : public PlaylistStore {
  public:
    static constexpr qsizetype maximum_file_size = 16 * 1024 * 1024;
    explicit QtJsonPlaylistStore(QString config_directory, QtJsonPlaylistStoreOptions options = {});
    [[nodiscard]] PlaylistLoadResult load() override;
    [[nodiscard]] PlaylistSaveStatus save(const PlaylistLibrarySnapshot& snapshot) override;

  private:
    QString config_directory_;
    QtJsonPlaylistStoreOptions options_;
};
} // namespace music_player::m0
