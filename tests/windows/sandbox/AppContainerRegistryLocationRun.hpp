#pragma once

#include "AppContainerRegistryLocationProtocol.hpp"
#include "adapters/WindowsDownloadProcess.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace music_player::sandbox_probe {
// Test-only process coordinator. The trusted bridge owns the authenticated
// runtime/pins and launches with the exact live owner's capability. These DTOs
// do not establish profile creation provenance or authorize profile cleanup.
class AppContainerRegistryLocationRun final {
  public:
    static constexpr qint64 execution_timeout_ms = 5000, drain_timeout_ms = 5000;
    static constexpr quint64 maximum_stderr_bytes = 4096;
    static constexpr quint64 maximum_memory_bytes = 64 * 1024 * 1024;
    struct Config final {
        m0::WindowsDownloadProcessConfig prepared_process{};
        sandbox_registry_location::Request request{};
        std::shared_ptr<const m0::WindowsDownloadAppContainer> expected_capability{};
    };
    class Port {
      public:
        virtual ~Port() = default;
        [[nodiscard]] virtual bool start(const m0::WindowsDownloadProcessConfig& config, qint64 now_ms) = 0;
        [[nodiscard]] virtual m0::WindowsDownloadProcessUpdate poll(qint64 now_ms) = 0;
        virtual void cancel() = 0;
    };
    enum class State { Idle, Running, Draining, Complete, Refused, Uncertain };
    enum class Reason {
        None, Configuration, Clock, Start, Exception, Timeout, Limit, Frame, Correlation, Process, ExitCode, Fence,
        Cancelled
    };
    struct Result final {
        State state{State::Idle};
        Reason reason{Reason::None};
        bool start_called{false}, started{false}, cancel_called{false}, terminal_observed{false};
        // Only terminal process state AND the exact capability's OpenIdle fence
        // witness transport stop. Failed/start false alone are never an ACK.
        bool transport_stopped{false}, captured{false};
        // Last scalar update with stdout/stderr chunks removed. Job evidence is
        // accumulated separately from semantic reply and exit-code acceptance.
        m0::WindowsDownloadProcessUpdate process{};
        std::vector<std::uint8_t> stdout_bytes{};
        QByteArray stderr_bytes{};
        std::optional<sandbox_registry_location::Reply> reply{};
    };

    // Passive construction copies borrowed Qt strings/bytes before any port
    // call. One trusted worker, no retries, wait loop or implicit cancellation.
    AppContainerRegistryLocationRun(Config config, std::shared_ptr<Port> port);
    ~AppContainerRegistryLocationRun();
    AppContainerRegistryLocationRun(const AppContainerRegistryLocationRun&) = delete;
    AppContainerRegistryLocationRun& operator=(const AppContainerRegistryLocationRun&) = delete;
    [[nodiscard]] const Result& start(qint64 now_ms) noexcept;
    [[nodiscard]] const Result& poll(qint64 now_ms) noexcept;
    [[nodiscard]] const Result& cancel(qint64 now_ms) noexcept;
    [[nodiscard]] const Result& result() const noexcept;

  private:
    struct Impl;
    // A started invocation lacking ACK deliberately retains this state, Port,
    // runtime and pins until process exit. Destruction never manufactures ACK
    // and performs no allocation, port call or profile cleanup.
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
