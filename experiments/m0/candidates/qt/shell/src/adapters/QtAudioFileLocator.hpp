#pragma once

#include "m0/ports/AudioFileLocator.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>

class QFileDialog;
class QWindow;

namespace music_player::m0 {

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
    void finish(std::uint64_t generation, AudioFileLocationResult result);

    QString title_;
    QString filter_;
    DialogFactory factory_;
    QPointer<QWindow> window_;
    QPointer<QFileDialog> dialog_;
    AudioFileLocationCompletion completion_;
    std::uint64_t generation_{0};
    bool pending_{false};
};

} // namespace music_player::m0
