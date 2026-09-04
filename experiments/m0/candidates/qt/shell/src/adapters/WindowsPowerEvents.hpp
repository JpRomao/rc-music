#pragma once

#include "m0/ports/SystemPowerEvents.hpp"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class QGuiApplication;
class QTimer;
class QWindow;

namespace music_player::m0 {

class SuspendResumeNativeBoundary {
  public:
    virtual ~SuspendResumeNativeBoundary() = default;

    [[nodiscard]] virtual bool bind(std::uintptr_t native_window_handle) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<SuspendResumeNativeBoundary> makeWin32SuspendResumeNativeBoundary();

class WindowsPowerEvents final : public QObject, public SystemPowerEvents, private QAbstractNativeEventFilter {
    Q_OBJECT

  public:
    explicit WindowsPowerEvents(std::unique_ptr<SuspendResumeNativeBoundary> boundary = {}, QObject* parent = nullptr);
    ~WindowsPowerEvents() override;

    [[nodiscard]] bool start();
    [[nodiscard]] bool available() const noexcept override;
    void setEventHandler(SystemPowerEventHandler handler) override;
    void shutdown() noexcept override;

  private:
    bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;
    [[nodiscard]] bool attemptNativeBinding();
    void scheduleRegistrationRetry();
    void deliverEvent(SystemPowerEvent event) noexcept;
    void releaseBinding() noexcept;

    std::unique_ptr<SuspendResumeNativeBoundary> boundary_;
    QPointer<QGuiApplication> application_;
    std::unique_ptr<QWindow> notification_window_;
    QTimer* registration_retry_timer_{nullptr};
    SystemPowerEventHandler event_handler_;
    std::optional<SystemPowerEvent> last_delivered_event_;
    std::uintptr_t native_window_handle_{0};
    std::size_t registration_retry_attempt_{0};
    bool available_{false};
    bool boundary_bound_{false};
    bool native_filter_installed_{false};
    bool shutdown_{false};
};

} // namespace music_player::m0
