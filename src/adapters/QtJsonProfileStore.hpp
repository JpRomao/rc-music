#pragma once

#include "m0/ports/ProfileStore.hpp"

#include <QString>
#include <QtTypes>

namespace music_player::m0 {
// A bounded v1 document in profile.json, with one known-good .bak and a
// cooperative .lock. Construction is passive and owns the absolute configuration directory.
class QtJsonProfileStore final : public ProfileStore {
  public:
    static constexpr qsizetype maximum_file_size = 64 * 1024;
    explicit QtJsonProfileStore(QString config_directory);
    [[nodiscard]] ProfileLoadResult load() override;
    [[nodiscard]] ProfileSaveStatus save(const ListenerProfile& profile) override;

  private:
    QString config_directory_;
};
} // namespace music_player::m0
