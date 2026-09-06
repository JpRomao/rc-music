#pragma once

#include "m0/ports/SystemMediaControls.hpp"

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <cstdint>
#include <memory>
#include <optional>

class QWindow;
class QEvent;

namespace music_player::m0 {

class SmtcNativeBoundary {
  public:
    using CommandCallback = SystemMediaCommandHandler;

    virtual ~SmtcNativeBoundary() = default;
    [[nodiscard]] virtual bool bind(std::uintptr_t native_window_handle, CommandCallback callback) noexcept = 0;
    [[nodiscard]] virtual bool publish(const SystemMediaSnapshot& snapshot) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<SmtcNativeBoundary> makeWinRtSmtcNativeBoundary();

class WindowsSmtcAdapter final : public QObject, public SystemMediaControls {
    Q_OBJECT

  public:
    explicit WindowsSmtcAdapter(std::unique_ptr<SmtcNativeBoundary> boundary = {}, QObject* parent = nullptr);
    ~WindowsSmtcAdapter() override;

    [[nodiscard]] bool bindWindow(QWindow& window);
    [[nodiscard]] bool available() const noexcept override;
    void setCommandHandler(SystemMediaCommandHandler handler) override;
    [[nodiscard]] bool publish(const SystemMediaSnapshot& snapshot) noexcept override;
    void shutdown() noexcept override;

  signals:
    void availabilityChanged(bool available);
    void playRequested();
    void pauseRequested();
    void previousRequested();
    void nextRequested();
    void seekRequested(qint64 absolute_position_ms);

  private:
    struct CallbackGate;

    void dispatchCommand(SystemMediaCommand command, std::uint64_t binding_generation);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void releaseNativeBinding() noexcept;
    void releaseBinding() noexcept;
    void setAvailable(bool available);

    std::unique_ptr<SmtcNativeBoundary> boundary_;
    std::shared_ptr<CallbackGate> callback_gate_;
    QPointer<QWindow> window_;
    QMetaObject::Connection window_destroyed_connection_;
    SystemMediaCommandHandler command_handler_;
    std::optional<SystemMediaSnapshot> latest_snapshot_;
    std::optional<SystemMediaSnapshot> published_snapshot_;
    bool available_{false};
    bool boundary_bound_{false};
    bool surface_present_{false};
    bool shutdown_{false};
    std::uint64_t binding_generation_{0};
};

} // namespace music_player::m0
