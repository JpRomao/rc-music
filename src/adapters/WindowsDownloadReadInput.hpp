#pragma once

#include <cstdint>
#include <memory>

namespace music_player::m0 {
class WindowsDownloadProcess;

// Trusted parent-only input capability. The factory reopens the borrowed file
// object with reduced access and an independent cursor, never by pathname.
class WindowsDownloadReadInput final {
  public:
    [[nodiscard]] static std::shared_ptr<const WindowsDownloadReadInput> fromHandle(std::uintptr_t borrowed);
    ~WindowsDownloadReadInput();
    WindowsDownloadReadInput(const WindowsDownloadReadInput&) = delete;
    WindowsDownloadReadInput& operator=(const WindowsDownloadReadInput&) = delete;
    [[nodiscard]] std::uint64_t handleValue() const noexcept;

  private:
    friend class WindowsDownloadProcess;
    struct Impl;
    explicit WindowsDownloadReadInput(std::unique_ptr<Impl> impl);
    [[nodiscard]] bool acquireUse() const noexcept;
    [[nodiscard]] bool beginInheritance() const noexcept;
    [[nodiscard]] bool endInheritance() const noexcept;
    void releaseUse() const noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
