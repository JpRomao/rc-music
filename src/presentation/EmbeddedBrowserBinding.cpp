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
void EmbeddedBrowserBinding::setDownloadSubmission(AsyncDownloadSubmission* submission) {
    if (closing_ || download_submission_ == submission)
        return;
    download_submission_ = submission;
    if (in_flight_window_) {
        applyDownloadSubmission(*in_flight_window_);
        return;
    }
    if (!window_)
        return;
    auto window = std::move(window_);
    const QPointer<EmbeddedBrowserBinding> guard{this};
    opening_ = true;
    in_flight_window_ = window.get();
    download_update_failed_ = false;
    applyDownloadSubmission(*window);
    if (guard) {
        in_flight_window_ = nullptr;
        if (!download_update_failed_) {
            window_ = std::move(window);
            opening_ = false;
            return;
        }
    }
    window->shutdown();
    if (guard) {
        opening_ = false;
        setError(tr("Não foi possível atualizar os downloads do navegador. Abra o navegador novamente."));
    }
}
void EmbeddedBrowserBinding::applyDownloadSubmission(EmbeddedBrowserWindow& window) {
    const QPointer<EmbeddedBrowserBinding> guard{this};
    try {
        window.setDownloadSubmission(download_submission_);
    } catch (...) {
        if (guard)
            download_update_failed_ = true;
    }
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
        if (guard && window && owner_handle_) {
            in_flight_window_ = window.get();
            download_update_failed_ = false;
            applyDownloadSubmission(*window);
            if (guard && owner_handle_ && !download_update_failed_)
                shown = window->show(owner_handle_);
        }
    } catch (...) {
        // Exceptions may contain paths, URLs or runtime diagnostics. The UI gets
        // only a fixed message with a recovery action.
    }
    if (guard) {
        in_flight_window_ = nullptr;
        shown = shown && !download_update_failed_;
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
