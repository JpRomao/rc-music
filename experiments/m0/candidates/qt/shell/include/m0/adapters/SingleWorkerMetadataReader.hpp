#pragma once

#include "m0/ports/MetadataReader.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace music_player::m0 {

struct SingleWorkerMetadataReaderOptions final {
    std::chrono::steady_clock::duration job_budget{std::chrono::seconds{2}};
    // Test seam: implementations must be thread-safe, non-blocking and non-throwing.
    std::function<std::chrono::steady_clock::time_point()> now{[] { return std::chrono::steady_clock::now(); }};
};

class SingleWorkerMetadataReader final : public MetadataReader {
  public:
    explicit SingleWorkerMetadataReader(std::unique_ptr<MetadataExtractor> extractor,
                                        SingleWorkerMetadataReaderOptions options = {});
    ~SingleWorkerMetadataReader() override;

    SingleWorkerMetadataReader(const SingleWorkerMetadataReader&) = delete;
    SingleWorkerMetadataReader& operator=(const SingleWorkerMetadataReader&) = delete;
    SingleWorkerMetadataReader(SingleWorkerMetadataReader&&) = delete;
    SingleWorkerMetadataReader& operator=(SingleWorkerMetadataReader&&) = delete;

    MetadataCommandResult startRead(MetadataReadRequest request) override;
    MetadataCommandResult cancelRead(MetadataGeneration generation) override;
    [[nodiscard]] std::optional<MetadataReadCompletion> tryTakeCompletion() override;
    MetadataCommandResult shutdown() override;

  private:
    using Deadline = std::chrono::steady_clock::time_point;

    struct WorkItem final {
        MetadataReadRequest request;
        Deadline deadline;
    };

    struct ReadyCompletion final {
        MetadataReadCompletion completion;
        Deadline deadline;
    };

    void runWorker();
    [[nodiscard]] static MetadataReadResult exceptionResult();
    [[nodiscard]] std::optional<Deadline> observeTime() const noexcept;

    std::unique_ptr<MetadataExtractor> extractor_;
    SingleWorkerMetadataReaderOptions options_;
    std::mutex mutex_{};
    std::condition_variable changed_{};
    std::mutex shutdown_mutex_{};
    MetadataGeneration highest_generation_{};
    std::optional<MetadataGeneration> cancelled_generation_{};
    std::optional<WorkItem> pending_{};
    std::optional<ReadyCompletion> completion_{};
    bool stopping_{false};
    std::jthread worker_{};
};

} // namespace music_player::m0
