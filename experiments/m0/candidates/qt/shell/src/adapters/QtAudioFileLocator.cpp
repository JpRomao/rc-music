#include "adapters/QtAudioFileLocator.hpp"

#include "adapters/QtAudioFileDialog.hpp"

#include <QFileDialog>

#include <utility>

namespace music_player::m0 {

QtAudioFileLocator::QtAudioFileLocator(QString title, QString filter, QObject* parent)
    : QtAudioFileLocator(std::move(title), std::move(filter), [] { return new QFileDialog; }, parent) {}

QtAudioFileLocator::QtAudioFileLocator(QString title, QString filter, DialogFactory factory, QObject* parent)
    : QObject(parent),
      dialog_(std::make_unique<QtAudioFileDialog>(std::move(title), std::move(filter), false, std::move(factory))) {}

QtAudioFileLocator::~QtAudioFileLocator() = default;

void QtAudioFileLocator::bindWindow(QWindow& window) { dialog_->bindWindow(window); }

bool QtAudioFileLocator::locate(const std::filesystem::path& missing_source, AudioFileLocationCompletion completion) {
    // Never probe or navigate to the missing source: it may be on a disconnected or remote drive.
    if (missing_source.empty() || !completion)
        return false;
    return dialog_->pick([completion = std::move(completion)](AudioFilesSelectionResult result) {
        switch (result.status) {
        case AudioFilesSelectionStatus::Selected:
            completion(
                {.status = AudioFileLocationStatus::Selected, .source_path = std::move(result.source_paths.front())});
            break;
        case AudioFilesSelectionStatus::Cancelled:
            completion({.status = AudioFileLocationStatus::Cancelled});
            break;
        case AudioFilesSelectionStatus::Failed:
            completion({.status = AudioFileLocationStatus::Failed});
            break;
        }
    });
}

void QtAudioFileLocator::cancel() noexcept { dialog_->cancel(); }

} // namespace music_player::m0
