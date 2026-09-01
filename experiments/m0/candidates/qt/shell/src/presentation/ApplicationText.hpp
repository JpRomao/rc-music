#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace music_player::m0 {

class ApplicationText final {
    Q_DECLARE_TR_FUNCTIONS(ApplicationText)

  public:
    [[nodiscard]] static QString localAudioDialogFilter(const QStringList& patterns) {
        return tr("Áudio local (%1);;Todos os arquivos (*)").arg(patterns.join(QLatin1Char{' '}));
    }
};

} // namespace music_player::m0
