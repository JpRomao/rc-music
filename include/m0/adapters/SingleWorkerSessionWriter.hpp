#pragma once

#include "m0/ports/SessionStore.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace music_player::m0 {

class SingleWorkerSessionWriter final : public SessionWriter {
  public:
    explicit SingleWorkerSessionWriter(SessionStore& store);
    ~SingleWorkerSessionWriter() override;

    SingleWorkerSessionWriter(const SingleWorkerSessionWriter&) = delete;
    SingleWorkerSessionWriter& operator=(const SingleWorkerSessionWriter&) = delete;
    SingleWorkerSessionWriter(SingleWorkerSessionWriter&&) = delete;
    SingleWorkerSessionWriter& operator=(SingleWorkerSessionWriter&&) = delete;

    [[nodiscard]] SessionWriterCommand start(SessionWriteRequest request) override;
    [[nodiscard]] std::optional<SessionWriteCompletion> poll() override;
    [[nodiscard]] std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override;
    [[nodiscard]] bool shutdown() override;

  private:
    void runWorker();
    [[nodiscard]] SessionWriteCompletion saveAndReconcile(const SessionWriteRequest& request) noexcept;

    SessionStore& store_;
    std::mutex mutex_;
    std::mutex shutdown_mutex_;
    std::condition_variable condition_;
    std::optional<SessionWriteRequest> pending_;
    std::optional<SessionRevision> active_revision_;
    std::optional<SessionWriteCompletion> completion_;
    std::optional<SessionWriteCompletion> latest_completed_;
    bool shutdown_requested_{false};
    bool stopped_{false};
    std::thread worker_;
};

} // namespace music_player::m0
