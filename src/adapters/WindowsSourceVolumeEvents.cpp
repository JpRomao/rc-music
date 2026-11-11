#include "adapters/WindowsSourceVolumeEvents.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QThread>
#include <QWindow>

#include <qt_windows.h>

#include <dbt.h>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::uint32_t valid_drive_mask = (std::uint32_t{1} << 26) - 1;

std::uintptr_t nativeReceiverHandle(QWindow& window) {
    const auto handle = static_cast<std::uintptr_t>(window.winId());
    return handle != 0 && IsWindow(reinterpret_cast<HWND>(handle)) ? handle : 0;
}

template <typename Payload> bool copyPayload(const void* address, Payload& payload) {
    SIZE_T copied = 0;
    return address != nullptr &&
           ReadProcessMemory(GetCurrentProcess(), address, &payload, sizeof(Payload), &copied) != FALSE &&
           copied == sizeof(Payload);
}

} // namespace

WindowsSourceVolumeEvents::WindowsSourceVolumeEvents(QObject* parent) : WindowsSourceVolumeEvents({}, parent) {}

WindowsSourceVolumeEvents::WindowsSourceVolumeEvents(WindowsSourceVolumeNativeOperations operations, QObject* parent)
    : QObject(parent), operations_(std::move(operations)) {
    if (!operations_.receiver_handle)
        operations_.receiver_handle = nativeReceiverHandle;
}

WindowsSourceVolumeEvents::~WindowsSourceVolumeEvents() {
    if (application_)
        application_->removeNativeEventFilter(this);
}

bool WindowsSourceVolumeEvents::start() {
    auto* const application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!application || thread() != application->thread() || QThread::currentThread() != application->thread())
        return false;
    if (receiver_)
        return true;

    try {
        // Volume notifications are broadcast to top-level windows without registration.
        auto receiver = std::make_unique<QWindow>();
        receiver->setObjectName(QStringLiteral("sourceVolumeEventsNotificationWindow"));
        const auto handle = operations_.receiver_handle(*receiver);
        if (handle == 0)
            return false;
        application_ = application;
        native_window_handle_ = handle;
        receiver_ = std::move(receiver);
        application_->installNativeEventFilter(this);
        return true;
    } catch (...) {
        if (application_)
            application_->removeNativeEventFilter(this);
        application_.clear();
        native_window_handle_ = 0;
        receiver_.reset();
        return false;
    }
}

bool WindowsSourceVolumeEvents::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) {
    if (native_window_handle_ == 0 || event_type != "windows_generic_MSG" || message == nullptr)
        return false;
    const auto& native = *static_cast<const MSG*>(message);
    if (reinterpret_cast<std::uintptr_t>(native.hwnd) != native_window_handle_ || native.message != WM_DEVICECHANGE ||
        native.wParam != DBT_DEVICEREMOVECOMPLETE)
        return false;

    // Copy bounded native payloads so an unreadable LPARAM cannot fault the dispatcher.
    const auto* address = reinterpret_cast<const void*>(native.lParam);
    DEV_BROADCAST_HDR header{};
    if (!copyPayload(address, header) || header.dbch_size != sizeof(DEV_BROADCAST_VOLUME) ||
        header.dbch_devicetype != DBT_DEVTYP_VOLUME)
        return false;
    DEV_BROADCAST_VOLUME volume{};
    if (!copyPayload(address, volume) || volume.dbcv_size != header.dbch_size ||
        volume.dbcv_devicetype != header.dbch_devicetype || (volume.dbcv_flags != 0 && volume.dbcv_flags != DBTF_MEDIA))
        return false;
    const auto mask = static_cast<std::uint32_t>(volume.dbcv_unitmask);
    if (mask == 0 || (mask & ~valid_drive_mask) != 0)
        return false;

    if (result != nullptr)
        *result = TRUE;
    try {
        emit volumeRemoved(mask);
    } catch (...) {
        // A consumer must not unwind through the native dispatcher.
    }
    return true;
}

} // namespace music_player::m0
