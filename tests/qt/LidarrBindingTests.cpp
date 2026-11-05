#include "adapters/QtLidarrService.hpp"
#include "presentation/LidarrBinding.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cstring>

using namespace music_player::m0;
namespace {
class Reply final : public QNetworkReply {
  public:
    Reply(const QNetworkRequest& request, const QNetworkAccessManager::Operation operation, QObject* parent)
        : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() override {
        if (!isFinished()) {
            setError(OperationCanceledError, "private diagnostic");
            setFinished(true);
            emit finished();
        }
    }
    void finish(const int status, QByteArray body) {
        body_ = std::move(body);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }

  protected:
    qint64 readData(char* data, const qint64 maximum) override {
        const auto count = std::min(maximum, qint64{body_.size() - offset_});
        if (count <= 0)
            return isFinished() ? -1 : 0;
        std::memcpy(data, body_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    QByteArray body_;
    qint64 offset_{};
};
class Network final : public QNetworkAccessManager {
  public:
    struct Request final {
        Operation operation;
        QNetworkRequest request;
        QByteArray body;
    };
    int calls{};
    QList<Request> requests;
    QList<QPointer<Reply>> replies;

  protected:
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoing) override {
        ++calls;
        requests.push_back({operation, request, outgoing ? outgoing->readAll() : QByteArray{}});
        auto* reply = new Reply(request, operation, this);
        replies.push_back(reply);
        return reply;
    }
};
} // namespace
class LidarrBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void startsOfflineWithoutNetworkAndExposesNoCredentialProperty() {
        Network network;
        QtLidarrService service(&network);
        LidarrBinding binding(service);
        QVERIFY(!binding.connected());
        QVERIFY(!binding.busy());
        QVERIFY(binding.interactionAllowed());
        QCOMPARE(binding.serverAddress(), "http://localhost:8686");
        QVERIFY(!binding.statusText().isEmpty());
        QVERIFY(binding.results().isEmpty());
        QCOMPARE(binding.metaObject()->indexOfProperty("apiKey"), -1);
        QCOMPARE(network.calls, 0);
    }
    void exitHasObservablePreparationAndBlocksEveryNewOperation() {
        Network network;
        QtLidarrService service(&network);
        LidarrBinding binding(service);
        QSignalSpy changed(&binding, &LidarrBinding::changed);
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.interactionAllowed());
        QVERIFY(binding.prepareExit());
        QVERIFY(!binding.connectServer("http://localhost:8686", "test-key"));
        QVERIFY(!binding.search("Album", "album"));
        QVERIFY(!binding.requestDownload("key", "C:/Music", 1, 1));
        QVERIFY(changed.count() > 0);
        QCOMPARE(network.calls, 0);
    }
    void cancelledExitRearmsButShutdownIsIrreversible() {
        Network network;
        QtLidarrService service(&network);
        LidarrBinding binding(service);
        QVERIFY(!binding.prepareExit());
        binding.resume();
        QVERIFY(binding.interactionAllowed());
        binding.beginShutdown();
        QVERIFY(!binding.interactionAllowed());
        binding.resume();
        QVERIFY(!binding.interactionAllowed());
        QVERIFY(!binding.connectServer("http://localhost:8686", "test-key"));
        QCOMPARE(network.calls, 0);
    }
    void unknownSearchKindIsRefusedBeforeNetwork() {
        Network network;
        QtLidarrService service(&network);
        LidarrBinding binding(service);
        QVERIFY(!binding.search("Album", "unexpected"));
        QCOMPARE(network.calls, 0);
    }
    void projectsCurrentAlbumAndConsumesExactDownloadSelection() {
        Network network;
        QtLidarrService service(&network);
        LidarrBinding binding(service);
        QSignalSpy changed(&binding, &LidarrBinding::changed);
        QVERIFY(binding.connectServer("http://localhost:8686/", "private-api-key"));
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/system/status");
        network.replies.back()->finish(200, "{\"appName\":\"Lidarr\",\"version\":\"3.1.0\"}");
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/rootfolder");
        network.replies.back()->finish(200, "[{\"path\":\"D:/Music\"}]");
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/qualityprofile");
        network.replies.back()->finish(200, "[{\"id\":2,\"name\":\"Lossless\"}]");
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/metadataprofile");
        network.replies.back()->finish(200, "[{\"id\":4,\"name\":\"Standard\"}]");
        QVERIFY(binding.connected());
        QVERIFY(!binding.busy());
        QCOMPARE(network.calls, 4);
        QCOMPARE(binding.rootFolders(), QStringList{"D:/Music"});
        const QVariantList expected_quality{QVariantMap{{"id", 2}, {"name", "Lossless"}}};
        const QVariantList expected_metadata{QVariantMap{{"id", 4}, {"name", "Standard"}}};
        QCOMPARE(binding.qualityProfiles(), expected_quality);
        QCOMPARE(binding.metadataProfiles(), expected_metadata);

        QVERIFY(binding.search("Album 日本", "album"));
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/album/lookup");
        network.replies.back()->finish(
            200,
            QJsonDocument(QJsonArray{QJsonObject{
                              {"id", 23},
                              {"foreignAlbumId", "f2a1ec0e-6592-4d00-a5a7-1be679cd7ad4"},
                              {"title", "Album 日本"},
                              {"releaseDate", "2026-05-01T00:00:00Z"},
                              {"artist", QJsonObject{{"artistName", "Artist 日本"},
                                                     {"foreignArtistId", "38c17704-fd5c-4f86-af9b-30eef68a1fa9"}}}}})
                .toJson(QJsonDocument::Compact));
        QCOMPARE(binding.results().size(), 1);
        const auto item = binding.results().front().toMap();
        QCOMPARE(item.value("kind").toString(), "album");
        QCOMPARE(item.value("title").toString(), "Album 日本");
        QCOMPARE(item.value("artist").toString(), "Artist 日本");
        QCOMPARE(item.value("year").toString(), "2026");
        QCOMPARE(item.value("existingId").toInt(), 23);
        const auto key = item.value("key").toString();
        QVERIFY(!key.isEmpty());
        QVERIFY(key != "f2a1ec0e-6592-4d00-a5a7-1be679cd7ad4");
        QVERIFY(!item.contains("foreignAlbumId"));
        QVERIFY(!item.contains("foreignArtistId"));
        QCOMPARE(network.calls, 5); // Projection itself cannot enqueue a download.
        QVERIFY(!binding.requestDownload("unrecognized-selection", {}, 0, 0));
        QCOMPARE(network.calls, 5);
        QVERIFY(binding.requestDownload(key, {}, 0, 0));
        QCOMPARE(network.calls, 6);
        QCOMPARE(network.requests.back().operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/command");
        const auto command = QJsonDocument::fromJson(network.requests.back().body).object();
        QCOMPARE(command.value("name").toString(), "AlbumSearch");
        QCOMPARE(command.value("albumIds").toArray(), QJsonArray{23});
        QCOMPARE(command.size(), 2);
        network.replies.back()->finish(201, "{\"id\":91,\"name\":\"AlbumSearch\",\"status\":\"queued\"}");
        QVERIFY(!binding.busy());
        QVERIFY(!binding.statusText().isEmpty());
        QVERIFY(!binding.requestDownload(key, {}, 0, 0));
        QCOMPARE(network.calls, 6);
        QVERIFY(changed.count() > 0);
    }
    void authenticationFailureNeverProjectsServerBodyOrCredentials() {
        Network network;
        QtLidarrService service(&network);
        LidarrBinding binding(service);
        const QString secret = QStringLiteral("private-api-key");
        const QString private_path = QStringLiteral("C:/Private/Library");
        QVERIFY(binding.connectServer("http://localhost:8686/", secret));
        network.replies.back()->finish(401, (QStringLiteral("<h1>") + secret + " " + private_path + "</h1>").toUtf8());
        QCOMPARE(service.error(), QtLidarrService::Error::Authentication);
        QVERIFY(!binding.connected());
        QVERIFY(!binding.busy());
        QVERIFY(binding.results().isEmpty());
        const auto message = binding.statusText();
        QVERIFY(message.contains("chave de API", Qt::CaseInsensitive));
        QVERIFY(!message.contains(secret));
        QVERIFY(!message.contains(private_path));
        QVERIFY(!message.contains("<h1>"));
        QCOMPARE(network.calls, 1);
    }
};
QTEST_GUILESS_MAIN(LidarrBindingTests)
#include "LidarrBindingTests.moc"
