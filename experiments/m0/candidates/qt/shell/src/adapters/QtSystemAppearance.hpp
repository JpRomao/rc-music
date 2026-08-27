#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>

#include <functional>

class QGuiApplication;

namespace music_player::m0 {

struct SystemAppearanceSnapshot {
    bool dark{false};
    // Unknown/unsupported motion settings fail closed instead of enabling animation.
    bool reduced_motion{true};

    bool operator==(const SystemAppearanceSnapshot&) const = default;
};

class QtSystemAppearance final : public QObject, private QAbstractNativeEventFilter {
    Q_OBJECT

  public:
    // Construct on the GUI thread after QGuiApplication and before creating the UI.
    // The default reader only observes the OS; a supplied reader keeps tests deterministic.
    explicit QtSystemAppearance(std::function<SystemAppearanceSnapshot()> reader = {}, QObject* parent = nullptr);
    ~QtSystemAppearance() override;

    [[nodiscard]] SystemAppearanceSnapshot snapshot() const noexcept;
    void refresh();

  signals:
    void appearanceChanged();

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;

    std::function<SystemAppearanceSnapshot()> reader_;
    SystemAppearanceSnapshot snapshot_{};
    QPointer<QGuiApplication> application_;
};

} // namespace music_player::m0
