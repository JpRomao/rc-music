#pragma once

#include "m0/ports/DiagnosticLog.hpp"

#include <QString>

#include <functional>

namespace music_player::m0 {

struct QtWindowsDiagnosticLogStoreOptions final {
    DiagnosticLogLimits limits{};
    std::function<std::int64_t()> clock{};
};

// config_root must already exist. Only its diagnostics child and the three
// versioned diagnostic-N.log slots are managed. I/O belongs on the writer worker.
class QtWindowsDiagnosticLogStore final : public DiagnosticLogStore {
  public:
    explicit QtWindowsDiagnosticLogStore(QString config_root, QtWindowsDiagnosticLogStoreOptions options = {});

    [[nodiscard]] bool append(const DiagnosticRecord& record) noexcept override;

  private:
    QString config_root_;
    QtWindowsDiagnosticLogStoreOptions options_;
};

} // namespace music_player::m0
