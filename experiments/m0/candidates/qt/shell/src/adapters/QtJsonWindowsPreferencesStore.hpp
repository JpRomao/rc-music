#pragma once

#include "m0/ports/PreferencesStore.hpp"

#include <QString>

#include <functional>

namespace music_player::m0 {

enum class PreferencesStoreFaultPoint {
    BeforePrimaryCommit,
    AfterPrimaryCommit,
};

struct QtJsonWindowsPreferencesStoreOptions final {
    std::function<bool(PreferencesStoreFaultPoint)> inject_fault{};
};

class QtJsonWindowsPreferencesStore final : public PreferencesStore {
  public:
    static constexpr qsizetype maximum_file_size = 64 * 1024;

    explicit QtJsonWindowsPreferencesStore(QString config_directory, QtJsonWindowsPreferencesStoreOptions options = {});

    [[nodiscard]] PreferencesLoadResult load() override;
    [[nodiscard]] PreferencesSaveStatus save(const PreferencesSaveRequest& request) override;

  private:
    QString config_directory_;
    QtJsonWindowsPreferencesStoreOptions options_;
};

} // namespace music_player::m0
