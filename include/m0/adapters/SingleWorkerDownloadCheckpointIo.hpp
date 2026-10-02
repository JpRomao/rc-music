#pragma once

#include "m0/ports/DownloadCheckpointIo.hpp"

#include <functional>
#include <memory>

namespace music_player::m0 {

// The factory and its inert store belong to the worker, including destruction.
// shutdown() closes admission without waiting for active native I/O. The owner
// keeps borrowed dependencies alive until stopped(); joining in the destructor
// is a fallback, not the application's normal UI shutdown protocol.
class SingleWorkerDownloadCheckpointIo final : public DownloadCheckpointIo {
  public:
    using Factory = std::function<std::unique_ptr<DownloadCheckpointStore>()>;

    explicit SingleWorkerDownloadCheckpointIo(Factory factory);
    ~SingleWorkerDownloadCheckpointIo() override;

    SingleWorkerDownloadCheckpointIo(const SingleWorkerDownloadCheckpointIo&) = delete;
    SingleWorkerDownloadCheckpointIo& operator=(const SingleWorkerDownloadCheckpointIo&) = delete;
    SingleWorkerDownloadCheckpointIo(SingleWorkerDownloadCheckpointIo&&) = delete;
    SingleWorkerDownloadCheckpointIo& operator=(SingleWorkerDownloadCheckpointIo&&) = delete;

    [[nodiscard]] DownloadCheckpointIoCommand startLoad(std::uint64_t operation_id) override;
    [[nodiscard]] DownloadCheckpointIoCommand startSave(std::uint64_t operation_id,
                                                        const DownloadCheckpoint& checkpoint) override;
    [[nodiscard]] std::optional<DownloadCheckpointIoResult> poll(std::uint64_t operation_id) override;
    void shutdown() override;
    [[nodiscard]] bool stopped() const override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
