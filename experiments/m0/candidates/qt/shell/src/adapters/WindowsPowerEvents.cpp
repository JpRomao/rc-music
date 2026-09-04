#include "adapters/WindowsPowerEvents.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

#include <qt_windows.h>

#include <array>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::array registration_retry_intervals_ms{1'000, 5'000, 30'000};

} // namespace

WindowsPowerEvents::WindowsPowerEvents(std::unique_ptr<SuspendResumeNativeBoundary> boundary, QObject* parent)
    : QObject(parent), boundary_(boundary ? std::move(boundary) : makeWin32SuspendResumeNativeBoundary()),
      application_(qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    registration_retry_timer_ = new QTimer(this);
    registration_retry_timer_->setObjectName(QStringLiteral("systemPowerEventsRegistrationRetryTimer"));
    registration_retry_timer_->setSingleShot(true);
    registration_retry_timer_->setTimerType(Qt::CoarseTimer);
    connect(registration_retry_timer_, &QTimer::timeout, this, [this] { static_cast<void>(attemptNativeBinding()); });
    if (application_ != nullptr) {
        application_->installNativeEventFilter(this);
        native_filter_installed_ = true;
        connect(application_, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState) {
            if (shutdown_ || available_ || notification_window_ == nullptr)
                return;
            registration_retry_attempt_ = 0;
            static_cast<void>(attemptNativeBinding());
        });
    }
}

WindowsPowerEvents::~WindowsPowerEvents() { shutdown(); }

bool WindowsPowerEvents::start() {
    if (shutdown_ || boundary_ == nullptr || !native_filter_installed_)
        return false;
    if (available_)
        return true;

    registration_retry_attempt_ = 0;
    if (notification_window_ == nullptr) {
        notification_window_ = std::make_unique<QWindow>();
        notification_window_->setObjectName(QStringLiteral("systemPowerEventsNotificationWindow"));
    }
    return attemptNativeBinding();
}

bool WindowsPowerEvents::available() const noexcept { return available_; }

void WindowsPowerEvents::setEventHandler(SystemPowerEventHandler handler) {
    if (shutdown_)
        return;
    event_handler_ = std::move(handler);
    last_delivered_event_.reset();
}

void WindowsPowerEvents::shutdown() noexcept {
    if (shutdown_)
        return;
    shutdown_ = true;
    event_handler_ = {};
    if (native_filter_installed_ && application_ != nullptr)
        application_->removeNativeEventFilter(this);
    native_filter_installed_ = false;
    releaseBinding();
}

bool WindowsPowerEvents::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) {
    if (shutdown_ || !available_ || event_type != "windows_generic_MSG" || message == nullptr)
        return false;

    const auto* const native_message = static_cast<const MSG*>(message);
    if (native_message->message != WM_POWERBROADCAST ||
        reinterpret_cast<std::uintptr_t>(native_message->hwnd) != native_window_handle_)
        return false;

    std::optional<SystemPowerEvent> event;
    if (native_message->wParam == PBT_APMSUSPEND)
        event = SystemPowerEvent::Suspending;
    else if (native_message->wParam == PBT_APMRESUMEAUTOMATIC)
        event = SystemPowerEvent::Resumed;
    if (!event.has_value())
        return false;

    if (result != nullptr)
        *result = TRUE;
    deliverEvent(*event);
    return true;
}

bool WindowsPowerEvents::attemptNativeBinding() {
    if (shutdown_ || boundary_ == nullptr || !native_filter_installed_ || notification_window_ == nullptr)
        return false;
    if (available_ && boundary_bound_)
        return true;

    registration_retry_timer_->stop();
    const auto native_handle = static_cast<std::uintptr_t>(notification_window_->winId());
    if (native_handle == 0) {
        scheduleRegistrationRetry();
        return false;
    }
    if (!boundary_->bind(native_handle)) {
        boundary_->shutdown();
        scheduleRegistrationRetry();
        return false;
    }

    boundary_bound_ = true;
    native_window_handle_ = native_handle;
    registration_retry_attempt_ = 0;
    available_ = true;
    return true;
}

void WindowsPowerEvents::scheduleRegistrationRetry() {
    registration_retry_timer_->stop();
    if (shutdown_ || notification_window_ == nullptr || boundary_bound_ ||
        registration_retry_attempt_ >= registration_retry_intervals_ms.size())
        return;
    registration_retry_timer_->setInterval(registration_retry_intervals_ms[registration_retry_attempt_]);
    ++registration_retry_attempt_;
    registration_retry_timer_->start();
}

void WindowsPowerEvents::deliverEvent(SystemPowerEvent event) noexcept {
    if (!event_handler_ || last_delivered_event_ == event)
        return;

    SystemPowerEventHandler handler;
    try {
        handler = event_handler_;
    } catch (...) {
        return;
    }
    last_delivered_event_ = event;
    try {
        handler(event);
    } catch (...) {
        // Native messages must never unwind through Qt's Win32 event dispatcher.
    }
}

void WindowsPowerEvents::releaseBinding() noexcept {
    registration_retry_timer_->stop();
    registration_retry_attempt_ = 0;
    native_window_handle_ = 0;
    last_delivered_event_.reset();
    if (boundary_bound_) {
        boundary_bound_ = false;
        boundary_->shutdown();
    }
    available_ = false;
    notification_window_.reset();
}

} // namespace music_player::m0
