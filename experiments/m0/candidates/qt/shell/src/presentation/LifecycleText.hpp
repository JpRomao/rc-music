#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace music_player::m0 {

// Shared by the QML notice and the native menu; this experimental opt-out is session-only.
class LifecycleText : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString showAction READ showAction CONSTANT)
    Q_PROPERTY(QString hideAction READ hideAction CONSTANT)
    Q_PROPERTY(QString exitAction READ exitAction CONSTANT)
    Q_PROPERTY(QString noTrack READ noTrack CONSTANT)
    Q_PROPERTY(QString trayTitle READ trayTitle CONSTANT)
    Q_PROPERTY(QString trayMessage READ trayMessage CONSTANT)
    Q_PROPERTY(QString continueAction READ continueAction CONSTANT)
    Q_PROPERTY(QString cancelAction READ cancelAction CONSTANT)
    Q_PROPERTY(QString trayNoticeEnabled READ trayNoticeEnabled CONSTANT)
    Q_PROPERTY(QString unavailableTitle READ unavailableTitle CONSTANT)
    Q_PROPERTY(QString unavailableMessage READ unavailableMessage CONSTANT)
    Q_PROPERTY(QString dismissAction READ dismissAction CONSTANT)

  public:
    explicit LifecycleText(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] QString showAction() const { return tr("Mostrar"); }
    [[nodiscard]] QString hideAction() const { return tr("Ocultar"); }
    [[nodiscard]] QString exitAction() const { return tr("Sair"); }
    [[nodiscard]] QString noTrack() const { return tr("Nenhuma faixa selecionada"); }
    [[nodiscard]] QString trayTitle() const { return tr("Continuar na bandeja"); }
    [[nodiscard]] QString trayMessage() const { return tr("O player continuará na bandeja. Use Sair para encerrar."); }
    [[nodiscard]] QString continueAction() const { return tr("Continuar"); }
    [[nodiscard]] QString cancelAction() const { return tr("Cancelar"); }
    [[nodiscard]] QString trayNoticeEnabled() const { return tr("Avisar ao fechar para a bandeja nesta sessão"); }
    [[nodiscard]] QString unavailableTitle() const { return tr("Bandeja indisponível"); }
    [[nodiscard]] QString unavailableMessage() const {
        return tr("Não há bandeja disponível neste ambiente. Ao fechar, a janela é minimizada normalmente.");
    }
    [[nodiscard]] QString dismissAction() const { return tr("Entendi"); }
};

} // namespace music_player::m0
