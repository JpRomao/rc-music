#pragma once

#include <QWidget>

#include <functional>

namespace music_player::m0 {
class WindowsWebView2Surface final : public QWidget {
  public:
    using QWidget::QWidget;
    std::function<void()> resized;
    std::function<void(Qt::FocusReason)> focused;

    bool moveFocusOut(Qt::FocusReason reason);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
};
} // namespace music_player::m0
