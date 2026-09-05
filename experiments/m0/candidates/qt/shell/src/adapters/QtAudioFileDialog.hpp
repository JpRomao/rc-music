#pragma once

#include "m0/ports/AudioFilesPicker.hpp"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>

class QFileDialog;
class QWindow;

namespace music_player::m0 {

// Internal, side-effect-free normalization of the selection supplied by either dialog backend.
[[nodiscard]] AudioFilesSelectionResult audioFilesSelectionFromUrls(const QList<QUrl>& selected, bool multiple);

// Internal lifecycle shared by the single-file locator and the multiple-file picker.
// It deliberately performs no media probing and accepts only local absolute results.
class QtAudioFileDialog final : public QObject {
  public:
    using DialogFactory = std::function<QFileDialog*()>;
    QtAudioFileDialog(QString title, QString filter, bool multiple, DialogFactory factory);
    ~QtAudioFileDialog() override;

    void bindWindow(QWindow& window);
    [[nodiscard]] bool pick(AudioFilesSelectionCompletion completion);
    void cancel() noexcept;

  private:
    void finish(std::uint64_t generation, AudioFilesSelectionResult result);

    QString title_;
    QString filter_;
    DialogFactory factory_;
    QPointer<QWindow> window_;
    QPointer<QFileDialog> dialog_;
    AudioFilesSelectionCompletion completion_;
    std::uint64_t generation_{0};
    bool pending_{false};
    bool multiple_{false};
};

} // namespace music_player::m0
