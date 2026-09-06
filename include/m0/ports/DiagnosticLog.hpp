#pragma once

#include "m0/domain/DiagnosticRecord.hpp"

namespace music_player::m0 {

class DiagnosticLogStore {
  public:
    virtual ~DiagnosticLogStore() = default;

    // Called by a worker. Failures are nonfatal and are reported as false.
    [[nodiscard]] virtual bool append(const DiagnosticRecord& record) noexcept = 0;
};

class DiagnosticSink {
  public:
    virtual ~DiagnosticSink() = default;

    // Best effort: never wait for filesystem I/O or another producer.
    [[nodiscard]] virtual bool submit(DiagnosticEntry entry) noexcept = 0;
};

} // namespace music_player::m0
