#pragma once

#include "m0/ports/SessionStore.hpp"

#include <QString>
#include <QtTypes>

#include <functional>

namespace music_player::m0 {

enum class SessionStoreFaultPoint {
    BeforeBackupCommit,
    AfterBackupCommit,
    BeforePrimaryCommit,
    AfterPrimaryCommit,
};

struct QtJsonWindowsSessionStoreOptions final {
    std::function<bool(SessionStoreFaultPoint)> inject_fault{};
};

class QtJsonWindowsSessionStore final : public SessionStore {
  public:
    static constexpr qsizetype maximum_file_size = 16 * 1024 * 1024;

    explicit QtJsonWindowsSessionStore(QString config_directory, QtJsonWindowsSessionStoreOptions options = {});

    [[nodiscard]] SessionLoadResult load() override;
    [[nodiscard]] SessionSaveStatus save(const SessionSaveRequest& request) override;

  private:
    QString config_directory_;
    QtJsonWindowsSessionStoreOptions options_;
};

} // namespace music_player::m0
