#include "EmbeddedBrowserBinding.hpp"

#include <QPointer>

#include <utility>

namespace music_player::m0 {
EmbeddedBrowserBinding::EmbeddedBrowserBinding(EmbeddedBrowserWindowFactory factory, QObject* parent)
    : QObject(parent), factory_(std::move(factory)) {}
EmbeddedBrowserBinding::~EmbeddedBrowserBinding() {
    closing_ = true;
    auto window = std::move(window_);
    if (window)
        window->shutdown();
}
void EmbeddedBrowserBinding::setOwnerHandle(std::uintptr_t owner_native_handle) {
    if (!closing_)
        owner_handle_ = owner_native_handle;
}
void EmbeddedBrowserBinding::open() {
    if (opening_ || closing_)
        return;
    if (!owner_handle_) {
        setError(tr("A janela do player ainda não está pronta. Tente abrir o navegador novamente."));
        return;
    }
    opening_ = true;
    const QPointer<EmbeddedBrowserBinding> guard{this};
    // Keep ownership on the stack while calling a port. A callback may destroy
    // the binding; it must not destroy the window during its own show call.
    auto window = std::move(window_);
    bool shown = false;
    try {
        if (!window) {
            // The callable also outlives deletion of the binding from a factory
            // callback. No factory or native window is created by construction.
            auto factory = factory_;
            if (factory)
                window = factory();
        }
        if (guard && window && owner_handle_)
            shown = window->show(owner_handle_);
    } catch (...) {
        // Exceptions may contain paths, URLs or runtime diagnostics. The UI gets
        // only a fixed message with a recovery action.
    }
    if (!guard || !shown) {
        if (window) {
            window->shutdown();
            window.reset();
        }
        if (!guard)
            return;
        opening_ = false;
        setError(tr("Não foi possível abrir o navegador. Verifique se o Microsoft Edge WebView2 Runtime está "
                    "instalado e tente novamente."));
        return;
    }
    window_ = std::move(window);
    opening_ = false;
    setError({});
}
QString EmbeddedBrowserBinding::errorText() const { return error_; }
void EmbeddedBrowserBinding::setError(QString message) {
    if (error_ == message)
        return;
    error_ = std::move(message);
    emit errorChanged();
}
} // namespace music_player::m0
