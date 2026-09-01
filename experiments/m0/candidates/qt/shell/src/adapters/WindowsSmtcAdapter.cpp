#include "adapters/WindowsSmtcAdapter.hpp"

#include <QEvent>
#include <QMetaObject>
#include <QPlatformSurfaceEvent>
#include <QWindow>

#include <mutex>
#include <utility>

namespace music_player::m0 {

struct WindowsSmtcAdapter::CallbackGate final {
    std::mutex mutex;
    WindowsSmtcAdapter* target{nullptr};
};

WindowsSmtcAdapter::WindowsSmtcAdapter(std::unique_ptr<SmtcNativeBoundary> boundary, QObject* parent)
    : QObject(parent), boundary_(boundary ? std::move(boundary) : makeWinRtSmtcNativeBoundary()),
      callback_gate_(std::make_shared<CallbackGate>()) {
    callback_gate_->target = this;
}

WindowsSmtcAdapter::~WindowsSmtcAdapter() {
    {
        const std::scoped_lock lock(callback_gate_->mutex);
        callback_gate_->target = nullptr;
    }
    shutdown();
}

bool WindowsSmtcAdapter::bindWindow(QWindow& window) {
    if (shutdown_ || boundary_ == nullptr)
        return false;
    if (available_ && window_ == &window)
        return true;

    releaseBinding();
    const auto native_handle = static_cast<std::uintptr_t>(window.winId());
    if (native_handle == 0)
        return false;

    const auto generation = ++binding_generation_;
    const std::weak_ptr<CallbackGate> weak_gate{callback_gate_};
    const auto callback = [weak_gate, generation](SystemMediaCommand command) {
        const auto gate = weak_gate.lock();
        if (!gate)
            return;
        const std::scoped_lock lock(gate->mutex);
        if (gate->target == nullptr)
            return;
        QMetaObject::invokeMethod(
            gate->target,
            [weak_gate, command, generation] {
                const auto queued_gate = weak_gate.lock();
                if (!queued_gate)
                    return;
                WindowsSmtcAdapter* target{nullptr};
                {
                    const std::scoped_lock queued_lock(queued_gate->mutex);
                    target = queued_gate->target;
                }
                if (target != nullptr)
                    target->dispatchCommand(command, generation);
            },
            Qt::QueuedConnection);
    };

    if (!boundary_->bind(native_handle, callback)) {
        boundary_->shutdown();
        return false;
    }
    boundary_bound_ = true;
    window_ = &window;
    surface_present_ = true;
    window.installEventFilter(this);
    window_destroyed_connection_ = connect(&window, &QObject::destroyed, this, [this] { releaseBinding(); });

    if (latest_snapshot_.has_value() && !boundary_->publish(*latest_snapshot_)) {
        releaseBinding();
        return false;
    }
    published_snapshot_ = latest_snapshot_;
    setAvailable(true);
    return true;
}

bool WindowsSmtcAdapter::available() const noexcept { return available_; }

void WindowsSmtcAdapter::setCommandHandler(SystemMediaCommandHandler handler) { command_handler_ = std::move(handler); }

bool WindowsSmtcAdapter::publish(const SystemMediaSnapshot& snapshot) noexcept {
    if (shutdown_)
        return false;
    try {
        latest_snapshot_ = snapshot;
    } catch (...) {
        return false;
    }
    if (!available_ || published_snapshot_ == latest_snapshot_)
        return true;
    if (!boundary_->publish(snapshot)) {
        releaseBinding();
        return false;
    }
    try {
        published_snapshot_ = snapshot;
    } catch (...) {
        releaseBinding();
        return false;
    }
    return true;
}

void WindowsSmtcAdapter::shutdown() noexcept {
    if (shutdown_)
        return;
    shutdown_ = true;
    command_handler_ = {};
    releaseBinding();
}

void WindowsSmtcAdapter::dispatchCommand(SystemMediaCommand command, std::uint64_t binding_generation) {
    if (shutdown_ || !available_ || binding_generation != binding_generation_)
        return;
    switch (command.kind) {
    case SystemMediaCommandKind::Play:
        emit playRequested();
        break;
    case SystemMediaCommandKind::Pause:
        emit pauseRequested();
        break;
    case SystemMediaCommandKind::Previous:
        emit previousRequested();
        break;
    case SystemMediaCommandKind::Next:
        emit nextRequested();
        break;
    case SystemMediaCommandKind::Seek:
        emit seekRequested(command.position_ms);
        break;
    }
    if (command_handler_)
        command_handler_(command);
}

bool WindowsSmtcAdapter::eventFilter(QObject* const watched, QEvent* const event) {
    if (watched == window_.data() && event->type() == QEvent::PlatformSurface) {
        const auto* const surface_event = static_cast<QPlatformSurfaceEvent*>(event);
        if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            surface_present_ = false;
            releaseNativeBinding();
        } else if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated && !shutdown_ &&
                   !available_) {
            surface_present_ = true;
            const QPointer<QWindow> target = window_;
            const std::uint64_t generation = binding_generation_;
            QMetaObject::invokeMethod(
                this,
                [this, target, generation] {
                    if (!shutdown_ && target != nullptr && window_ == target && !available_ && surface_present_ &&
                        binding_generation_ == generation)
                        static_cast<void>(bindWindow(*target));
                },
                Qt::QueuedConnection);
        }
    }
    return QObject::eventFilter(watched, event);
}

void WindowsSmtcAdapter::releaseNativeBinding() noexcept {
    ++binding_generation_;
    published_snapshot_.reset();
    if (boundary_bound_) {
        boundary_bound_ = false;
        boundary_->shutdown();
    }
    setAvailable(false);
}

void WindowsSmtcAdapter::releaseBinding() noexcept {
    if (window_ != nullptr)
        window_->removeEventFilter(this);
    QObject::disconnect(window_destroyed_connection_);
    window_destroyed_connection_ = {};
    window_.clear();
    surface_present_ = false;
    releaseNativeBinding();
}

void WindowsSmtcAdapter::setAvailable(bool available) {
    if (available_ == available)
        return;
    available_ = available;
    emit availabilityChanged(available_);
}

} // namespace music_player::m0
