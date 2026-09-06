#pragma once

#include "m0/ports/AudioFileLocator.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QFileDialog;
class QWindow;

namespace music_player::m0 {

class QtAudioFileDialog;

class QtAudioFileLocator final : public QObject, public AudioFileLocator {
    Q_OBJECT

  public:
    // The factory transfers ownership of a fresh dialog. Production uses the native dialog;
    // adapter tests may supply a widget dialog without adding executable test hooks.
    using DialogFactory = std::function<QFileDialog*()>;
    QtAudioFileLocator(QString title, QString filter, QObject* parent = nullptr);
    QtAudioFileLocator(QString title, QString filter, DialogFactory factory, QObject* parent = nullptr);
    ~QtAudioFileLocator() override;

    void bindWindow(QWindow& window);
    [[nodiscard]] bool locate(const std::filesystem::path& missing_source,
                              AudioFileLocationCompletion completion) override;
    void cancel() noexcept override;

  private:
    std::unique_ptr<QtAudioFileDialog> dialog_;
};

} // namespace music_player::m0
