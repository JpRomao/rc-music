#pragma once

#include "m0/ports/AudioFilesPicker.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QFileDialog;
class QWindow;

namespace music_player::m0 {

class QtAudioFileDialog;

class QtAudioFilesPicker final : public QObject, public AudioFilesPicker {
    Q_OBJECT

  public:
    // The factory transfers a fresh dialog to the adapter; the default uses the native backend.
    using DialogFactory = std::function<QFileDialog*()>;
    QtAudioFilesPicker(QString title, QString filter, QObject* parent = nullptr);
    QtAudioFilesPicker(QString title, QString filter, DialogFactory factory, QObject* parent = nullptr);
    ~QtAudioFilesPicker() override;

    void bindWindow(QWindow& window);
    [[nodiscard]] bool pick(AudioFilesSelectionCompletion completion) override;
    void cancel() noexcept override;

  private:
    std::unique_ptr<QtAudioFileDialog> dialog_;
};

} // namespace music_player::m0
