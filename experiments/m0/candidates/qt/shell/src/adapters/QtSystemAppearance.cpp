#include "adapters/QtSystemAppearance.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QStyleHints>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <utility>

namespace music_player::m0 {
namespace {

[[nodiscard]] SystemAppearanceSnapshot readSystemAppearance() {
    SystemAppearanceSnapshot result;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        const auto* const hints = QGuiApplication::styleHints();
        result.dark = hints != nullptr && hints->colorScheme() == Qt::ColorScheme::Dark;
    }
#ifdef Q_OS_WIN
    // SPI_GETCLIENTAREAANIMATION writes only this local BOOL, never a system preference.
    BOOL animations_enabled{FALSE};
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations_enabled, 0) != FALSE)
        result.reduced_motion = animations_enabled == FALSE;
#else
    // No supported native motion reader is integrated on Linux/other platforms yet.
    // Preserve the conservative reduced-motion default rather than guessing from theme.
#endif
    return result;
}

} // namespace

QtSystemAppearance::QtSystemAppearance(std::function<SystemAppearanceSnapshot()> reader, QObject* parent)
    : QObject(parent), reader_(reader ? std::move(reader) : readSystemAppearance), snapshot_(reader_()),
      application_(qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    if (application_ == nullptr)
        return;
    application_->installEventFilter(this);
#ifdef Q_OS_WIN
    application_->installNativeEventFilter(this);
#endif
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &QtSystemAppearance::refresh);
}

QtSystemAppearance::~QtSystemAppearance() {
    if (application_ == nullptr)
        return;
    application_->removeEventFilter(this);
#ifdef Q_OS_WIN
    application_->removeNativeEventFilter(this);
#endif
}

SystemAppearanceSnapshot QtSystemAppearance::snapshot() const noexcept { return snapshot_; }

void QtSystemAppearance::refresh() {
    const auto next = reader_();
    if (next == snapshot_)
        return;
    snapshot_ = next;
    emit appearanceChanged();
}

bool QtSystemAppearance::eventFilter(QObject*, QEvent* event) {
    switch (event->type()) {
    case QEvent::ApplicationStateChange:
    case QEvent::WindowActivate:
    case QEvent::ApplicationPaletteChange:
    case QEvent::PaletteChange:
    case QEvent::ThemeChange:
        refresh();
        break;
    default:
        break;
    }
    return false;
}

bool QtSystemAppearance::nativeEventFilter(const QByteArray& event_type, void* message, qintptr*) {
#ifdef Q_OS_WIN
    if (message != nullptr && (event_type == "windows_generic_MSG" || event_type == "windows_dispatcher_MSG")) {
        const auto* const native_message = static_cast<const MSG*>(message);
        if (native_message->message == WM_SETTINGCHANGE || native_message->message == WM_THEMECHANGED)
            refresh();
    }
#else
    Q_UNUSED(event_type)
    Q_UNUSED(message)
#endif
    return false;
}

} // namespace music_player::m0
