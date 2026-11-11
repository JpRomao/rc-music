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

struct WindowsSourceVolumeNativeOperations final {
    // Offscreen tests can give the private receiver a synthetic handle.
    std::function<std::uintptr_t(QWindow&)> receiver_handle;
};

class WindowsSourceVolumeEvents final : public QObject, private QAbstractNativeEventFilter {
    Q_OBJECT

  public:
    explicit WindowsSourceVolumeEvents(QObject* parent = nullptr);
    explicit WindowsSourceVolumeEvents(WindowsSourceVolumeNativeOperations operations, QObject* parent = nullptr);
    ~WindowsSourceVolumeEvents() override;

    [[nodiscard]] bool start();

  signals:
    void volumeRemoved(std::uint32_t drive_mask);

  private:
    bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;

    WindowsSourceVolumeNativeOperations operations_;
    QPointer<QGuiApplication> application_;
    std::unique_ptr<QWindow> receiver_;
    std::uintptr_t native_window_handle_{};
};

} // namespace music_player::m0
