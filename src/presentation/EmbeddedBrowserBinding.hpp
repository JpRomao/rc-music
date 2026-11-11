#pragma once

#include "m0/ports/EmbeddedBrowserWindow.hpp"

#include <QObject>
#include <QString>

namespace music_player::m0 {
class EmbeddedBrowserBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)

  public:
    explicit EmbeddedBrowserBinding(EmbeddedBrowserWindowFactory factory, QObject* parent = nullptr);
    ~EmbeddedBrowserBinding() override;
    void setOwnerHandle(std::uintptr_t owner_native_handle);
    void setDownloadSubmission(AsyncDownloadSubmission* submission);
    Q_INVOKABLE void open();
    [[nodiscard]] QString errorText() const;

  signals:
    void errorChanged();

  private:
    void applyDownloadSubmission(EmbeddedBrowserWindow& window);
    void setError(QString message);
    EmbeddedBrowserWindowFactory factory_;
    std::unique_ptr<EmbeddedBrowserWindow> window_{};
    // The caller on the stack owns this window while it invokes the port.
    EmbeddedBrowserWindow* in_flight_window_{};
    AsyncDownloadSubmission* download_submission_{};
    std::uintptr_t owner_handle_{0};
    QString error_{};
    bool opening_{false};
    bool closing_{false};
    bool download_update_failed_{false};
};
} // namespace music_player::m0
