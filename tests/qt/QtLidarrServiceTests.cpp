#include "adapters/QtLidarrService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTest>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>
#include <memory>

using Service = music_player::m0::QtLidarrService;
namespace {
const QString artist_id = QStringLiteral("38c17704-fd5c-4f86-af9b-30eef68a1fa9");
const QString album_id = QStringLiteral("f2a1ec0e-6592-4d00-a5a7-1be679cd7ad4");
const QString api_key = QStringLiteral("0123456789abcdef0123456789abcdef");
class Reply final : public QNetworkReply {
  public:
    struct Observation final {
        bool aborted{};
    };
    Reply(const QNetworkRequest& request, QNetworkAccessManager::Operation operation, QObject* parent)
        : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    bool aborted{};
    std::shared_ptr<Observation> observation{std::make_shared<Observation>()};
    void abort() override {
        aborted = true;
        observation->aborted = true;
        if (isFinished())
            return;
        setError(OperationCanceledError, "private fake error");
        setFinished(true);
        emit finished();
    }
    void finish(int status, QByteArray body) {
        body_ = std::move(body);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }

  protected:
    qint64 readData(char* data, qint64 maximum) override {
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
    QList<Request> requests;
    QList<QPointer<Reply>> replies;

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* outgoing) override {
        requests.push_back({operation, request, outgoing ? outgoing->readAll() : QByteArray{}});
        auto* reply = new Reply(request, operation, this);
        replies.push_back(reply);
        return reply;
    }
};
QByteArray json(const QJsonValue& value) {
    return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                           : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}
QJsonObject album(int existing_id = 0) {
    return {{"id", existing_id},
            {"title", "Album & 日本"},
            {"foreignAlbumId", album_id},
            {"releaseDate", "2026-05-01T00:00:00Z"},
            {"artist", QJsonObject{{"artistName", "Artist 日本"}, {"foreignArtistId", artist_id}, {"id", 0}}}};
}
bool ready(Service& service, Network& network, const QString& base = QStringLiteral("http://localhost:8686/")) {
    if (!service.connectServer(base, api_key) || network.requests.isEmpty())
        return false;
    auto prefix = QUrl(base).path();
    if (prefix.endsWith('/'))
        prefix.chop(1);
    if (network.requests.back().request.url().path() != prefix + "/api/v1/system/status")
        return false;
    network.replies.back()->finish(200, "{\"version\":\"3.1.0\",\"appName\":\"Lidarr\"}");
    if (network.requests.back().request.url().path().section('/', -1) != "rootfolder")
        return false;
    network.replies.back()->finish(200, "[{\"path\":\"D:/Music\"}]");
    if (network.requests.back().request.url().path().section('/', -1) != "qualityprofile")
        return false;
    network.replies.back()->finish(200, "[{\"id\":2,\"name\":\"Lossless\"}]");
    if (network.requests.back().request.url().path().section('/', -1) != "metadataprofile")
        return false;
    network.replies.back()->finish(200, "[{\"id\":4,\"name\":\"Standard\"}]");
    return service.connected() && !service.busy();
}
QString lookup(Service& service, Network& network, int existing_id = 0) {
    if (!service.search("Album", Service::Kind::Album))
        return {};
    network.replies.back()->finish(200, json(QJsonArray{album(existing_id)}));
    return service.results().isEmpty() ? QString{} : service.results().front().key;
}
} // namespace

class QtLidarrServiceTests final : public QObject {
    Q_OBJECT
  private slots:
    void disconnectedByDefaultWithoutNetworkOrImplicitSearch() {
        Network network;
        Service service(&network);
        QVERIFY(!service.connected());
        QVERIFY(!service.busy());
        QVERIFY(service.drained());
        QCOMPARE(service.status(), Service::Status::Disconnected);
        QVERIFY(!service.search("Album", Service::Kind::Album));
        QVERIFY(network.requests.isEmpty());
    }
    void explicitConnectionLoadsOnlyStatusAndConfiguredChoices() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QCOMPARE(network.requests.size(), 4);
        QCOMPARE(service.rootFolders(), QStringList{"D:/Music"});
        const QVector<Service::Profile> expected_quality{{2, "Lossless"}}, expected_metadata{{4, "Standard"}};
        QCOMPARE(service.qualityProfiles(), expected_quality);
        QCOMPARE(service.metadataProfiles(), expected_metadata);
        QVERIFY(service.results().isEmpty());
        QCOMPARE(service.status(), Service::Status::Ready);
    }
    void apiKeyOnlyTravelsInHeaderAndBasePathIsPreserved() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network, "https://music.example.test/lidarr/"));
        for (const auto& entry : network.requests) {
            QCOMPARE(entry.operation, QNetworkAccessManager::GetOperation);
            QCOMPARE(entry.request.rawHeader("X-Api-Key"), api_key.toUtf8());
            QVERIFY(entry.request.url().toString().startsWith("https://music.example.test/lidarr/api/v1/"));
            QVERIFY(!entry.request.url().toString().contains(api_key));
            QVERIFY(entry.request.url().query().isEmpty());
            QCOMPARE(entry.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                     int(QNetworkRequest::ManualRedirectPolicy));
            QCOMPARE(entry.request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
                     int(QNetworkRequest::Manual));
            QCOMPARE(entry.request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(),
                     int(QNetworkRequest::Manual));
            QVERIFY(entry.body.isEmpty());
        }
    }
    void rejectsAmbiguousAddressesAndHeaderInjection_data() {
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("key");
        for (const auto& address : {"", "localhost:8686", "file:///tmp/lidarr", "ftp://host/", "http://user:pass@host/",
                                    "http://host/?apikey=secret", "http://host/#fragment", "http://host/../secret"})
            QTest::newRow(address) << QString(address) << api_key;
        QTest::newRow("missing-key") << QString("http://localhost:8686/") << QString{};
        QTest::newRow("header-injection") << QString("http://localhost:8686/") << QString("secret\r\nHost: attacker");
    }
    void rejectsAmbiguousAddressesAndHeaderInjection() {
        QFETCH(QString, address);
        QFETCH(QString, key);
        Network network;
        Service service(&network);
        QVERIFY(!service.connectServer(address, key));
        QCOMPARE(service.error(), Service::Error::Configuration);
        QVERIFY(network.requests.isEmpty());
    }
    void albumLookupEncodesTermAndPublishesOnlyTypedCurrentResults() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QVERIFY(service.search("Album & 日本", Service::Kind::Album));
        const auto request = network.requests.back().request;
        QCOMPARE(request.url().path(), "/api/v1/album/lookup");
        QCOMPARE(QUrlQuery(request.url()).queryItemValue("term"), "Album & 日本");
        QCOMPARE(network.requests.size(), 5);
        network.replies.back()->finish(200, json(QJsonArray{album()}));
        QCOMPARE(service.results().size(), 1);
        const auto result = service.results().front();
        QVERIFY(!result.key.isEmpty());
        QCOMPARE(result.kind, Service::Kind::Album);
        QCOMPARE(result.title, "Album & 日本");
        QCOMPARE(result.artist, "Artist 日本");
        QCOMPARE(result.year, "2026");
        QCOMPARE(result.existing_id, 0);
        QCOMPARE(network.requests.size(), 5); // Displaying results never adds or downloads.
    }
    void artistLookupDoesNotBecomeAnAlbumDownload() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QVERIFY(service.search("Artist", Service::Kind::Artist));
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/artist/lookup");
        network.replies.back()->finish(
            200,
            json(QJsonArray{QJsonObject{{"artistName", "Artist 日本"}, {"foreignArtistId", artist_id}, {"id", 12}}}));
        QCOMPARE(service.results().size(), 1);
        QCOMPARE(service.results().front().kind, Service::Kind::Artist);
        const auto calls = network.requests.size();
        QVERIFY(!service.requestAlbum(service.results().front().key, "D:/Music", 2, 4));
        QCOMPARE(network.requests.size(), calls);
    }
    void existingAlbumRequestsExactlyOneSearchCommand() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto key = lookup(service, network, 23);
        QVERIFY(!key.isEmpty());
        QVERIFY(service.requestAlbum(key, {}, 0, 0));
        QVERIFY(std::none_of(service.results().begin(), service.results().end(),
                             [&](const auto& result) { return result.key == key; }));
        QCOMPARE(network.requests.size(), 6);
        QCOMPARE(network.requests.back().operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/command");
        const auto command = QJsonDocument::fromJson(network.requests.back().body).object();
        QCOMPARE(command.value("name").toString(), "AlbumSearch");
        QCOMPARE(command.value("albumIds").toArray(), QJsonArray{23});
        QCOMPARE(command.size(), 2);
        QVERIFY(!service.requestAlbum(key, {}, 0, 0));
        network.replies.back()->finish(201, "{\"id\":91,\"name\":\"AlbumSearch\",\"status\":\"queued\"}");
        QCOMPARE(service.status(), Service::Status::Requested);
        QVERIFY(service.connected());
        QVERIFY(!service.requestAlbum(key, {}, 0, 0)); // Successful selection consumed; no duplicate command.
        QCOMPARE(network.requests.size(), 6);
    }
    void addingAlbumRequiresServerChoicesAndDoesNotForwardRemoteFields() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QVERIFY(service.search("Album", Service::Kind::Album));
        auto data = album();
        data.insert("unexpectedCallbackUrl", "https://attacker.test/");
        data.insert("path", "C:/Unapproved");
        auto artist = data.value("artist").toObject();
        artist.insert("path", "C:/UnapprovedArtist");
        artist.insert("monitored", true);
        data.insert("artist", artist);
        network.replies.back()->finish(200, json(QJsonArray{data}));
        const auto key = service.results().front().key;
        const auto calls = network.requests.size();
        QVERIFY(!service.requestAlbum(key, "C:/Other", 2, 4));
        QVERIFY(!service.requestAlbum(key, "D:/Music", 999, 4));
        QVERIFY(!service.requestAlbum(key, "D:/Music", 2, 999));
        QCOMPARE(network.requests.size(), calls);
        QVERIFY(service.requestAlbum(key, "D:/Music", 2, 4));
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/album");
        const auto sent = QJsonDocument::fromJson(network.requests.back().body).object();
        QCOMPARE(sent.value("foreignAlbumId").toString(), album_id);
        QVERIFY(!sent.contains("unexpectedCallbackUrl"));
        QVERIFY(!sent.contains("path"));
        const auto sent_artist = sent.value("artist").toObject();
        QCOMPARE(sent_artist.value("foreignArtistId").toString(), artist_id);
        QCOMPARE(sent_artist.value("rootFolderPath").toString(), "D:/Music");
        QCOMPARE(sent_artist.value("qualityProfileId").toInt(), 2);
        QCOMPARE(sent_artist.value("metadataProfileId").toInt(), 4);
        QVERIFY(!sent_artist.contains("path"));
        QVERIFY(!sent_artist.contains("addOptions"));
        QCOMPARE(sent_artist.value("monitorNewItems").toString(), "none");
        QCOMPARE(sent_artist.value("monitored").toBool(), true);
        QCOMPARE(sent.value("monitored").toBool(), true);
        QCOMPARE(sent.value("anyReleaseOk").toBool(), true);
        QCOMPARE(sent.value("addOptions").toObject().value("searchForNewAlbum").toBool(), true);
        network.replies.back()->finish(201, "{\"id\":24,\"foreignAlbumId\":\"f2a1ec0e-6592-4d00-a5a7-1be679cd7ad4\"}");
        QCOMPARE(service.status(), Service::Status::Requested);
        QCOMPARE(network.requests.size(), calls + 1); // Add option is the search; no raced second POST.
    }
    void replacementSearchRevokesOldSelectionAndReplies() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto old_key = lookup(service, network, 23);
        QVERIFY(service.search("New", Service::Kind::Album));
        const auto old_reply = network.replies.back();
        service.disconnectServer();
        QVERIFY(old_reply->aborted);
        old_reply->finish(200, json(QJsonArray{album(27)}));
        QVERIFY(service.results().isEmpty());
        QVERIFY(!service.connected());
        QVERIFY(service.serverAddress().isEmpty());
        QVERIFY(!service.requestAlbum(old_key, {}, 0, 0));
        QCOMPARE(service.status(), Service::Status::Disconnected);
    }
    void oldSelectionIsInvalidAsSoonAsAnotherLookupStarts() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto old_key = lookup(service, network, 23);
        QVERIFY(service.search("New", Service::Kind::Album));
        QVERIFY(service.results().isEmpty());
        network.replies.back()->finish(200, json(QJsonArray{album(23)}));
        QVERIFY(service.results().front().key != old_key);
        const auto calls = network.requests.size();
        QVERIFY(!service.requestAlbum(old_key, {}, 0, 0));
        QCOMPARE(network.requests.size(), calls);
    }
    void connectionAndLookupFailuresRemainTyped_data() {
        QTest::addColumn<int>("http_status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<Service::Error>("expected");
        QTest::newRow("unauthorized") << 401 << QByteArray("secret server diagnostic")
                                      << Service::Error::Authentication;
        QTest::newRow("forbidden") << 403 << QByteArray("secret server diagnostic") << Service::Error::Authentication;
        QTest::newRow("redirect") << 302 << QByteArray("{}") << Service::Error::Http;
        QTest::newRow("http-failure") << 503 << QByteArray("private server path") << Service::Error::Http;
        QTest::newRow("not-json") << 200 << QByteArray("not json") << Service::Error::Response;
        QTest::newRow("wrong-shape") << 200 << QByteArray("[]") << Service::Error::Response;
        QTest::newRow("over-limit") << 200 << QByteArray(Service::maximum_response_bytes + 1, 'x')
                                    << Service::Error::Limit;
    }
    void connectionAndLookupFailuresRemainTyped() {
        QFETCH(int, http_status);
        QFETCH(QByteArray, body);
        QFETCH(Service::Error, expected);
        Network network;
        Service service(&network);
        QVERIFY(service.connectServer("http://localhost:8686/", api_key));
        network.replies.back()->finish(http_status, body);
        QCOMPARE(service.status(), Service::Status::Error);
        QCOMPARE(service.error(), expected);
        QVERIFY(!service.connected());
        QVERIFY(!service.busy());
        QCOMPARE(network.requests.size(), 1);
    }
    void limitsRejectWholeLookupInsteadOfPublishingPartialChoices() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QVERIFY(service.search("Album", Service::Kind::Album));
        QJsonArray too_many;
        for (qsizetype i = 0; i <= Service::maximum_results; ++i)
            too_many.append(album());
        network.replies.back()->finish(200, json(too_many));
        QCOMPARE(service.error(), Service::Error::Limit);
        QVERIFY(service.results().isEmpty());
    }
    void malformedLookupCannotCreateAnActionableSelection() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QVERIFY(service.search("Album", Service::Kind::Album));
        auto invalid = album();
        invalid.insert("foreignAlbumId", "../../arbitrary");
        network.replies.back()->finish(200, json(QJsonArray{album(), invalid}));
        QCOMPARE(service.error(), Service::Error::Response);
        QVERIFY(service.results().isEmpty());
    }
    void timeoutAbortsWithoutRetryAndShutdownClosesAdmission() {
        Network network;
        Service service(&network, nullptr, 10);
        QVERIFY(service.connectServer("http://localhost:8686/", api_key));
        const auto reply = network.replies.back();
        const auto observation = reply->observation;
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 1000);
        QVERIFY(observation->aborted);
        QCOMPARE(service.error(), Service::Error::Timeout);
        QCOMPARE(network.requests.size(), 1);
        service.beginShutdown();
        QVERIFY(service.drained());
        QVERIFY(!service.connectServer("http://localhost:8686/", api_key));
        QVERIFY(!service.search("Album", Service::Kind::Album));
        QCOMPARE(network.requests.size(), 1);
    }
    void cancellingMutationDoesNotRetryOrClaimDownloadCompletion() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto key = lookup(service, network, 23);
        QVERIFY(service.requestAlbum(key, {}, 0, 0));
        const auto reply = network.replies.back();
        service.cancel();
        QVERIFY(reply->aborted);
        QVERIFY(!service.busy());
        QVERIFY(service.drained());
        QCOMPARE(service.error(), Service::Error::Cancelled);
        const auto calls = network.requests.size();
        QVERIFY(!service.requestAlbum(key, {}, 0, 0)); // Ambiguous POST is consumed, never retried automatically.
        reply->finish(201, "{\"id\":91}");
        QCOMPARE(service.error(), Service::Error::Cancelled);
        QCOMPARE(network.requests.size(), calls);
    }
    void reentrantDisconnectCannotLaunchNextConnectionStage() {
        Network network;
        Service service(&network);
        QVERIFY(service.connectServer("http://localhost:8686/", api_key));
        bool first = true;
        QObject::connect(&service, &Service::changed, &service, [&] {
            if (first) {
                first = false;
                service.disconnectServer();
            }
        });
        network.replies.back()->finish(200, "{\"version\":\"3.1.0\"}");
        QVERIFY(!service.connected());
        QCOMPARE(network.requests.size(), 1);
    }
};
QTEST_GUILESS_MAIN(QtLidarrServiceTests)
#include "QtLidarrServiceTests.moc"
