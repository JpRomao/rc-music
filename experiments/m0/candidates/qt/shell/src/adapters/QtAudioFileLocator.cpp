#include "adapters/QtAudioFileLocator.hpp"

#include <QFileDialog>
#include <QScopeGuard>
#include <QUrl>
#include <QWindow>

#include <utility>

namespace music_player::m0 {
namespace {

[[nodiscard]] AudioFileLocationResult selectedResult(const QFileDialog& dialog, int result) {
    if (result != QDialog::Accepted)
        return {.status = AudioFileLocationStatus::Cancelled};
    const QList<QUrl> selected = dialog.selectedUrls();
    if (selected.size() != 1 || !selected.front().isLocalFile())
        return {.status = AudioFileLocationStatus::Failed};
    const QString path = selected.front().toLocalFile();
    if (path.isEmpty() || path.contains(QChar::Null))
        return {.status = AudioFileLocationStatus::Failed};
#ifdef Q_OS_WIN
    const std::filesystem::path source(path.toStdWString());
#else
    const std::filesystem::path source(path.toUtf8().toStdString());
#endif
    if (!source.is_absolute())
        return {.status = AudioFileLocationStatus::Failed};
    return {.status = AudioFileLocationStatus::Selected, .source_path = source};
}

} // namespace

QtAudioFileLocator::QtAudioFileLocator(QString title, QString filter, QObject* parent)
    : QtAudioFileLocator(std::move(title), std::move(filter), [] { return new QFileDialog; }, parent) {}

QtAudioFileLocator::QtAudioFileLocator(QString title, QString filter, DialogFactory factory, QObject* parent)
    : QObject(parent), title_(std::move(title)), filter_(std::move(filter)), factory_(std::move(factory)) {}

QtAudioFileLocator::~QtAudioFileLocator() { cancel(); }

void QtAudioFileLocator::bindWindow(QWindow& window) {
    const QPointer<QtAudioFileLocator> guard(this);
    cancel();
    if (!guard)
        return;
    if (window_)
        disconnect(window_, nullptr, this, nullptr);
    window_ = &window;
    connect(&window, &QObject::destroyed, this, [this] {
        window_.clear();
        cancel();
    });
}

bool QtAudioFileLocator::locate(const std::filesystem::path& missing_source, AudioFileLocationCompletion completion) {
    if (pending_ || !window_ || missing_source.empty() || !completion || !factory_)
        return false;
    // Never probe or navigate to the missing source: it may be on a disconnected or remote drive.
    const auto factory = factory_;
    const QPointer<QtAudioFileLocator> guard(this);
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
                [this, request] { finish(request, {.status = AudioFileLocationStatus::Failed}); });
        candidate->setAcceptMode(QFileDialog::AcceptOpen);
        candidate->setFileMode(QFileDialog::ExistingFile);
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
                finish(request, selectedResult(*selected_dialog, result));
            } catch (...) {
                finish(request, {.status = AudioFileLocationStatus::Failed});
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

void QtAudioFileLocator::cancel() noexcept {
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

void QtAudioFileLocator::finish(std::uint64_t generation, AudioFileLocationResult result) {
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
