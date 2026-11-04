#pragma once

#include "AppContainerNetworkObserver.hpp"
#include "AppContainerObserverProcess.hpp"
#include "AppContainerObserverProtocol.hpp"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace music_player::sandbox_probe {
// Manual-probe actor only. No child, profile, elevation or policy mutation.
// All native inspection and the bounded synchronous loop begin in run().
class AppContainerObserverActor final {
  public:
    struct Config final {
        QString workspace_root{}, directory{}, trusted_probe_path{}, trusted_helper_source{};
    };
    struct Calls final {
        std::function<AppContainerObserverProcess::Snapshot()> current_process{};
        std::function<AppContainerObserverProcess::Snapshot(std::uint32_t)> open_driver{};
        std::function<AppContainerObserverProcess::Poll()> poll_driver{};
        AppContainerNetworkObserver::Calls network_calls{};
        std::function<std::int64_t()> monotonic_ms{};
        std::function<void()> pump{};
    };
    enum class Reason {
        None,
        InvalidConfiguration,
        Files,
        Request,
        ObserverIdentity,
        DriverIdentity,
        Image,
        Helper,
        DriverStopped,
        DriverUnknown,
        Clock,
        ReadyWrite,
        NetworkStart,
        NetworkStop,
        ResultWrite,
        Exception,
        AlreadyRun
    };
    struct RunResult final {
        // Zero reports healthy transport/teardown, never successful isolation.
        int exit_code{1};
        Reason reason{Reason::InvalidConfiguration};
        bool ready_written{false}, result_written{false}, resources_retained{false};
        std::optional<AppContainerObserverProtocol::Result> result{};
    };

    // Binds only. The driver's process object is retained by the bound calls.
    [[nodiscard]] static Calls systemCalls();
    AppContainerObserverActor(Config config, Calls calls);
    ~AppContainerObserverActor();
    AppContainerObserverActor(const AppContainerObserverActor&) = delete;
    AppContainerObserverActor& operator=(const AppContainerObserverActor&) = delete;
    // One shot, including rejected/reentrant calls. No automatic retries.
    [[nodiscard]] RunResult run();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
