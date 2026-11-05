#include "adapters/DownloadLyricsLookup.hpp"
#include "adapters/QtLyricsService.hpp"

#include <QDir>
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

using namespace music_player::m0;
namespace {
using State = DownloadLyricsLookup::State;
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
        if (!isFinished()) {
            setError(OperationCanceledError, "cancelled synthetic reply");
            setFinished(true);
            emit finished();
        }
    }
    void finish(const int status, QByteArray body, const QByteArray& retry_after = {}) {
        body_ = std::move(body);
        offset_ = 0;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!retry_after.isEmpty())
            setRawHeader("Retry-After", retry_after);
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }

  protected:
    qint64 readData(char* output, const qint64 maximum) override {
        const auto count = std::min(maximum, qint64{body_.size() - offset_});
        if (count <= 0)
            return isFinished() ? -1 : 0;
        std::memcpy(output, body_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    QByteArray body_;
    qint64 offset_{};
};
class Network final : public QNetworkAccessManager {
  public:
    QList<QNetworkRequest> requests;
    QList<QPointer<Reply>> replies;

  protected:
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request, QIODevice*) override {
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
PublishedLyricsRequest selected(const std::string& job = "published_1") {
    return {job,
            QDir::tempPath() + "/rc-lyrics-published/music 日本.mp3",
            QStringLiteral("歌の名前"),
            QStringLiteral("作者 日本語"),
            QStringLiteral("https://www.letras.mus.br/naruto/silhouette/"),
            QStringLiteral("Album"),
            123'000};
}
} // namespace

class DownloadLyricsLookupTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionIsPassiveAndOnlyExplicitPublishedRequestSearchesTheFallback() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        QCOMPARE(lookup.state(), State::Idle);
        QVERIFY(lookup.drained());
        QVERIFY(!lookup.result());
        QVERIFY(network.requests.isEmpty());
        const auto request = selected();
        QVERIFY(lookup.request(request));
        QCOMPARE(lookup.state(), State::Searching);
        QVERIFY(!lookup.result());
        QVERIFY(!lookup.drained());
        QVERIFY(lookup.message().contains("Letras", Qt::CaseInsensitive));
        QVERIFY(lookup.message().contains("indispon", Qt::CaseInsensitive));
        QVERIFY(lookup.message().contains("LRCLIB", Qt::CaseInsensitive));
        QCOMPARE(network.requests.size(), 1);
        const auto http = network.requests.front();
        QCOMPARE(http.url().scheme(), QStringLiteral("https"));
        QCOMPARE(http.url().host(), QStringLiteral("lrclib.net"));
        QCOMPARE(http.url().path(), QStringLiteral("/api/get"));
        const QUrlQuery query(http.url());
        QCOMPARE(query.queryItemValue("track_name"), request.title);
        QCOMPARE(query.queryItemValue("artist_name"), request.artist);
        QCOMPARE(query.queryItemValue("album_name"), request.album);
        QCOMPARE(query.queryItemValue("duration"), QStringLiteral("123"));
        QVERIFY(!http.url().toString().contains(".mp3"));
        QVERIFY(!http.url().toString().contains("letras.mus.br"));
        QVERIFY(!http.url().toString().contains(QString::fromStdString(request.job_id)));
        network.replies.back()->finish(200, payload("[00:01.00]日本語\n[00:02.00]次の行", "Nihongo\nTsugi no gyou"));
        QCOMPARE(lookup.state(), State::Ready);
        QVERIFY(lookup.result());
        const auto& result = *lookup.result();
        QCOMPARE(result.job_id, request.job_id);
        QCOMPARE(result.final_mp3, request.final_mp3);
        QVERIFY(result.generation > 0);
        QCOMPARE(result.source, QStringLiteral("LRCLIB"));
        QCOMPARE(result.candidates.size(), std::size_t{2});
        QCOMPARE(result.candidates.front().lines.front().timestamp_ms, std::int64_t{1000});
        QCOMPARE(result.candidates.front().lines.front().text, QStringLiteral("日本語").toStdString());
        QVERIFY(result.candidates.back().lines.empty());
        QCOMPARE(result.candidates.back().plain_text, std::string{"Nihongo\nTsugi no gyou"});
        QVERIFY(lookup.drained());
    }
    void missingMetadataCanBeCorrectedWithoutChangingTheAudioTarget_data() {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("artist");
        QTest::newRow("missing artist") << QStringLiteral("Song") << QString{};
        QTest::newRow("blank artist") << QStringLiteral("Song") << QStringLiteral("  \t ");
        QTest::newRow("missing title") << QString{} << QStringLiteral("Artist");
    }
    void missingMetadataCanBeCorrectedWithoutChangingTheAudioTarget() {
        QFETCH(QString, title);
        QFETCH(QString, artist);
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        auto request = selected();
        request.title = title;
        request.artist = artist;
        QVERIFY(lookup.request(request));
        QCOMPARE(lookup.state(), State::NeedsMetadata);
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!lookup.result());
        QVERIFY(lookup.retry("Corrected song", "Corrected artist"));
        QCOMPARE(lookup.state(), State::Searching);
        QCOMPARE(network.requests.size(), 1);
        network.replies.back()->finish(200, payload({}, "Original text"));
        QVERIFY(lookup.result());
        QCOMPARE(lookup.result()->job_id, request.job_id);
        QCOMPARE(lookup.result()->final_mp3, request.final_mp3);
        QCOMPARE(lookup.result()->candidates.size(), std::size_t{1});
    }
    void invalidTargetsAndUnboundedOrInvalidMetadataNeverSearch_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 11; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void invalidTargetsAndUnboundedOrInvalidMetadataNeverSearch() {
        QFETCH(int, scenario);
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        auto request = selected();
        switch (scenario) {
        case 0:
            request.job_id.clear();
            break;
        case 1:
            request.job_id = "https://attacker.invalid/";
            break;
        case 2:
            request.final_mp3 = "relative.mp3";
            break;
        case 3:
            request.final_mp3 = "https://attacker.invalid/music.mp3";
            break;
        case 4:
            request.final_mp3 = "//server/share/music.mp3";
            break;
        case 5:
            request.final_mp3 += ".wav";
            break;
        case 6:
            request.title = QString(513, 'a');
            break;
        case 7:
            request.artist = QString(513, 'a');
            break;
        case 8:
            request.artist = QStringLiteral("bad") + QChar(1) + QStringLiteral("artist");
            break;
        case 9:
            request.title = QString(QChar(0xd800));
            break;
        default:
            request.lyrics_page = "https://attacker.invalid/lyrics";
            break;
        }
        QVERIFY(!lookup.request(request));
        QCOMPARE(lookup.state(), State::Error);
        QVERIFY(!lookup.result());
        QVERIFY(network.requests.isEmpty());
        QVERIFY(lookup.drained());
    }
    void notFoundInstrumentalAndFailureAreIndependentTerminalResults_data() {
        QTest::addColumn<int>("http");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("expected");
        QTest::newRow("not found") << 404 << QByteArray("{}") << static_cast<int>(State::NotFound);
        QTest::newRow("instrumental") << 200 << payload({}, {}, true) << static_cast<int>(State::Instrumental);
        QTest::newRow("unavailable") << 503 << QByteArray("{}") << static_cast<int>(State::Error);
        QTest::newRow("invalid JSON") << 200 << QByteArray("not JSON") << static_cast<int>(State::Error);
    }
    void notFoundInstrumentalAndFailureAreIndependentTerminalResults() {
        QFETCH(int, http);
        QFETCH(QByteArray, body);
        QFETCH(int, expected);
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        QVERIFY(lookup.request(selected()));
        network.replies.back()->finish(http, body);
        QCOMPARE(lookup.state(), static_cast<State>(expected));
        QVERIFY(!lookup.result());
        QVERIFY(lookup.drained());
        QCOMPARE(network.requests.size(), 1);
    }
    void retryKeepsExactJobAndPathAndChangesOnlyTheLookupMetadata() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        const auto request = selected();
        QVERIFY(lookup.request(request));
        network.replies.back()->finish(404, "{}");
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(lookup.retry("Another spelling", "Corrected artist"));
        QCOMPARE(network.requests.size(), 2);
        const QUrlQuery query(network.requests.back().url());
        QCOMPARE(query.queryItemValue("track_name"), QStringLiteral("Another spelling"));
        QCOMPARE(query.queryItemValue("artist_name"), QStringLiteral("Corrected artist"));
        network.replies.back()->finish(200, payload({}, "Recovered lyrics"));
        QVERIFY(lookup.result());
        QCOMPARE(lookup.result()->job_id, request.job_id);
        QCOMPARE(lookup.result()->final_mp3, request.final_mp3);
        QCOMPARE(lookup.result()->candidates.front().plain_text, std::string{"Recovered lyrics"});
    }
    void changingRequestRevokesTheOldReplyAndDoesNotBypassTheProviderCooldown() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        QVERIFY(lookup.request(selected("first")));
        const auto old = network.replies.back();
        auto next = selected("second");
        next.final_mp3.replace("music", "second");
        QVERIFY(lookup.request(next));
        QVERIFY(old->aborted);
        QVERIFY(!lookup.result());
        QCOMPARE(network.requests.size(), 1);
        old->finish(200, payload({}, "Wrong request"));
        QVERIFY(!lookup.result());
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        // A timer waking only enables a later explicit retry.
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(lookup.retry(next.title, next.artist));
        network.replies.back()->finish(200, payload({}, "Second request"));
        QVERIFY(lookup.result());
        QCOMPARE(lookup.result()->job_id, next.job_id);
        QCOMPARE(lookup.result()->final_mp3, next.final_mp3);
        QCOMPARE(lookup.result()->candidates.front().plain_text, std::string{"Second request"});
    }
    void retryAfterIsRetainedAcrossClearAndAChangedPublishedTarget() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        QVERIFY(lookup.request(selected("first")));
        network.replies.back()->finish(429, "{}", "1");
        QCOMPARE(lookup.state(), State::Error);
        lookup.clear();
        QVERIFY(lookup.request(selected("second")));
        QVERIFY(!lookup.retry("Song", "Artist"));
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!lookup.result());
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 2000);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(lookup.retry("Song", "Artist"));
        network.replies.back()->finish(200, payload({}, "Result after cooldown"));
        QVERIFY(lookup.result());
        QCOMPARE(lookup.result()->job_id, std::string{"second"});
    }
    void clearAndExitRevokeResultsAndRequireExplicitResumeBeforeAnotherRequest() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        QVERIFY(lookup.request(selected()));
        const auto old = network.replies.back();
        QVERIFY(!lookup.prepareExit());
        QCOMPARE(lookup.state(), State::Cancelled);
        QVERIFY(old->aborted);
        QVERIFY(!lookup.result());
        QVERIFY(!lookup.request(selected("refused")));
        QVERIFY(!lookup.retry("Song", "Artist"));
        QVERIFY(lookup.prepareExit());
        lookup.resume();
        QCOMPARE(lookup.state(), State::Idle);
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(lookup.request(selected("resumed")));
        network.replies.back()->finish(200, payload({}, "New result"));
        QVERIFY(lookup.result());
        const auto generation = lookup.result()->generation;
        lookup.clear();
        QCOMPARE(lookup.state(), State::Cancelled);
        QVERIFY(!lookup.result());
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(lookup.request(selected("again")));
        network.replies.back()->finish(200, payload({}, "Again"));
        QVERIFY(lookup.result());
        QVERIFY(lookup.result()->generation > generation);
        lookup.beginShutdown();
        lookup.resume();
        QVERIFY(!lookup.request(selected("closed")));
        QVERIFY(!lookup.retry("Song", "Artist"));
        QVERIFY(!lookup.result());
        QVERIFY(lookup.drained());
    }
    void aReadyResultOwnsTheExactPublishedPathInsteadOfBorrowingRequestStorage() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        auto request = selected();
        auto path_storage = request.final_mp3;
        const auto expected = QString(path_storage.constData(), path_storage.size());
        request.final_mp3 = QString::fromRawData(path_storage.constData(), path_storage.size());
        QVERIFY(lookup.request(request));
        path_storage.fill('X');
        request.job_id = "mutated";
        network.replies.back()->finish(200, payload({}, "Owned result"));
        QVERIFY(lookup.result());
        QCOMPARE(lookup.result()->job_id, std::string{"published_1"});
        QCOMPARE(lookup.result()->final_mp3, expected);
    }
    void reentrantClearExitOrReplacementCannotResumeAnOldLookup_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("clear") << 0;
        QTest::newRow("exit") << 1;
        QTest::newRow("replacement") << 2;
    }
    void reentrantClearExitOrReplacementCannotResumeAnOldLookup() {
        QFETCH(int, scenario);
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        bool acted{};
        connect(&lookup, &DownloadLyricsLookup::changed, &lookup, [&] {
            if (acted || lookup.state() != State::Searching)
                return;
            acted = true;
            if (scenario == 0)
                lookup.clear();
            else if (scenario == 1)
                static_cast<void>(lookup.prepareExit());
            else {
                auto replacement = selected("replacement");
                replacement.artist.clear();
                QVERIFY(lookup.request(replacement));
            }
        });
        QVERIFY(!lookup.request(selected("obsolete")));
        QVERIFY(acted);
        QCOMPARE(lookup.state(), scenario == 2 ? State::NeedsMetadata : State::Cancelled);
        QVERIFY(!lookup.result());
        QVERIFY(network.requests.isEmpty());
    }
    void aReadyNotificationCanRevokeItsCandidatesWithoutLaterCooldownResurrection() {
        Network network;
        QtLyricsService service(&network);
        DownloadLyricsLookup lookup(service);
        connect(&lookup, &DownloadLyricsLookup::changed, &lookup, [&] {
            if (lookup.state() == State::Ready)
                lookup.clear();
        });
        QVERIFY(lookup.request(selected()));
        network.replies.back()->finish(200, payload({}, "Revoked by receiver"));
        QCOMPARE(lookup.state(), State::Cancelled);
        QVERIFY(!lookup.result());
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(!lookup.result());
        QCOMPARE(lookup.state(), State::Cancelled);
        QCOMPARE(network.requests.size(), 1);
    }
};
QTEST_GUILESS_MAIN(DownloadLyricsLookupTests)
#include "DownloadLyricsLookupTests.moc"
