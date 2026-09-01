#pragma once

#include "m0/ports/PreferencesStore.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace music_player::m0 {

class SingleWorkerPreferencesWriter final : public PreferencesWriter {
  public:
    explicit SingleWorkerPreferencesWriter(PreferencesStore& store);
    ~SingleWorkerPreferencesWriter() override;

    SingleWorkerPreferencesWriter(const SingleWorkerPreferencesWriter&) = delete;
    SingleWorkerPreferencesWriter& operator=(const SingleWorkerPreferencesWriter&) = delete;
    SingleWorkerPreferencesWriter(SingleWorkerPreferencesWriter&&) = delete;
    SingleWorkerPreferencesWriter& operator=(SingleWorkerPreferencesWriter&&) = delete;

    [[nodiscard]] PreferencesWriterCommand start(PreferencesWriteRequest request) override;
    [[nodiscard]] std::optional<PreferencesWriteCompletion> poll() override;
    [[nodiscard]] std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override;
    [[nodiscard]] bool shutdown() override;

  private:
    void runWorker();
    [[nodiscard]] PreferencesSaveStatus save(const PreferencesWriteRequest& request) noexcept;

    PreferencesStore& store_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::optional<PreferencesWriteRequest> pending_;
    std::optional<PreferencesRevision> active_revision_;
    std::optional<PreferencesWriteCompletion> completion_;
    std::optional<PreferencesWriteCompletion> latest_completed_;
    bool shutdown_requested_{false};
    bool stopped_{false};
};

} // namespace music_player::m0
