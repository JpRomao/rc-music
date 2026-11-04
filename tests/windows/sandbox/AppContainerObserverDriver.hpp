#pragma once

#include "AppContainerObserverProcess.hpp"
#include "AppContainerObserverProtocol.hpp"

#include <QByteArrayView>

#include <cstdint>
#include <memory>
#include <optional>

namespace music_player::sandbox_probe {
// Pure, one-shot coordinator. It performs no native calls, file I/O or launches.
// Peer.image_pinned is evidence supplied by the native Main, not created here.
class AppContainerObserverDriver final {
  public:
    static constexpr std::int64_t ready_timeout_ms = 120'000;
    static constexpr std::int64_t completion_timeout_ms = 20'000;
    static constexpr std::uint64_t minimum_helper_ticks = 10ULL * 10'000'000;
    enum class State { AwaitingReady, Ready, AwaitingResult, AwaitingExit, Complete, Uncertain };
    enum class Reason {
        None,
        Configuration,
        Clock,
        Timeout,
        State,
        Frame,
        Peer,
        Window,
        Retained,
        Nonterminal,
        Transport
    };
    struct Config final {
        AppContainerObserverProtocol::Request request{};
        QByteArray request_sha256{};
        AppContainerObserverProcess::Snapshot driver{};
        std::int64_t start_ms{};
    };
    struct Peer final {
        AppContainerObserverProcess::Snapshot process{};
        QByteArray image_sha256{};
        bool image_pinned{false};
        AppContainerObserverProcess::Poll poll{};
    };
    explicit AppContainerObserverDriver(Config config);
    ~AppContainerObserverDriver();
    AppContainerObserverDriver(const AppContainerObserverDriver&) = delete;
    AppContainerObserverDriver& operator=(const AppContainerObserverDriver&) = delete;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] Reason reason() const noexcept;
    [[nodiscard]] bool ready(QByteArrayView frame, const Peer& peer, std::int64_t now_ms, std::uint64_t now_filetime);
    [[nodiscard]] bool takeLaunchPermission();
    [[nodiscard]] bool result(QByteArrayView frame, const Peer& peer, std::int64_t now_ms);
    [[nodiscard]] bool stopped(const Peer& peer, std::int64_t now_ms);
    [[nodiscard]] State tick(std::int64_t now_ms);
    void transportFailure() noexcept;
    [[nodiscard]] const std::optional<AppContainerObserverProtocol::Ready>& acceptedReady() const noexcept;
    [[nodiscard]] const std::optional<AppContainerObserverProtocol::Result>& result() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> exitCode() const noexcept;
    // Cleanup requires an explicit stopped witness and healthy terminal result.
    // Neither cleanup nor proof eligibility means that network isolation passed.
    [[nodiscard]] bool canCleanup() const noexcept;
    [[nodiscard]] bool proofEligible() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
