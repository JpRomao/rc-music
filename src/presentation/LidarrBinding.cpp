#include "LidarrBinding.hpp"
#include "adapters/QtLidarrService.hpp"

namespace music_player::m0 {
namespace {
QVariantList profiles(const QVector<QtLidarrService::Profile>& values) {
    QVariantList result;
    result.reserve(values.size());
    for (const auto& value : values)
        result.push_back(QVariantMap{{"id", value.id}, {"name", value.name}});
    return result;
}
} // namespace
LidarrBinding::LidarrBinding(QtLidarrService& service, QObject* parent) : QObject(parent), service_(service) {
    connect(&service_, &QtLidarrService::changed, this, &LidarrBinding::changed);
}
bool LidarrBinding::connected() const { return service_.connected(); }
bool LidarrBinding::busy() const { return service_.busy(); }
bool LidarrBinding::interactionAllowed() const { return !exit_preparing_ && !shutting_down_; }
QString LidarrBinding::serverAddress() const {
    const auto address = service_.serverAddress();
    return address.isEmpty() ? QStringLiteral("http://localhost:8686") : address;
}
QString LidarrBinding::statusText() const {
    using Error = QtLidarrService::Error;
    switch (service_.error()) {
    case Error::Configuration:
        return tr("Informe um endereço HTTP/HTTPS e uma chave de API válida.");
    case Error::NotConnected:
        return tr("Conecte ao Lidarr antes de pesquisar.");
    case Error::Busy:
        return tr("Aguarde a operação atual ou cancele-a.");
    case Error::Authentication:
        return tr("O Lidarr recusou a chave de API. Confira a chave nas configurações do servidor.");
    case Error::Network:
        return tr("Não foi possível acessar o Lidarr. Confira o endereço e se o servidor está disponível.");
    case Error::Http:
        return tr("O Lidarr recusou a operação. Confira o estado do servidor antes de tentar novamente.");
    case Error::Timeout:
        return tr("O Lidarr não respondeu a tempo. Se solicitou um download, confira no Lidarr antes de repetir.");
    case Error::Response:
    case Error::Limit:
        return tr("A resposta do Lidarr é inválida ou excedeu o limite. Confira a versão e refine a pesquisa.");
    case Error::Selection:
        return tr("A seleção expirou. Pesquise novamente e selecione o álbum desejado.");
    case Error::Unavailable:
        return tr("Configure uma pasta de biblioteca e perfis no Lidarr antes de adicionar um álbum.");
    case Error::Cancelled:
        return tr("Operação cancelada. Um pedido já enviado pode continuar no Lidarr; confira antes de repetir.");
    case Error::None:
        break;
    }
    using Status = QtLidarrService::Status;
    switch (service_.status()) {
    case Status::Disconnected:
        return tr("Conecte à sua instância do Lidarr para pesquisar artistas ou álbuns.");
    case Status::Connecting:
        return tr("Conectando ao Lidarr…");
    case Status::Searching:
        return tr("Pesquisando no Lidarr…");
    case Status::Requesting:
        return tr("Solicitando busca do álbum no Lidarr…");
    case Status::Requested:
        return tr("Busca solicitada no Lidarr. Acompanhe os resultados e downloads no servidor.");
    case Status::Ready:
        return tr("Conectado ao Lidarr. Pesquise artistas ou álbuns.");
    case Status::Error:
        return tr("Não foi possível concluir a operação no Lidarr.");
    }
    return {};
}
QVariantList LidarrBinding::results() const {
    QVariantList result;
    result.reserve(service_.results().size());
    for (const auto& item : service_.results())
        result.push_back(QVariantMap{{"key", item.key},
                                     {"kind", item.kind == QtLidarrService::Kind::Album ? "album" : "artist"},
                                     {"title", item.title},
                                     {"artist", item.artist},
                                     {"year", item.year},
                                     {"existingId", item.existing_id}});
    return result;
}
QStringList LidarrBinding::rootFolders() const { return service_.rootFolders(); }
QVariantList LidarrBinding::qualityProfiles() const { return profiles(service_.qualityProfiles()); }
QVariantList LidarrBinding::metadataProfiles() const { return profiles(service_.metadataProfiles()); }
bool LidarrBinding::connectServer(const QString& address, const QString& api_key) {
    return interactionAllowed() && service_.connectServer(address, api_key);
}
void LidarrBinding::disconnectServer() {
    if (interactionAllowed())
        service_.disconnectServer();
}
bool LidarrBinding::search(const QString& term, const QString& kind) {
    if (!interactionAllowed() || (kind != QStringLiteral("album") && kind != QStringLiteral("artist")))
        return false;
    return service_.search(term, kind == QStringLiteral("album") ? QtLidarrService::Kind::Album
                                                                 : QtLidarrService::Kind::Artist);
}
bool LidarrBinding::requestDownload(const QString& key, const QString& root, const int quality_id,
                                    const int metadata_id) {
    return interactionAllowed() && service_.requestAlbum(key, root, quality_id, metadata_id);
}
void LidarrBinding::cancel() {
    if (interactionAllowed())
        service_.cancel();
}
bool LidarrBinding::prepareExit() {
    if (!exit_preparing_) {
        exit_preparing_ = true;
        service_.cancel();
        emit changed();
        return false;
    }
    return service_.drained();
}
void LidarrBinding::resume() {
    if (exit_preparing_ && !shutting_down_) {
        exit_preparing_ = false;
        emit changed();
    }
}
void LidarrBinding::beginShutdown() {
    shutting_down_ = true;
    service_.beginShutdown();
    emit changed();
}
} // namespace music_player::m0
