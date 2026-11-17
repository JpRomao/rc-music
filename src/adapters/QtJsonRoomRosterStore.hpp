#pragma once

#include "m0/ports/RoomRosterStore.hpp"

#include <QString>
#include <QtTypes>

namespace music_player::m0 {
// A bounded v1 document in room-roles.json, with one known-good .bak and a cooperative .lock.
// It holds the invite secret, so the adapter never logs file contents.
class QtJsonRoomRosterStore final : public RoomRosterStore {
  public:
    static constexpr qsizetype maximum_file_size = 256 * 1024;
    explicit QtJsonRoomRosterStore(QString config_directory);
    [[nodiscard]] RoomRosterLoadResult load() override;
    [[nodiscard]] RoomRosterSaveStatus save(const RoomRosterSnapshot& snapshot) override;

  private:
    QString config_directory_;
};
} // namespace music_player::m0
