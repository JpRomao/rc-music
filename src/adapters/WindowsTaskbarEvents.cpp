#include "adapters/WindowsTaskbarEvents.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

#include <utility>

namespace music_player::m0 {

WindowsTaskbarEvents::WindowsTaskbarEvents(QObject* parent) : WindowsTaskbarEvents({}, parent) {}

WindowsTaskbarEvents::WindowsTaskbarEvents(WindowsTaskbarNativeOperations operations, QObject* parent)
    : QObject(parent), operations_(std::move(operations)),
      application_(qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    if (!operations_.register_message) {
        operations_.register_message = []() -> std::uint32_t {
#if defined(Q_OS_WIN)
            return RegisterWindowMessageW(L"TaskbarCreated");
#else
            return 0;
#endif
        };
    }
    if (!operations_.create_window)
        operations_.create_window = [] { return std::make_unique<QWindow>(); };
}

WindowsTaskbarEvents::~WindowsTaskbarEvents() { shutdown(); }

bool WindowsTaskbarEvents::start() {
#if defined(Q_OS_WIN)
    if (shutdown_ || application_ == nullptr)
        return false;
    if (native_filter_installed_)
        return true;

    try {
        const auto message_id = operations_.register_message();
        if (message_id < 0xC000U || message_id > 0xFFFFU)
            return false;
        auto receiver = operations_.create_window();
        if (receiver == nullptr)
            return false;
        receiver->setObjectName(QStringLiteral("taskbarEventsNotificationWindow"));
        const auto handle = static_cast<std::uintptr_t>(receiver->winId());
        if (handle == 0 || !IsWindow(reinterpret_cast<HWND>(handle)))
            return false;

        // A hidden top-level window receives TaskbarCreated broadcasts; HWND_MESSAGE would not.
        notification_window_ = std::move(receiver);
        native_window_handle_ = handle;
        message_id_ = message_id;
        application_->installNativeEventFilter(this);
        native_filter_installed_ = true;
        return true;
    } catch (...) {
        native_window_handle_ = 0;
        message_id_ = 0;
        notification_window_.reset();
        return false;
    }
#else
    return false;
#endif
}

void WindowsTaskbarEvents::shutdown() noexcept {
    if (shutdown_)
        return;
    shutdown_ = true;
    if (native_filter_installed_ && application_ != nullptr)
        application_->removeNativeEventFilter(this);
    native_filter_installed_ = false;
    native_window_handle_ = 0;
    message_id_ = 0;
    notification_window_.reset();
}

bool WindowsTaskbarEvents::shellAvailable() noexcept {
#if defined(Q_OS_WIN)
    return GetShellWindow() != nullptr && FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
#else
    return false;
#endif
}

bool WindowsTaskbarEvents::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) {
#if defined(Q_OS_WIN)
    if (shutdown_ || !native_filter_installed_ || event_type != "windows_generic_MSG" || message == nullptr)
        return false;
    const auto* const native_message = static_cast<const MSG*>(message);
    if (message_id_ == 0 || native_message->message != message_id_ ||
        reinterpret_cast<std::uintptr_t>(native_message->hwnd) != native_window_handle_)
        return false;

    if (result != nullptr)
        *result = 0;
    try {
        const QPointer<WindowsTaskbarEvents> guarded{this};
        QMetaObject::invokeMethod(
            this,
            [guarded] {
                if (guarded != nullptr && !guarded->shutdown_ && guarded->native_filter_installed_)
                    emit guarded->taskbarCreated();
            },
            Qt::QueuedConnection);
    } catch (...) {
        // Never unwind through the native dispatcher if the queued delivery cannot be allocated.
    }
    return true;
#else
    Q_UNUSED(event_type)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

} // namespace music_player::m0
