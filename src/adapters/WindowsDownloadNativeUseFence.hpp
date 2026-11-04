#pragma once

#include <memory>
#include <optional>

namespace music_player::m0 {

// Trusted adapter-only coordination, never profile ownership or a QML grant.
// A closed quiescent fence proves only that its own tickets acknowledged work.
class WindowsDownloadNativeUseFence final {
    struct Impl;

  public:
    enum class State { OpenIdle, OpenBusy, ClosingBusy, ClosedQuiescent, Uncertain };
    class Ticket final {
      public:
        ~Ticket();
        Ticket(Ticket&& other) noexcept;
        Ticket& operator=(Ticket&& other) noexcept;
        Ticket(const Ticket&) = delete;
        Ticket& operator=(const Ticket&) = delete;
        [[nodiscard]] bool acknowledgeNoChild();
        [[nodiscard]] bool acknowledgeNativeStopped();

      private:
        friend class WindowsDownloadNativeUseFence;
        explicit Ticket(std::shared_ptr<Impl> impl);
        void abandon() noexcept;
        [[nodiscard]] bool acknowledge();
        std::shared_ptr<Impl> impl_;
        bool pending_{false};
    };

    WindowsDownloadNativeUseFence();
    ~WindowsDownloadNativeUseFence();
    WindowsDownloadNativeUseFence(const WindowsDownloadNativeUseFence&) = delete;
    WindowsDownloadNativeUseFence& operator=(const WindowsDownloadNativeUseFence&) = delete;
    [[nodiscard]] std::optional<Ticket> begin();
    [[nodiscard]] State closeAdmission();
    [[nodiscard]] State state() const;

  private:
    std::shared_ptr<Impl> impl_;
};
} // namespace music_player::m0
