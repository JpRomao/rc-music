#include "WindowsDownloadNativeUseFence.hpp"

#include <mutex>
#include <utility>

namespace music_player::m0 {
struct WindowsDownloadNativeUseFence::Impl final {
    std::mutex mutex;
    State state{State::OpenIdle};
};
WindowsDownloadNativeUseFence::WindowsDownloadNativeUseFence() : impl_(std::make_shared<Impl>()) {}
WindowsDownloadNativeUseFence::~WindowsDownloadNativeUseFence() = default;
WindowsDownloadNativeUseFence::Ticket::Ticket(std::shared_ptr<Impl> impl) : impl_(std::move(impl)), pending_(true) {}
WindowsDownloadNativeUseFence::Ticket::~Ticket() { abandon(); }
WindowsDownloadNativeUseFence::Ticket::Ticket(Ticket&& other) noexcept
    : impl_(std::move(other.impl_)), pending_(std::exchange(other.pending_, false)) {}
WindowsDownloadNativeUseFence::Ticket& WindowsDownloadNativeUseFence::Ticket::operator=(Ticket&& other) noexcept {
    if (this != &other) {
        abandon();
        impl_ = std::move(other.impl_);
        pending_ = std::exchange(other.pending_, false);
    }
    return *this;
}
void WindowsDownloadNativeUseFence::Ticket::abandon() noexcept {
    if (!pending_ || !impl_)
        return;
    const std::lock_guard lock(impl_->mutex);
    impl_->state = State::Uncertain;
    pending_ = false;
}
bool WindowsDownloadNativeUseFence::Ticket::acknowledge() {
    if (!pending_ || !impl_)
        return false;
    const std::lock_guard lock(impl_->mutex);
    if (impl_->state == State::OpenBusy)
        impl_->state = State::OpenIdle;
    else if (impl_->state == State::ClosingBusy)
        impl_->state = State::ClosedQuiescent;
    else
        return false;
    pending_ = false;
    return true;
}
bool WindowsDownloadNativeUseFence::Ticket::acknowledgeNoChild() { return acknowledge(); }
bool WindowsDownloadNativeUseFence::Ticket::acknowledgeNativeStopped() { return acknowledge(); }
std::optional<WindowsDownloadNativeUseFence::Ticket> WindowsDownloadNativeUseFence::begin() {
    const std::lock_guard lock(impl_->mutex);
    if (impl_->state != State::OpenIdle)
        return std::nullopt;
    impl_->state = State::OpenBusy;
    return Ticket{impl_};
}
WindowsDownloadNativeUseFence::State WindowsDownloadNativeUseFence::closeAdmission() {
    const std::lock_guard lock(impl_->mutex);
    if (impl_->state == State::OpenIdle)
        impl_->state = State::ClosedQuiescent;
    else if (impl_->state == State::OpenBusy)
        impl_->state = State::ClosingBusy;
    return impl_->state;
}
WindowsDownloadNativeUseFence::State WindowsDownloadNativeUseFence::state() const {
    const std::lock_guard lock(impl_->mutex);
    return impl_->state;
}
} // namespace music_player::m0
