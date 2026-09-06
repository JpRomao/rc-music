#pragma once

#include "m0/ports/DiagnosticLog.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace music_player::m0 {

struct SingleWorkerDiagnosticWriterOptions final {
    // Invoked only on the worker, immediately before append; empty uses UTC now.
    std::function<std::int64_t()> now{};
    // Pending entries, in addition to the single in-flight store operation.
    std::size_t capacity{256};
};

class SingleWorkerDiagnosticWriter final : public DiagnosticSink {
  public:
    explicit SingleWorkerDiagnosticWriter(DiagnosticLogStore& store, SingleWorkerDiagnosticWriterOptions options = {});
    ~SingleWorkerDiagnosticWriter() override;

    SingleWorkerDiagnosticWriter(const SingleWorkerDiagnosticWriter&) = delete;
    SingleWorkerDiagnosticWriter& operator=(const SingleWorkerDiagnosticWriter&) = delete;
    SingleWorkerDiagnosticWriter(SingleWorkerDiagnosticWriter&&) = delete;
    SingleWorkerDiagnosticWriter& operator=(SingleWorkerDiagnosticWriter&&) = delete;

    [[nodiscard]] bool submit(DiagnosticEntry entry) noexcept override;
    // Reject new entries, drain accepted entries and join. Repeated/concurrent
    // callers receive the same outcome. A blocked store can still delay joining.
    // The owner must keep the writer and store alive until all producer and
    // shutdown callers have returned; shutdown does not extend object lifetime.
    [[nodiscard]] bool shutdown() noexcept;

  private:
    void runWorker() noexcept;
    void wakeWorker() noexcept;
    void recordDropped() noexcept;
    void appendEntry(DiagnosticEntry entry) noexcept;

    DiagnosticLogStore& store_;
    SingleWorkerDiagnosticWriterOptions options_;
    std::array<DiagnosticEntry, 256> pending_{};
    std::mutex mutex_;
    std::mutex shutdown_mutex_;
    std::size_t head_{0};
    std::size_t size_{0};
    // One atomic closes ingress and tracks producers already inside submit.
    // The high bit is closed; the remaining bits count in-flight producers.
    std::atomic<std::uint64_t> producer_state_{0};
    std::atomic<bool> work_signalled_{false};
    std::atomic<std::uint64_t> dropped_{0};
    bool writes_succeeded_{true};
    std::thread worker_;
};

} // namespace music_player::m0
