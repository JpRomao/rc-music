#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>

#include <cstdint>
#include <functional>
#include <memory>

class QGuiApplication;
class QWindow;

namespace music_player::m0 {

struct WindowsTaskbarNativeOperations final {
    // Adapter-local seams: tests may fail registration/allocation without changing the desktop shell.
    std::function<std::uint32_t()> register_message;
    std::function<std::unique_ptr<QWindow>()> create_window;
};

class WindowsTaskbarEvents final : public QObject, private QAbstractNativeEventFilter {
    Q_OBJECT

  public:
    explicit WindowsTaskbarEvents(QObject* parent = nullptr);
    explicit WindowsTaskbarEvents(WindowsTaskbarNativeOperations operations, QObject* parent = nullptr);
    ~WindowsTaskbarEvents() override;

    // Construct after QGuiApplication. Failed registration/receiver creation is retryable by start().
    // Non-Windows builds, a missing GUI application and calls after shutdown fail closed.
    [[nodiscard]] bool start();
    void shutdown() noexcept;
    // Shell presence only: this does not prove that a particular notification icon is registered.
    [[nodiscard]] static bool shellAvailable() noexcept;

  signals:
    void taskbarCreated();

  private:
    bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;

    WindowsTaskbarNativeOperations operations_;
    QPointer<QGuiApplication> application_;
    std::unique_ptr<QWindow> notification_window_;
    std::uintptr_t native_window_handle_{0};
    std::uint32_t message_id_{0};
    bool native_filter_installed_{false};
    bool shutdown_{false};
};

} // namespace music_player::m0
