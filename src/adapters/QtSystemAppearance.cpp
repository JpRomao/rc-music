#include "adapters/QtSystemAppearance.hpp"

#include <QAccessibilityHints>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <utility>

namespace music_player::m0 {
namespace {

#ifdef Q_OS_WIN
[[nodiscard]] QColor readWindowsColor(int element) {
    const auto color = GetSysColor(element);
    // COLORREF stores channels differently from QRgb; never reinterpret it as a QColor value.
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}
#endif

[[nodiscard]] HighContrastPalette readHighContrastPalette() {
#ifdef Q_OS_WIN
    return {readWindowsColor(COLOR_WINDOW), readWindowsColor(COLOR_WINDOWTEXT), readWindowsColor(COLOR_HIGHLIGHT),
            readWindowsColor(COLOR_HIGHLIGHTTEXT), readWindowsColor(COLOR_GRAYTEXT)};
#else
    // Qt palette fallback only; native Linux contrast propagation still needs platform validation.
    const auto palette = QGuiApplication::palette();
    return {palette.color(QPalette::Active, QPalette::Window), palette.color(QPalette::Active, QPalette::WindowText),
            palette.color(QPalette::Active, QPalette::Highlight),
            palette.color(QPalette::Active, QPalette::HighlightedText),
            palette.color(QPalette::Disabled, QPalette::WindowText)};
#endif
}

[[nodiscard]] SystemAppearanceSnapshot readSystemAppearance() {
    SystemAppearanceSnapshot result;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        const auto* const hints = QGuiApplication::styleHints();
        result.dark = hints != nullptr && hints->colorScheme() == Qt::ColorScheme::Dark;
        const auto* const accessibility = hints != nullptr ? hints->accessibility() : nullptr;
        if (accessibility != nullptr && accessibility->contrastPreference() == Qt::ContrastPreference::HighContrast)
            result.high_contrast = readHighContrastPalette();
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
    const auto* const hints = QGuiApplication::styleHints();
    if (hints == nullptr)
        return;
    connect(hints, &QStyleHints::colorSchemeChanged, this, &QtSystemAppearance::scheduleRefresh);
    if (const auto* const accessibility = hints->accessibility(); accessibility != nullptr)
        connect(accessibility, &QAccessibilityHints::contrastPreferenceChanged, this,
                &QtSystemAppearance::scheduleRefresh);
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

void QtSystemAppearance::scheduleRefresh() {
    if (refresh_pending_)
        return;
    refresh_pending_ = true;
    // Qt can emit appearance signals before replacing its palette. Read once after that turn;
    // the QObject context also discards this callback if the adapter is destroyed first.
    QMetaObject::invokeMethod(
        this,
        [this] {
            refresh_pending_ = false;
            refresh();
        },
        Qt::QueuedConnection);
}

bool QtSystemAppearance::eventFilter(QObject*, QEvent* event) {
    switch (event->type()) {
    case QEvent::ApplicationStateChange:
    case QEvent::WindowActivate:
    case QEvent::ApplicationPaletteChange:
    case QEvent::PaletteChange:
    case QEvent::ThemeChange:
        scheduleRefresh();
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
        if (native_message->message == WM_SETTINGCHANGE || native_message->message == WM_THEMECHANGED ||
            native_message->message == WM_SYSCOLORCHANGE)
            scheduleRefresh();
    }
#else
    Q_UNUSED(event_type)
    Q_UNUSED(message)
#endif
    return false;
}

} // namespace music_player::m0
