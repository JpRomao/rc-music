#include "AppContainerObserverDriver.hpp"

#include <algorithm>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Driver = AppContainerObserverDriver;
using Protocol = AppContainerObserverProtocol;
using Process = AppContainerObserverProcess;
using Identity = AppContainerObserverIdentity;
using Observer = AppContainerNetworkObserver;
bool processShape(const Process::Snapshot& value) {
    return value.complete && value.error == 0 && value.pid != 0 && value.creation_filetime != 0 &&
           !value.image_path.empty() && value.image_path.size() <= Process::maximum_image_path_units &&
           std::find(value.image_path.begin(), value.image_path.end(), L'\0') == value.image_path.end();
}
bool sameSid(const Identity::Bytes& left, const QByteArray& right) {
    return left.size() == static_cast<std::size_t>(right.size()) &&
           std::equal(left.begin(), left.end(), reinterpret_cast<const std::uint8_t*>(right.constData()));
}
bool configuration(const Driver::Config& value) {
    return value.start_ms >= 0 && value.request_sha256.size() == 32 &&
           !Protocol::encodeRequest(value.request).isEmpty() && processShape(value.driver) &&
           Identity::normalDriver(value.driver.token) && value.request.driver_pid == value.driver.pid &&
           value.request.driver_creation_filetime == value.driver.creation_filetime &&
           sameSid(value.driver.token.user_sid, value.request.driver_user_sid);
}
bool sameToken(const Identity::Snapshot& left, const Identity::Snapshot& right) {
    return left.complete == right.complete && left.error == right.error &&
           left.failed_information_class == right.failed_information_class && left.user_sid == right.user_sid &&
           left.session_id == right.session_id && left.elevated == right.elevated &&
           left.elevation_type == right.elevation_type && left.integrity_rid == right.integrity_rid &&
           left.ui_access == right.ui_access && left.app_container == right.app_container &&
           left.app_container_sid == right.app_container_sid && left.capability_count == right.capability_count &&
           left.administrators_group_present == right.administrators_group_present &&
           left.administrators_group_attributes == right.administrators_group_attributes &&
           std::equal(left.restricted_sids.begin(), left.restricted_sids.end(), right.restricted_sids.begin(),
                      right.restricted_sids.end(),
                      [](const auto& a, const auto& b) { return a.sid == b.sid && a.attributes == b.attributes; }) &&
           std::equal(left.enabled_privileges.begin(), left.enabled_privileges.end(), right.enabled_privileges.begin(),
                      right.enabled_privileges.end(),
                      [](const auto& a, const auto& b) { return a.name == b.name && a.attributes == b.attributes; });
}
bool sameProcess(const Process::Snapshot& left, const Process::Snapshot& right) {
    return left.complete == right.complete && left.error == right.error && left.pid == right.pid &&
           left.creation_filetime == right.creation_filetime && left.image_path == right.image_path &&
           sameToken(left.token, right.token);
}
bool knownPoll(const Process::Poll& value) {
    if (value.error != 0)
        return false;
    switch (value.state) {
    case Process::State::Running:
        return !value.exit_code;
    case Process::State::Stopped:
        return value.exit_code.has_value();
    case Process::State::Unknown:
        return false;
    }
    return false;
}
bool healthyTerminal(const Protocol::Result& value) {
    if (value.status == Protocol::ResultStatus::AdmissionRejected)
        return !value.ready && !value.report;
    if (value.status != Protocol::ResultStatus::ObserverResult || !value.report || value.report->context_retained)
        return false;
    const auto& report = *value.report;
    switch (report.state) {
    case Observer::State::Stopped:
        return report.unsubscribe_error == 0 && report.close_error == 0;
    case Observer::State::Unavailable:
        return report.subscribe_error == Observer::not_called && report.unsubscribe_error == Observer::not_called &&
               (report.close_error == 0 || report.close_error == Observer::not_called);
    case Observer::State::Idle:
    case Observer::State::Observing:
    case Observer::State::StopUnconfirmed:
        return false;
    }
    return false;
}
} // namespace

struct AppContainerObserverDriver::Impl final {
    Config config;
    State state{State::AwaitingReady};
    Reason reason{Reason::None};
    std::optional<AppContainerObserverProtocol::Ready> ready{};
    std::optional<AppContainerObserverProtocol::Result> result{};
    std::optional<std::uint32_t> exit_code{};
    std::optional<Peer> peer{};
    std::int64_t last_ms{};
    std::optional<std::int64_t> ready_ms{};
    explicit Impl(Config value) : config(std::move(value)), last_ms(config.start_ms) {
        if (!configuration(config))
            fail(Reason::Configuration);
    }
    void fail(const Reason error) noexcept {
        if (state != State::Uncertain) {
            state = State::Uncertain;
            reason = error;
        }
    }
    bool clock(const std::int64_t now_ms) {
        if (state == State::Uncertain)
            return false;
        if (now_ms < 0 || now_ms < last_ms) {
            fail(Reason::Clock);
            return false;
        }
        last_ms = now_ms;
        // A completed ACK does not expire merely because a caller later polls.
        if (state == State::Complete)
            return true;
        // Nonnegative ordered operands keep both subtractions in int64 range.
        if (ready_ms ? now_ms - *ready_ms > completion_timeout_ms : now_ms - config.start_ms > ready_timeout_ms) {
            fail(Reason::Timeout);
            return false;
        }
        return true;
    }
    bool authenticated(const Peer& value, const std::uint32_t pid, const std::uint64_t creation) const {
        return processShape(value.process) && Identity::elevatedObserver(value.process.token) &&
               value.process.token.session_id == config.driver.token.session_id &&
               value.process.pid != config.driver.pid && value.process.pid == pid &&
               value.process.creation_filetime == creation && value.image_pinned &&
               value.image_sha256 == config.request.probe_sha256 && knownPoll(value.poll) &&
               (!peer || (sameProcess(peer->process, value.process) && peer->image_sha256 == value.image_sha256));
    }
};
AppContainerObserverDriver::AppContainerObserverDriver(Config config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
AppContainerObserverDriver::~AppContainerObserverDriver() = default;
AppContainerObserverDriver::State AppContainerObserverDriver::state() const noexcept { return impl_->state; }
AppContainerObserverDriver::Reason AppContainerObserverDriver::reason() const noexcept { return impl_->reason; }
bool AppContainerObserverDriver::ready(const QByteArrayView frame, const Peer& peer, const std::int64_t now_ms,
                                       const std::uint64_t now_filetime) {
    auto& value = *impl_;
    if (!value.clock(now_ms))
        return false;
    if (value.state != State::AwaitingReady) {
        value.fail(Reason::State);
        return false;
    }
    auto parsed = Protocol::decodeReady(frame);
    if (!parsed || !Protocol::matchesRequest(value.config.request, value.config.request_sha256, *parsed)) {
        value.fail(Reason::Frame);
        return false;
    }
    if (!value.authenticated(peer, parsed->observer_pid, parsed->observer_creation_filetime) ||
        peer.poll.state != Process::State::Running) {
        value.fail(Reason::Peer);
        return false;
    }
    if (now_filetime < parsed->window_begin_filetime || now_filetime > parsed->window_end_filetime ||
        parsed->window_end_filetime - now_filetime < minimum_helper_ticks) {
        value.fail(Reason::Window);
        return false;
    }
    value.ready = std::move(parsed);
    value.peer = peer;
    value.ready_ms = now_ms;
    value.state = State::Ready;
    return true;
}
bool AppContainerObserverDriver::takeLaunchPermission() {
    if (impl_->state != State::Ready)
        return false;
    impl_->state = State::AwaitingResult;
    return true;
}
bool AppContainerObserverDriver::result(const QByteArrayView frame, const Peer& peer, const std::int64_t now_ms) {
    auto& value = *impl_;
    if (!value.clock(now_ms))
        return false;
    if (value.state != State::AwaitingReady && value.state != State::Ready && value.state != State::AwaitingResult) {
        value.fail(Reason::State);
        return false;
    }
    auto parsed = Protocol::decodeResult(frame);
    if (!parsed || !Protocol::matchesRequest(value.config.request, value.config.request_sha256, *parsed)) {
        value.fail(Reason::Frame);
        return false;
    }
    if (!value.authenticated(peer, parsed->observer_pid, parsed->observer_creation_filetime)) {
        value.fail(Reason::Peer);
        return false;
    }
    // An early terminal frame may reject admission or report startup failure.
    // It may not supply the Ready that this driver never accepted.
    if (value.ready ? parsed->status != Protocol::ResultStatus::ObserverResult || !parsed->ready ||
                          Protocol::encodeReady(*parsed->ready) != Protocol::encodeReady(*value.ready)
                    : parsed->ready.has_value()) {
        value.fail(Reason::Frame);
        return false;
    }
    if (!value.peer)
        value.peer = peer;
    value.result = std::move(parsed);
    // Accepted Ready proves that Subscribe had succeeded. An Unavailable
    // before-Subscribe result cannot describe that same observation.
    const bool unavailable_after_ready =
        value.ready && value.result->report && value.result->report->state == Observer::State::Unavailable;
    if (unavailable_after_ready || !healthyTerminal(*value.result)) {
        value.fail(value.result->report && value.result->report->context_retained ? Reason::Retained
                                                                                  : Reason::Nonterminal);
        return false;
    }
    value.state = State::AwaitingExit;
    return true;
}
bool AppContainerObserverDriver::stopped(const Peer& peer, const std::int64_t now_ms) {
    auto& value = *impl_;
    if (!value.clock(now_ms))
        return false;
    if (value.state != State::AwaitingExit || !value.peer || !value.result) {
        value.fail(Reason::State);
        return false;
    }
    if (!value.authenticated(peer, value.peer->process.pid, value.peer->process.creation_filetime) ||
        peer.poll.state != Process::State::Stopped || !peer.poll.exit_code) {
        value.fail(Reason::Peer);
        return false;
    }
    if (!healthyTerminal(*value.result)) {
        value.fail(Reason::Nonterminal);
        return false;
    }
    value.exit_code = peer.poll.exit_code;
    value.state = State::Complete;
    return true;
}
AppContainerObserverDriver::State AppContainerObserverDriver::tick(const std::int64_t now_ms) {
    static_cast<void>(impl_->clock(now_ms));
    return impl_->state;
}
void AppContainerObserverDriver::transportFailure() noexcept { impl_->fail(Reason::Transport); }
const std::optional<AppContainerObserverProtocol::Ready>& AppContainerObserverDriver::acceptedReady() const noexcept {
    return impl_->ready;
}
const std::optional<AppContainerObserverProtocol::Result>& AppContainerObserverDriver::result() const noexcept {
    return impl_->result;
}
std::optional<std::uint32_t> AppContainerObserverDriver::exitCode() const noexcept { return impl_->exit_code; }
bool AppContainerObserverDriver::canCleanup() const noexcept {
    return impl_->state == State::Complete && impl_->exit_code && impl_->result && healthyTerminal(*impl_->result);
}
bool AppContainerObserverDriver::proofEligible() const noexcept {
    if (!canCleanup() || *impl_->exit_code != 0 || !impl_->ready ||
        impl_->result->status != Protocol::ResultStatus::ObserverResult || !impl_->result->report)
        return false;
    const auto& report = *impl_->result->report;
    return report.state == Observer::State::Stopped && report.available && report.reason == Observer::Reason::None &&
           report.unsubscribe_error == 0 && report.close_error == 0 && !report.context_retained;
}
} // namespace music_player::sandbox_probe
