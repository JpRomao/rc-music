#include "adapters/QtLyricsService.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

using music_player::m0::QtLyricsService;
using Service = QtLyricsService;
namespace {
class Reply final : public QNetworkReply {
  public:
    Reply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    bool aborted{};
    void abort() override {
        aborted = true;
        if (isFinished())
            return;
        setError(OperationCanceledError, "cancelled fake");
        setFinished(true);
        emit finished();
    }
    void finish(const int status, QByteArray body, const QByteArray& retry_after = {}) {
        body_ = std::move(body);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!retry_after.isEmpty())
            setRawHeader("Retry-After", retry_after);
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
    QList<QPointer<Reply>> replies;
    QList<QNetworkRequest> requests;

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice*) override {
        if (operation != GetOperation)
            return nullptr;
        requests.push_back(request);
        auto* reply = new Reply(request, this);
        replies.push_back(reply);
        return reply;
    }
};
QByteArray payload(const QString& synced, const QString& plain = {}, const bool instrumental = false) {
    return QJsonDocument(QJsonObject{{"syncedLyrics", synced}, {"plainLyrics", plain}, {"instrumental", instrumental}})
        .toJson(QJsonDocument::Compact);
}
void setTrack(Service& service, const QString& path = "C:/music/source.mp3") {
    service.setTrack(path, "Song & name", "Artist 日本", "Album", 123'000);
}
bool save(const QString& path, const QByteArray& text) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
void awaitLocal(Service& service) { QTRY_VERIFY_WITH_TIMEOUT(service.status() != Service::Status::LoadingLocal, 1500); }
} // namespace

class QtLyricsServiceTests final : public QObject {
    Q_OBJECT
  private slots:
    void lookupContextNeverStartsAReadOfTheSiblingLyrics() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto audio = temp.filePath("published 日本.mp3");
        QVERIFY(save(temp.filePath("published 日本.lrc"), "[00:01.00]Must not load"));
        Network network;
        Service service(&network);
        service.setLookupTrack(audio, "Lookup title", "Lookup artist", "Album", 123'000);
        QCOMPARE(service.status(), Service::Status::Idle);
        QCOMPARE(service.source(), Service::Source::None);
        QVERIFY(service.drained());
        QVERIFY(service.document().lines.empty());
        QVERIFY(service.onlineDocuments().empty());
        QVERIFY(network.requests.isEmpty());
        QVERIFY(service.canSearch());
        QVERIFY(service.searchOnline());
        const QUrlQuery query(network.requests.back().url());
        QCOMPARE(query.queryItemValue("track_name"), QStringLiteral("Lookup title"));
        QCOMPARE(query.queryItemValue("artist_name"), QStringLiteral("Lookup artist"));
        QVERIFY(!query.toString().contains(audio));
    }
    void switchingALocalContextToLookupForTheSamePathRevokesItsLoadedContent() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto audio = temp.filePath("published.mp3");
        QVERIFY(save(temp.filePath("published.lrc"), "[00:01.00]Local lyric"));
        Network network;
        Service service(&network);
        setTrack(service, audio);
        awaitLocal(service);
        QCOMPARE(service.status(), Service::Status::Ready);
        service.setLookupTrack(audio, "Other title", "Other artist", {}, 0);
        QCOMPARE(service.status(), Service::Status::Idle);
        QCOMPARE(service.source(), Service::Source::None);
        QVERIFY(service.document().lines.empty());
        QVERIFY(service.document().plain_text.empty());
        QVERIFY(service.drained());
        QVERIFY(network.requests.isEmpty());
    }
    void distinctOnlineContentsPreserveUnicodeAndExistingTimesWithoutGuessingForms() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload("[00:01.25]日本語\n[00:02.00]次の行", "Nihongo\nTsugi no gyou"));
        QCOMPARE(service.status(), Service::Status::Ready);
        const auto& documents = service.onlineDocuments();
        QCOMPARE(documents.size(), std::size_t{2});
        QCOMPARE(documents.front(), service.document());
        QCOMPARE(documents.front().lines.front().timestamp_ms, std::int64_t{1250});
        QCOMPARE(documents.front().lines.front().text, QStringLiteral("日本語").toStdString());
        QVERIFY(documents.back().lines.empty());
        QCOMPARE(documents.back().plain_text, std::string{"Nihongo\nTsugi no gyou"});
    }
    void identicalUntimedContentDoesNotDuplicateTheSynchronizedCandidate() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload("[00:01.00]日本語\n[00:02.00]次の行", "日本語\n次の行"));
        QCOMPARE(service.onlineDocuments().size(), std::size_t{1});
        QCOMPARE(service.onlineDocuments().front(), service.document());
        QVERIFY(!service.onlineDocuments().front().lines.empty());
    }
    void plainOnlineContentRemainsPlainEvenWhenItLooksLikeATimestamp() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload({}, "[00:12.00]Literal original line\r\n[Chorus]"));
        QCOMPARE(service.onlineDocuments().size(), std::size_t{1});
        QVERIFY(service.onlineDocuments().front().lines.empty());
        QCOMPARE(service.onlineDocuments().front().plain_text,
                 std::string{"[00:12.00]Literal original line\n[Chorus]"});
    }
    void startingAnotherSearchRevokesThePreviousCandidateBatchBeforeItsResponse() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload({}, "First content"));
        QCOMPARE(service.onlineDocuments().size(), std::size_t{1});
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(service.searchOnline("Another song", "Another artist"));
        QVERIFY(service.onlineDocuments().empty());
        network.replies.back()->finish(404, "{}");
        QVERIFY(service.onlineDocuments().empty());
    }
    void cancelLookupAndInstrumentalRevokeAllOnlineCandidates() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload("[00:01.00]First", "Other content"));
        QCOMPARE(service.onlineDocuments().size(), std::size_t{2});
        service.cancel();
        QVERIFY(service.onlineDocuments().empty());
        service.setLookupTrack("C:/published/other.mp3", "Other", "Artist", {}, 0);
        QVERIFY(!service.canSearch());
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload({}, {}, true));
        QCOMPARE(service.status(), Service::Status::Instrumental);
        QVERIFY(service.onlineDocuments().empty());
    }
    void metadataOnlyLookupRetainsRetryAfterAcrossTargetChanges() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(429, "{}", "1");
        service.setLookupTrack("C:/published/other.mp3", "Other", "Artist", {}, 0);
        QVERIFY(!service.canSearch());
        QVERIFY(!service.searchOnline());
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 2000);
        QVERIFY(service.searchOnline());
        QCOMPARE(network.requests.size(), 2);
        const QUrlQuery query(network.requests.back().url());
        QCOMPARE(query.queryItemValue("track_name"), QStringLiteral("Other"));
        QCOMPARE(query.queryItemValue("artist_name"), QStringLiteral("Artist"));
    }
    void malformedSecondaryContentDoesNotPublishAPartialCandidateBatch() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload("[00:01.00]Valid timed text", QStringLiteral("bad\x01line")));
        QCOMPARE(service.status(), Service::Status::Error);
        QVERIFY(service.onlineDocuments().empty());
    }
    void lookupPreservesLiteralPlusAndPercentSequencesInEveryTextField() {
        Network network;
        Service service(&network);
        const auto title = QStringLiteral("Song+A %2B");
        const auto artist = QStringLiteral("Artist+B %26");
        const auto album = QStringLiteral("日本+C %20");
        service.setLookupTrack("C:/published/song.mp3", title, artist, album, 120000);
        QVERIFY(service.searchOnline());
        const auto url = network.requests.back().url();
        QCOMPARE(url.query(QUrl::FullyEncoded),
                 QStringLiteral("track_name=Song%2BA%20%252B&artist_name=Artist%2BB%20%2526&"
                                "album_name=%E6%97%A5%E6%9C%AC%2BC%20%2520&duration=120"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("track_name"), QUrl::FullyDecoded), title);
        QCOMPARE(query.queryItemValue(QStringLiteral("artist_name"), QUrl::FullyDecoded), artist);
        QCOMPARE(query.queryItemValue(QStringLiteral("album_name"), QUrl::FullyDecoded), album);
    }
    void currentTrackNeverStartsANetworkRequestAndReadsSiblingLrc() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto audio = temp.filePath("song 日本.mp3");
        QVERIFY(save(temp.filePath("song 日本.lrc"), "[00:01.00]First\n[00:03.50]Second"));
        Network network;
        Service service(&network);
        setTrack(service, audio);
        QCOMPARE(service.status(), Service::Status::LoadingLocal);
        awaitLocal(service);
        QCOMPARE(service.status(), Service::Status::Ready);
        QCOMPARE(service.source(), Service::Source::Local);
        QCOMPARE(service.document().lines.size(), std::size_t{2});
        QCOMPARE(service.document().lines[0].timestamp_ms, std::int64_t{1000});
        QVERIFY(network.requests.isEmpty());
    }
    void explicitSearchUsesOnlyFixedHttpsEndpointAndEncodedMetadata() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        QCOMPARE(service.status(), Service::Status::Searching);
        QCOMPARE(network.requests.size(), 1);
        const auto request = network.requests.front();
        QCOMPARE(request.url().scheme(), "https");
        QCOMPARE(request.url().host(), "lrclib.net");
        QCOMPARE(request.url().path(), "/api/get");
        const QUrlQuery query(request.url());
        QCOMPARE(query.queryItemValue("track_name"), "Song & name");
        QCOMPARE(query.queryItemValue("artist_name"), "Artist 日本");
        QCOMPARE(query.queryItemValue("album_name"), "Album");
        QCOMPARE(query.queryItemValue("duration"), "123");
        QVERIFY(!request.url().toString().contains("source.mp3"));
        QCOMPARE(request.attribute(QNetworkRequest::Http2AllowedAttribute), QVariant(false));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::ManualRedirectPolicy));
        QVERIFY(request.rawHeader("User-Agent").startsWith("RCMusic/"));
        network.replies.back()->finish(200, payload("[00:00.00]Online line"));
        QCOMPARE(service.status(), Service::Status::Ready);
        QCOMPARE(service.source(), Service::Source::Online);
        QCOMPARE(service.document().lines.front().text, std::string{"Online line"});
    }
    void unsyncedAndInstrumentalResultsRemainDistinct() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload({}, "Plain original lyrics"));
        QCOMPARE(service.status(), Service::Status::Ready);
        QVERIFY(service.document().lines.empty());
        QCOMPARE(service.document().plain_text, std::string{"Plain original lyrics"});
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload({}, {}, true));
        QCOMPARE(service.status(), Service::Status::Instrumental);
        QVERIFY(service.document().plain_text.empty());
    }
    void switchingTracksRevokesOldRepliesAndLocalResults() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        const auto revision = service.contextRevision();
        const auto old = network.replies.back();
        service.setTrack("C:/music/other.mp3", "Other", "Second artist", {}, 10'000);
        QVERIFY(service.contextRevision() != revision);
        QVERIFY(old->aborted);
        old->finish(200, payload("[00:00.00]Wrong song"));
        awaitLocal(service);
        QVERIFY(service.document().lines.empty());
        QVERIFY(service.document().plain_text.empty());
    }
    void missingTagsCanBeOverriddenWithoutChangingTheFile() {
        Network network;
        Service service(&network);
        service.setTrack("C:/music/untagged.mp3", {}, {}, {}, 0);
        QVERIFY(service.canSearch()); // The UI can ask for missing title/artist.
        QVERIFY(!service.searchOnline());
        QVERIFY(service.searchOnline("Manual title", "Manual artist"));
        const QUrlQuery query(network.requests.back().url());
        QCOMPARE(query.queryItemValue("track_name"), "Manual title");
        QCOMPARE(query.queryItemValue("artist_name"), "Manual artist");
        QVERIFY(query.queryItemValue("duration").isEmpty());
    }
    void failuresAndResponseLimitsDoNotExposeOrPublishPartialLyrics_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("missing") << 404 << QByteArray{"{}"};
        QTest::newRow("redirect") << 302 << payload("[00:00.00]Redirected");
        QTest::newRow("malformed") << 200 << QByteArray{"not json"};
        QTest::newRow("oversized") << 200 << QByteArray(Service::maximum_response_bytes + 1, 'x');
    }
    void failuresAndResponseLimitsDoNotExposeOrPublishPartialLyrics() {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(status, body);
        QCOMPARE(service.status(), status == 404 ? Service::Status::NotFound : Service::Status::Error);
        QVERIFY(service.document().lines.empty());
        QVERIFY(service.document().plain_text.empty());
        QVERIFY(!service.message().contains("C:/"));
    }
    void timeoutAbortsTheOnlyRequestAndKeepsPlaybackIndependent() {
        Network network;
        Service service(&network, nullptr, "RCMusic/test", 20);
        setTrack(service);
        QVERIFY(service.searchOnline());
        const auto reply = network.replies.back();
        QTRY_COMPARE_WITH_TIMEOUT(service.status(), Service::Status::Error, 1000);
        QVERIFY(reply.isNull() || reply->aborted);
    }
    void shutdownRevokesRequestsAndRefusesFurtherTrackOrSearchWork() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        const auto reply = network.replies.back();
        service.beginShutdown();
        QVERIFY(reply.isNull() || reply->aborted);
        QVERIFY(!service.searchOnline("Other", "Artist"));
        service.setTrack("C:/music/never-read.mp3", "Other", "Artist", {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(service.stopped(), 1500);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(service.document().lines.empty());
    }
    void sameTrackMetadataUpdatesKeepHealthyLocalLyrics() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto audio = temp.filePath("song.mp3");
        QVERIFY(save(temp.filePath("song.lrc"), "[00:00.00]Local stays"));
        Network network;
        Service service(&network);
        setTrack(service, audio);
        QCOMPARE(service.status(), Service::Status::LoadingLocal);
        awaitLocal(service);
        QCOMPARE(service.status(), Service::Status::Ready);
        service.setTrack(audio, "Updated title", "Updated artist", {}, 10'000);
        QCOMPARE(service.status(), Service::Status::Ready);
        QCOMPARE(service.document().lines.front().text, std::string{"Local stays"});
        QVERIFY(network.requests.isEmpty());
    }
    void sameTrackMetadataUpdatesKeepTheConfirmedOnlineSearchAndRefreshLaterDefaults() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto audio = temp.filePath("song.mp3");
        Network network;
        Service service(&network);
        service.setTrack(audio, "Fallback title", {}, {}, 0);
        QVERIFY(service.searchOnline("Confirmed title", "Confirmed artist"));
        const auto revision = service.contextRevision();
        const auto reply = network.replies.front();
        const auto original_url = network.requests.front().url();

        service.setTrack(audio, "Tagged title", "Tagged artist", "Tagged album", 27'000);
        QCOMPARE(service.status(), Service::Status::Searching);
        QCOMPARE(service.contextRevision(), revision);
        QVERIFY(!reply->aborted);
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests.front().url(), original_url);
        const QUrlQuery original(original_url);
        QCOMPARE(original.queryItemValue("track_name"), "Confirmed title");
        QCOMPARE(original.queryItemValue("artist_name"), "Confirmed artist");
        QVERIFY(!original.hasQueryItem("album_name"));
        QVERIFY(!original.hasQueryItem("duration"));

        reply->finish(200, payload("[00:01.00]Original result", "Plain result"));
        QCOMPARE(service.status(), Service::Status::Ready);
        QCOMPARE(service.source(), Service::Source::Online);
        QCOMPARE(service.contextRevision(), revision);
        QCOMPARE(service.onlineDocuments().size(), std::size_t{2});
        QCOMPARE(service.document().lines.front().text, std::string{"Original result"});

        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(service.searchOnline());
        QVERIFY(service.contextRevision() != revision);
        QVERIFY(service.onlineDocuments().empty());
        QCOMPARE(network.requests.size(), 2);
        const QUrlQuery updated(network.requests.back().url());
        QCOMPARE(updated.queryItemValue("track_name"), "Tagged title");
        QCOMPARE(updated.queryItemValue("artist_name"), "Tagged artist");
        QCOMPARE(updated.queryItemValue("album_name"), "Tagged album");
        QCOMPARE(updated.queryItemValue("duration"), "27");
    }
    void onlyOneOnlineRequestCanRunAtATime() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        const auto first = network.replies.back();
        QVERIFY(!service.searchOnline("Another title", "Another artist"));
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!first->aborted);
        QCOMPARE(service.status(), Service::Status::Searching);
    }
    void completedSearchesAreThrottledBeforeAnotherExplicitSearch() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(200, payload("[00:00.00]First result"));
        QCOMPARE(service.status(), Service::Status::Ready);
        QVERIFY(!service.canSearch());
        QVERIFY(!service.searchOnline());
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(service.searchOnline());
        QCOMPARE(network.requests.size(), 2);
    }
    void retryAfterSurvivesTrackChangesAndBlocksQueriesUntilItsDeadline() {
        Network network;
        Service service(&network);
        setTrack(service);
        QVERIFY(service.searchOnline());
        network.replies.back()->finish(429, "{}", "1");
        QCOMPARE(service.status(), Service::Status::Error);
        service.setTrack("C:/music/different.mp3", "Different title", "Artist", {}, 10'000);
        QVERIFY(!service.canSearch());
        QVERIFY(!service.searchOnline());
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 2000);
        QVERIFY(service.searchOnline());
        QCOMPARE(network.requests.size(), 2);
    }
};
QTEST_GUILESS_MAIN(QtLyricsServiceTests)
#include "QtLyricsServiceTests.moc"
