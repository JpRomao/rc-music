#include "adapters/QtAudioFilesPicker.hpp"

#include "adapters/QtAudioFileDialog.hpp"

#include <QFileDialog>
#include <utility>

namespace music_player::m0 {

QtAudioFilesPicker::QtAudioFilesPicker(QString title, QString filter, QObject* parent)
    : QtAudioFilesPicker(std::move(title), std::move(filter), [] { return new QFileDialog; }, parent) {}
QtAudioFilesPicker::QtAudioFilesPicker(QString title, QString filter, DialogFactory factory, QObject* parent)
    : QObject(parent),
      dialog_(std::make_unique<QtAudioFileDialog>(std::move(title), std::move(filter), true, std::move(factory))) {}
QtAudioFilesPicker::~QtAudioFilesPicker() = default;
void QtAudioFilesPicker::bindWindow(QWindow& window) { dialog_->bindWindow(window); }
bool QtAudioFilesPicker::pick(AudioFilesSelectionCompletion completion) { return dialog_->pick(std::move(completion)); }
void QtAudioFilesPicker::cancel() noexcept { dialog_->cancel(); }

} // namespace music_player::m0
