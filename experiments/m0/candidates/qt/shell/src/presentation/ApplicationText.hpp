#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace music_player::m0 {

class ApplicationText final {
    Q_DECLARE_TR_FUNCTIONS(ApplicationText)

  public:
    [[nodiscard]] static QString locateAudioDialogTitle() { return tr("Localizar arquivo de áudio"); }

    [[nodiscard]] static QString localAudioDialogFilter(const QStringList& patterns) {
        return tr("Áudio local (%1);;Todos os arquivos (*)").arg(patterns.join(QLatin1Char{' '}));
    }

    [[nodiscard]] static QString openSourcesFailureTitle() { return tr("Não foi possível abrir os arquivos"); }

    [[nodiscard]] static QString openSourcesFailureMessage(const QString& detail, const bool timed_out) {
        if (detail == QStringLiteral("tooManyPaths") || detail == QStringLiteral("payloadTooLarge"))
            return tr("A solicitação contém arquivos demais ou excede o limite permitido.");
        if (detail == QStringLiteral("queueFull"))
            return tr("O player está processando muitas solicitações. Tente novamente em instantes.");
        if (detail == QStringLiteral("temporarilyUnavailable"))
            return tr("O player está concluindo uma operação. Tente novamente em instantes.");
        if (detail == QStringLiteral("shuttingDown"))
            return tr("O player está sendo encerrado. Tente abrir os arquivos novamente.");
        if (timed_out)
            return tr("O player já está aberto, mas não respondeu a tempo.");
        return tr("Não foi possível encaminhar os arquivos para o player.");
    }
};

} // namespace music_player::m0
