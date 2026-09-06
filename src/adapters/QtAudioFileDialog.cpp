#include "adapters/QtAudioFileDialog.hpp"

#include "boundary/FilesystemPath.hpp"

#include <QFileDialog>
#include <QScopeGuard>
#include <QUrl>
#include <QWindow>

#include <utility>

namespace music_player::m0 {

AudioFilesSelectionResult audioFilesSelectionFromUrls(const QList<QUrl>& selected, bool multiple) {
    if (selected.isEmpty() || (!multiple && selected.size() != 1))
        return {.status = AudioFilesSelectionStatus::Failed};
    AudioFilesSelectionResult selection{.status = AudioFilesSelectionStatus::Selected};
    selection.source_paths.reserve(static_cast<std::size_t>(selected.size()));
    for (const QUrl& url : selected) {
        auto source = localFilesystemPathFromUrl(url);
        if (!source.has_value())
            return {.status = AudioFilesSelectionStatus::Failed};
        selection.source_paths.push_back(std::move(*source));
    }
    return selection;
}

QtAudioFileDialog::QtAudioFileDialog(QString title, QString filter, bool multiple, DialogFactory factory)
    : title_(std::move(title)), filter_(std::move(filter)), factory_(std::move(factory)), multiple_(multiple) {}

QtAudioFileDialog::~QtAudioFileDialog() { cancel(); }

void QtAudioFileDialog::bindWindow(QWindow& window) {
    const QPointer<QtAudioFileDialog> guard(this);
    const QPointer<QWindow> candidate_window(&window);
    cancel();
    if (!guard)
        return;
    if (window_)
        disconnect(window_, nullptr, this, nullptr);
    window_ = candidate_window;
    if (!window_)
        return;
    connect(window_, &QObject::destroyed, this, [this] {
        window_.clear();
        cancel();
    });
}

bool QtAudioFileDialog::pick(AudioFilesSelectionCompletion completion) {
    if (pending_ || !window_ || !completion || !factory_)
        return false;
    const auto factory = factory_;
    const QPointer<QtAudioFileDialog> guard(this);
    const std::uint64_t request = ++generation_;
    completion_ = std::move(completion);
    pending_ = true;
    QPointer<QFileDialog> candidate;
    auto release_unclaimed = qScopeGuard([&] {
        if (candidate)
            candidate->deleteLater();
    });
    const auto still_current = [&] { return guard && pending_ && generation_ == request && window_ && candidate; };
    try {
        candidate = factory();
        if (!still_current()) {
            if (guard && pending_ && generation_ == request)
                cancel();
            return false;
        }
        dialog_ = candidate;
        connect(candidate, &QObject::destroyed, this,
                [this, request] { finish(request, {.status = AudioFilesSelectionStatus::Failed}); });
        candidate->setAcceptMode(QFileDialog::AcceptOpen);
        candidate->setFileMode(multiple_ ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
        candidate->setSupportedSchemes({QStringLiteral("file")});
        candidate->setOption(QFileDialog::ReadOnly);
        candidate->setOption(QFileDialog::DontUseCustomDirectoryIcons);
        candidate->setNameFilter(filter_);
        candidate->setWindowTitle(title_);
        if (!still_current())
            return false;
        static_cast<void>(candidate->winId());
        if (!still_current())
            return false;
        candidate->windowHandle()->setTransientParent(window_);
        if (!still_current())
            return false;
        connect(candidate, &QDialog::finished, this, [this, request, selected_dialog = candidate](int result) {
            if (!pending_ || generation_ != request || !selected_dialog)
                return;
            try {
                finish(request, result == QDialog::Accepted
                                    ? audioFilesSelectionFromUrls(selected_dialog->selectedUrls(), multiple_)
                                    : AudioFilesSelectionResult{.status = AudioFilesSelectionStatus::Cancelled});
            } catch (...) {
                finish(request, {.status = AudioFilesSelectionStatus::Failed});
            }
        });
        // QDialog::open is asynchronous WindowModal. Unlike exec(), Qt's Windows backend
        // dispatches the native dialog on its COM dialog thread without blocking UI timers.
        candidate->open();
        release_unclaimed.dismiss();
        return true;
    } catch (...) {
        if (guard && pending_ && generation_ == request)
            cancel();
        return false;
    }
}

void QtAudioFileDialog::cancel() noexcept {
    ++generation_;
    pending_ = false;
    completion_ = {};
    const QPointer<QFileDialog> cancelled = dialog_;
    dialog_.clear();
    if (!cancelled)
        return;
    disconnect(cancelled, nullptr, this, nullptr);
    cancelled->reject();
    if (cancelled)
        cancelled->deleteLater();
}

void QtAudioFileDialog::finish(std::uint64_t generation, AudioFilesSelectionResult result) {
    if (!pending_ || generation_ != generation)
        return;
    pending_ = false;
    auto completion = std::move(completion_);
    const QPointer<QFileDialog> finished = dialog_;
    dialog_.clear();
    if (finished) {
        disconnect(finished, nullptr, this, nullptr);
        finished->deleteLater();
    }
    // State and ownership are released before a callback can cancel, restart or delete us.
    try {
        completion(std::move(result));
    } catch (...) {
        // Application callbacks must not unwind through a Qt signal or the native dialog boundary.
    }
}

} // namespace music_player::m0
