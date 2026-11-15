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
QJsonObject libraryAlbum(int existing_id = 23, int existing_artist = 31) {
    auto result = album(existing_id);
    result.remove("artist");
    result.insert("artistId", existing_artist);
    return result;
}
QJsonObject track(int existing_album = 23) {
    return {{"id", 51},
            {"albumId", existing_album},
            {"title", "Track 日本"},
            {"trackNumber", "A1"},
            {"mediumNumber", 2},
            {"hasFile", true}};
}
bool ready(Service& service, Network& network, const QString& base = QStringLiteral("http://localhost:8686/"),
           const QString& key = api_key) {
    if (!service.connectServer(base, key) || network.requests.isEmpty())
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
QString lookupArtist(Service& service, Network& network, int existing_id = 31) {
    if (!service.search("Artist", Service::Kind::Artist))
        return {};
    const QJsonObject artist{{"id", existing_id}, {"artistName", "Artist 日本"}, {"foreignArtistId", artist_id}};
    network.replies.back()->finish(200, json(QJsonArray{artist}));
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
            QCOMPARE(entry.request.attribute(QNetworkRequest::Http2AllowedAttribute), QVariant(false));
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
    void lookupPreservesLiteralPlusAndPercentSequences() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto term = QStringLiteral("A+B %2B & 日本");
        QVERIFY(service.search(term, Service::Kind::Album));
        const auto url = network.requests.back().request.url();
        QCOMPARE(url.query(QUrl::FullyEncoded), QStringLiteral("term=A%2BB%20%252B%20%26%20%E6%97%A5%E6%9C%AC"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("term"), QUrl::FullyDecoded), term);
    }
    void registeredArtistListsAlbumsAndKeepsExactAlbumConfirmation() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto catalog_key = lookupArtist(service, network, 0);
        QVERIFY(!catalog_key.isEmpty());
        QVERIFY(!service.listArtistAlbums(catalog_key));
        const auto artist_key = lookupArtist(service, network);
        QVERIFY(!artist_key.isEmpty());
        QVERIFY(service.listArtistAlbums(artist_key));
        const auto request = network.requests.back();
        QCOMPARE(request.operation, QNetworkAccessManager::GetOperation);
        QCOMPARE(request.request.url().path(), "/api/v1/album");
        QCOMPARE(request.request.url().query(), "artistId=31");
        QVERIFY(request.body.isEmpty());
        network.replies.back()->finish(200, json(QJsonArray{libraryAlbum()}));
        QCOMPARE(service.status(), Service::Status::Ready);
        QCOMPARE(service.results().size(), 1);
        const auto item = service.results().front();
        QCOMPARE(item.kind, Service::Kind::Album);
        QCOMPARE(item.title, "Album & 日本");
        QCOMPARE(item.artist, "Artist 日本");
        QCOMPARE(item.year, "2026");
        QCOMPARE(item.existing_id, 23);
        QVERIFY(item.key != artist_key);
        QVERIFY(!service.listArtistAlbums(artist_key));
        QVERIFY(service.requestAlbum(item.key, {}, 0, 0));
        QCOMPARE(network.requests.back().operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/command");
        const QJsonObject expected{{"name", "AlbumSearch"}, {"albumIds", QJsonArray{23}}};
        QCOMPARE(QJsonDocument::fromJson(network.requests.back().body).object(), expected);
    }
    void registeredAlbumTracksAreBoundToSelectionAndKeepTheAlbumResults() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto catalog_key = lookup(service, network);
        QVERIFY(!catalog_key.isEmpty());
        QVERIFY(!service.listAlbumTracks(catalog_key));
        const auto album_key = lookup(service, network, 23);
        QVERIFY(!album_key.isEmpty());
        const auto previous_results = service.results();
        QVERIFY(service.listAlbumTracks(album_key));
        const auto request = network.requests.back();
        QCOMPARE(request.operation, QNetworkAccessManager::GetOperation);
        QCOMPARE(request.request.url().path(), "/api/v1/track");
        QCOMPARE(request.request.url().query(), "albumId=23");
        QVERIFY(request.body.isEmpty());
        QCOMPARE(service.tracksKey(), album_key);
        QVERIFY(!service.tracksReady());
        network.replies.back()->finish(200, json(QJsonArray{track()}));
        QCOMPARE(service.status(), Service::Status::Ready);
        QVERIFY(service.tracksReady());
        QCOMPARE(service.tracksKey(), album_key);
        QCOMPARE(service.results(), previous_results);
        const QVector<Service::TrackItem> expected{{"Track 日本", "A1", 2, true}};
        QCOMPARE(service.tracks(), expected);
        QVERIFY(service.search("Other", Service::Kind::Album));
        QVERIFY(service.tracksKey().isEmpty());
        QVERIFY(!service.tracksReady());
        QVERIFY(service.tracks().isEmpty());
        network.replies.back()->finish(200, json(QJsonArray{album(23)}));
        QVERIFY(!service.listAlbumTracks(album_key));
        QVERIFY(service.listAlbumTracks(service.results().front().key));
        network.replies.back()->finish(200, "[]");
        QVERIFY(service.tracksReady());
        QVERIFY(service.tracks().isEmpty());
        service.disconnectServer();
        QVERIFY(!service.tracksReady());
        QVERIFY(service.tracksKey().isEmpty());
    }
    void libraryResponseMustMatchTheSelectedArtistOrAlbum() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QVERIFY(service.listArtistAlbums(lookupArtist(service, network)));
        network.replies.back()->finish(200, json(QJsonArray{libraryAlbum(), libraryAlbum(24, 99)}));
        QCOMPARE(service.error(), Service::Error::Response);
        QVERIFY(service.results().isEmpty());

        const auto album_key = lookup(service, network, 23);
        const auto previous_results = service.results();
        QVERIFY(service.listAlbumTracks(album_key));
        network.replies.back()->finish(200, json(QJsonArray{track(), track(99)}));
        QCOMPARE(service.error(), Service::Error::Response);
        QVERIFY(!service.tracksReady());
        QVERIFY(service.tracksKey().isEmpty());
        QVERIFY(service.tracks().isEmpty());
        QCOMPARE(service.results(), previous_results);

        QVERIFY(service.listAlbumTracks(album_key));
        QJsonArray too_many;
        for (qsizetype index = 0; index <= Service::maximum_results; ++index)
            too_many.append(track());
        network.replies.back()->finish(200, json(too_many));
        QCOMPARE(service.error(), Service::Error::Limit);
        QVERIFY(!service.tracksReady());
        QVERIFY(service.tracks().isEmpty());
        QCOMPARE(service.results(), previous_results);
    }
    void emptyLookupIsDistinctFromIdleFailureAndReconnect() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        QCOMPARE(service.status(), Service::Status::Ready);
        QVERIFY(service.search("No matching album", Service::Kind::Album));
        QCOMPARE(service.status(), Service::Status::Searching);
        network.replies.back()->finish(200, "[]");
        QCOMPARE(service.status(), Service::Status::NoResults);
        QCOMPARE(service.error(), Service::Error::None);
        QVERIFY(service.results().isEmpty());
        QVERIFY(service.connected());
        QVERIFY(!service.busy());
        QVERIFY(service.drained());

        QVERIFY(!lookup(service, network, 23).isEmpty());
        QCOMPARE(service.status(), Service::Status::Ready);
        QVERIFY(service.search("Unavailable artist", Service::Kind::Artist));
        network.replies.back()->finish(503, "[]");
        QCOMPARE(service.status(), Service::Status::Error);
        QCOMPARE(service.error(), Service::Error::Http);
        QVERIFY(service.connected());
        QVERIFY(service.search("No matching artist", Service::Kind::Artist));
        network.replies.back()->finish(200, "[]");
        QCOMPARE(service.status(), Service::Status::NoResults);
        QCOMPARE(service.error(), Service::Error::None);

        service.disconnectServer();
        QCOMPARE(service.status(), Service::Status::Disconnected);
        QVERIFY(!service.connected());
        QVERIFY(ready(service, network, "https://music.example.test/lidarr/"));
        QCOMPARE(service.status(), Service::Status::Ready);
        QVERIFY(service.results().isEmpty());
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
        QCOMPARE(network.requests.back().request.attribute(QNetworkRequest::Http2AllowedAttribute), QVariant(false));
        QCOMPARE(network.requests.back().request.url().path(), "/api/v1/command");
        const auto command = QJsonDocument::fromJson(network.requests.back().body).object();
        QCOMPARE(command.value("name").toString(), "AlbumSearch");
        QCOMPARE(command.value("albumIds").toArray(), QJsonArray{23});
        QCOMPARE(command.size(), 2);
        QVERIFY(!service.requestAlbum(key, {}, 0, 0));
        network.replies.back()->finish(201, "{\"id\":91,\"name\":\"AlbumSearch\",\"status\":\"queued\"}");
        QCOMPARE(service.status(), Service::Status::Requested);
        QVERIFY(service.results().isEmpty());
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
    void checksTheRequestedAlbumsQueueWithoutRepeatingTheMutation_data() {
        QTest::addColumn<bool>("already_registered");
        QTest::newRow("existing album") << true;
        QTest::newRow("added album") << false;
    }
    void checksTheRequestedAlbumsQueueWithoutRepeatingTheMutation() {
        QFETCH(bool, already_registered);
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network, "https://music.example.test/lidarr/"));
        const auto key = lookup(service, network, already_registered ? 23 : 0);
        QVERIFY(!key.isEmpty());
        QVERIFY(!service.checkAlbumActivity());
        QVERIFY(service.requestAlbum(key, "D:/Music", 2, 4));
        QVERIFY(!service.checkAlbumActivity());
        network.replies.back()->finish(201, already_registered ? QByteArray{"{\"id\":91}"} : json(album(23)));
        QCOMPARE(service.status(), Service::Status::Requested);
        QCOMPARE(network.requests.size(), 6);
        QCOMPARE(service.requestedAlbumTitle(), "Album & 日本");
        QVERIFY(service.checkAlbumActivity());
        QCOMPARE(service.status(), Service::Status::CheckingActivity);
        QVERIFY(service.busy());
        const auto request = network.requests.back();
        QCOMPARE(request.operation, QNetworkAccessManager::GetOperation);
        QCOMPARE(request.request.url().path(), "/lidarr/api/v1/queue/details");
        const QUrlQuery query(request.request.url());
        QCOMPARE(query.queryItemValue("albumIds"), "23");
        QCOMPARE(query.queryItemValue("includeAlbum"), "false");
        QCOMPARE(request.request.rawHeader("X-Api-Key"), api_key.toUtf8());
        QVERIFY(request.body.isEmpty());
        network.replies.back()->finish(200,
            R"([{"albumId":23,"status":"downloading","size":400,"sizeleft":232,"outputPath":"C:/private"},)"
            R"({"albumId":23,"status":"completed","size":20,"sizeleft":0},)"
            R"({"albumId":23,"status":"pending","size":0,"sizeleft":0}])");
        QCOMPARE(service.status(), Service::Status::AlbumActivity);
        const QVector<Service::Transfer> transfers{{Service::TransferStatus::Downloading, 42},
                                                   {Service::TransferStatus::TransferComplete, 100},
                                                   {Service::TransferStatus::Other, -1}};
        QCOMPARE(service.albumTransfers(), transfers);
        QVERIFY(service.checkAlbumActivity());
        network.replies.back()->finish(200, "[]");
        QCOMPARE(service.status(), Service::Status::AlbumActivity);
        QVERIFY(service.albumTransfers().isEmpty());
        QCOMPARE(network.requests.size(), 8);
        QVERIFY(!service.requestAlbum(key, "D:/Music", 2, 4));
    }
    void rejectsAnotherAlbumsActivityAndRevokesAccessAfterAuthenticationFailure() {
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network));
        const auto key = lookup(service, network, 23);
        QVERIFY(service.requestAlbum(key, {}, 0, 0));
        network.replies.back()->finish(201, "{\"id\":91}");
        QVERIFY(service.checkAlbumActivity());
        network.replies.back()->finish(200, R"([{"albumId":24,"status":"downloading"}])");
        QCOMPARE(service.error(), Service::Error::Response);
        QVERIFY(service.albumTransfers().isEmpty());
        QVERIFY(service.checkAlbumActivity());
        network.replies.back()->finish(401, "{}");
        QCOMPARE(service.error(), Service::Error::Authentication);
        QVERIFY(service.requestedAlbumTitle().isEmpty());
        QVERIFY(!service.checkAlbumActivity());
        QVERIFY(ready(service, network));
        QVERIFY(!service.checkAlbumActivity());
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
    void authenticationFailureRequiresExplicitReconnect_data() {
        QTest::addColumn<int>("http_status");
        QTest::addColumn<bool>("during_album_request");
        QTest::addColumn<bool>("oversized_body");
        QTest::newRow("lookup-unauthorized") << 401 << false << false;
        QTest::newRow("lookup-forbidden") << 403 << false << false;
        QTest::newRow("album-request-unauthorized") << 401 << true << false;
        QTest::newRow("album-request-forbidden") << 403 << true << false;
        QTest::newRow("lookup-forbidden-oversized-body") << 403 << false << true;
    }
    void authenticationFailureRequiresExplicitReconnect() {
        QFETCH(int, http_status);
        QFETCH(bool, during_album_request);
        QFETCH(bool, oversized_body);
        Network network;
        Service service(&network);
        QVERIFY(ready(service, network, "http://localhost:8686/lidarr/"));
        const auto address = service.serverAddress();

        QVERIFY(service.search("Album", Service::Kind::Album));
        auto second_album = album(27);
        second_album.insert("title", "Another album");
        second_album.insert("foreignAlbumId", "f2a1ec0e-6592-4d00-a5a7-1be679cd7ad5");
        network.replies.back()->finish(200, json(QJsonArray{album(23), second_album}));
        QCOMPARE(service.results().size(), 2);
        const auto stale_key = service.results().back().key;
        if (during_album_request) {
            QVERIFY(service.requestAlbum(service.results().front().key, {}, 0, 0));
            QCOMPARE(service.results().size(), 1);
        } else {
            QVERIFY(service.search("Another album", Service::Kind::Album));
        }
        const auto calls = network.requests.size();
        const auto response =
            oversized_body ? QByteArray(Service::maximum_response_bytes + 1, 'x') : QByteArray("{}\n");
        network.replies.back()->finish(http_status, response);

        QCOMPARE(service.status(), Service::Status::Error);
        QCOMPARE(service.error(), Service::Error::Authentication);
        QVERIFY(!service.connected());
        QVERIFY(!service.busy());
        QVERIFY(service.drained());
        QCOMPARE(service.serverAddress(), address);
        QVERIFY(service.results().isEmpty());
        QVERIFY(service.rootFolders().isEmpty());
        QVERIFY(service.qualityProfiles().isEmpty());
        QVERIFY(service.metadataProfiles().isEmpty());
        QVERIFY(!service.search("Album", Service::Kind::Album));
        QVERIFY(!service.requestAlbum(stale_key, {}, 0, 0));
        QCOMPARE(network.requests.size(), calls);

        const auto new_key = QStringLiteral("abcdef0123456789abcdef0123456789");
        QVERIFY(ready(service, network, address, new_key));
        QCOMPARE(service.status(), Service::Status::Ready);
        QCOMPARE(service.error(), Service::Error::None);
        QVERIFY(!lookup(service, network, 27).isEmpty());
        QVERIFY(!service.requestAlbum(stale_key, {}, 0, 0));
        QCOMPARE(network.requests.size(), calls + 5);
        for (qsizetype i = calls; i < network.requests.size(); ++i) {
            QCOMPARE(network.requests[i].request.rawHeader("X-Api-Key"), new_key.toUtf8());
        }
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
