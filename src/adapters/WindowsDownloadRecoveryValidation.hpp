#pragma once

#include "adapters/WindowsDownloadWorkspace.hpp"
#include "adapters/WindowsLocalMp3Validator.hpp"
#include "m0/ports/DownloadRecoveryValidation.hpp"

#include <functional>
#include <memory>

namespace music_player::m0 {
// Trusted slow task, constructed/used/destroyed in the private Qt worker loop.
// Construction/factory is inert: null/exception retains no escaped work. Once
// start is called, false/exception is uncertain and needs cancel+poll to drain.
// nullopt is pending; a known outcome acknowledges stopped native work.
// Validated additionally retains the capability until task destruction. Released
// from a task only acknowledges cancellation drain; the fast port emits its own
// Released only AFTER destroying the task. Exceptions/unknown enum never prove
// drain. cancel may be repeated after an uncertain call. No work may escape.
class WindowsDownloadRecoveryValidationTask {
  public:
    virtual ~WindowsDownloadRecoveryValidationTask() = default;
    [[nodiscard]] virtual bool start(const std::string& job_id, qint64 now_ms) = 0;
    virtual void cancel() = 0;
    [[nodiscard]] virtual std::optional<DownloadRecoveryValidationOutcome> poll(qint64 now_ms) = 0;
};

class WindowsDownloadRecoveryValidation final : public DownloadRecoveryValidation {
  public:
    using TaskFactory = std::function<std::unique_ptr<WindowsDownloadRecoveryValidationTask>()>;
    explicit WindowsDownloadRecoveryValidation(TaskFactory factory);
    WindowsDownloadRecoveryValidation(QString journal_directory, WindowsDownloadWorkspaceConfig workspace,
                                      WindowsLocalMp3ValidatorConfig tools);
    ~WindowsDownloadRecoveryValidation() override;
    WindowsDownloadRecoveryValidation(const WindowsDownloadRecoveryValidation&) = delete;
    WindowsDownloadRecoveryValidation& operator=(const WindowsDownloadRecoveryValidation&) = delete;
    [[nodiscard]] DownloadRecoveryValidationCommand startValidation(std::uint64_t operation_id,
                                                                    std::string_view job_id) override;
    [[nodiscard]] bool release(std::uint64_t operation_id) override;
    [[nodiscard]] std::optional<DownloadRecoveryValidationResult> poll(std::uint64_t operation_id) override;
    void shutdown() override;
    [[nodiscard]] bool stopped() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
