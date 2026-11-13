#include "adapters/QtLyricsService.hpp"
#include "m0/ports/LyricsVariantsStore.hpp"
#include "presentation/DownloadLyricsBinding.hpp"
#include "presentation/LyricsBinding.hpp"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>

using namespace music_player::m0;
namespace {
class ExternalUrlCapture final : public QObject {
    Q_OBJECT
  public:
    ExternalUrlCapture() { QDesktopServices::setUrlHandler("https", this, "capture"); }
    ~ExternalUrlCapture() override { QDesktopServices::unsetUrlHandler("https"); }
    QList<QUrl> urls;
  public slots:
    void capture(const QUrl& url) { urls.push_back(url); }
};

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
            setError(OperationCanceledError, "synthetic cancellation");
            setFinished(true);
            emit finished();
        }
    }
    void finish(const int status, QByteArray body) {
        bytes_ = std::move(body);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override { return bytes_.size() - offset_ + QNetworkReply::bytesAvailable(); }

  protected:
    qint64 readData(char* output, const qint64 maximum) override {
        const auto count = std::min(maximum, qint64{bytes_.size() - offset_});
        if (count <= 0)
            return isFinished() ? -1 : 0;
        std::memcpy(output, bytes_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    QByteArray bytes_;
    qint64 offset_{};
};
class Network final : public QNetworkAccessManager {
  public:
    QList<QNetworkRequest> requests;
    QList<QPointer<Reply>> replies;

  protected:
    QNetworkReply* createRequest(const Operation operation, const QNetworkRequest& request, QIODevice*) override {
        if (operation != GetOperation && operation != PostOperation)
            return nullptr;
        requests.push_back(request);
        auto* reply = new Reply(request, this);
        replies.push_back(reply);
        return reply;
    }
};
QByteArray payload() {
    return QJsonDocument(QJsonObject{{"syncedLyrics", QStringLiteral("[00:01.00]日本語\n[00:02.00]次の行")},
                                     {"plainLyrics", "Nihongo\nTsugi no gyou"},
                                     {"instrumental", false}})
        .toJson(QJsonDocument::Compact);
}
std::filesystem::path nativePath(const QString& value) {
#ifdef Q_OS_WIN
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toUtf8().toStdString());
#endif
}
class Store final : public LyricsVariantsStore {
  public:
    std::atomic_int loads{}, saves{};
    std::atomic_bool block_save{}, save_entered{};
    std::atomic<LyricsVariantsLoadStatus> load_status{LyricsVariantsLoadStatus::Missing};
    std::atomic<LyricsVariantsSaveStatus> save_status{LyricsVariantsSaveStatus::Saved};
    QSemaphore release_save;
    using Attempt = std::pair<std::filesystem::path, LyricsVariants>;
    LyricsVariantsLoadResult load(const std::filesystem::path& path) override {
        ++loads;
        if (load_status != LyricsVariantsLoadStatus::Missing)
            return {.status = load_status.load()};
        std::lock_guard lock(mutex_);
        const auto found = disk_.find(path);
        return found == disk_.end() ? LyricsVariantsLoadResult{}
                                    : LyricsVariantsLoadResult{found->second, LyricsVariantsLoadStatus::Loaded};
    }
    LyricsVariantsSaveStatus save(const std::filesystem::path& path, const LyricsVariants& variants) override {
        ++saves;
        if (block_save.exchange(false)) {
            save_entered = true;
            static_cast<void>(release_save.tryAcquire(1, 3000));
        }
        std::lock_guard lock(mutex_);
        attempts_.emplace_back(path, variants);
        const auto status = save_status.load();
        if (status == LyricsVariantsSaveStatus::Saved)
            disk_[path] = variants;
        return status;
    }
    LyricsVariants read(const QString& target) {
        std::lock_guard lock(mutex_);
        const auto found = disk_.find(nativePath(target));
        return found == disk_.end() ? LyricsVariants{} : found->second;
    }
    std::vector<Attempt> attempts() {
        std::lock_guard lock(mutex_);
        return attempts_;
    }

  private:
    std::mutex mutex_;
    std::map<std::filesystem::path, LyricsVariants> disk_;
    std::vector<Attempt> attempts_;
};
LyricsVariants foreignDraft() {
    return {{0, "Foreign", "", "", "manual", "Other explicit content", LyricsVariantKind::Original, false}};
}
bool savePasted(DownloadLyricsBinding& binding, const QString& token, const QString& content, const QString& name,
                const QString& language = {}, const QString& script = {}, const QString& kind = "original") {
    bool accepted{};
    return QMetaObject::invokeMethod(&binding, "savePasted", Qt::DirectConnection, Q_RETURN_ARG(bool, accepted),
                                     Q_ARG(QString, token), Q_ARG(QString, content), Q_ARG(QString, name),
                                     Q_ARG(QString, language), Q_ARG(QString, script), Q_ARG(QString, kind)) &&
           accepted;
}
bool discardPendingSave(DownloadLyricsBinding& binding, const QString& request_token, const QString& discard_token) {
    bool accepted{};
    return QMetaObject::invokeMethod(&binding, "discardPendingSave", Qt::DirectConnection, Q_RETURN_ARG(bool, accepted),
                                     Q_ARG(QString, request_token), Q_ARG(QString, discard_token)) &&
           accepted;
}
struct Harness final {
    QTemporaryDir directory;
    Network network, playback_network;
    QtLyricsService service{&network}, playback{&playback_network};
    DownloadLyricsLookup lookup{service};
    std::shared_ptr<Store> store{std::make_shared<Store>()};
    LyricsBinding writer{playback, store};
    DownloadLyricsBinding binding{lookup, writer};
    ~Harness() { store->release_save.release(); }
    PublishedLyricsRequest request(const std::string& id = "published_1") const {
        return {id,
                directory.filePath("downloaded 日本.mp3"),
                QStringLiteral("歌の名前"),
                QStringLiteral("作者 日本語"),
                QStringLiteral("https://www.letras.mus.br/naruto/silhouette/"),
                {},
                0};
    }
    bool ready() {
        if (!directory.isValid() || !binding.offerPublished(request()) || network.replies.isEmpty())
            return false;
        network.replies.back()->finish(200, payload());
        return binding.candidates().size() == 2 && binding.canSave();
    }
};
} // namespace

class DownloadLyricsBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void displaysLicensedPreviewAndRefusesToSaveProviderContent() {
        Harness h;
        h.service.configureLyricFind({"fixture-key", {}, "BR", true});
        QVERIFY(h.binding.offerPublished(h.request()));
        QCOMPARE(h.network.requests.size(), 1);
        QCOMPARE(h.network.requests.back().url().host(), QStringLiteral("api.lyricfind.com"));
        h.network.replies.back()->finish(200, R"({"response":{"code":101},"track":{
          "viewable":true,"copyright":"Fixture Publisher","writer":"Fixture Writer","lyrics":"Fixture verse"}})");
        QVERIFY(h.binding.lyricsCopyProtected());
        QVERIFY(h.binding.lyricsAttribution().contains("Fixture Writer"));
        QCOMPARE(h.binding.candidates().size(), 1);
        QVERIFY(!h.binding.canSave());
        QVERIFY(!h.binding.saveCandidate(0, "Original", {}, {}, "original"));
        QCOMPARE(h.store->saves.load(), 0);
        QVERIFY(h.binding.dismiss());
        QVERIFY(!h.binding.lyricsCopyProtected());
        QVERIFY(h.binding.lyricsAttribution().isEmpty());
        QVERIFY(h.binding.candidates().isEmpty());
    }

    void constructionIsPassiveAndPublicationAuthorityIsNotExposedToQml() {
        Harness h;
        QVERIFY(h.directory.isValid());
        QVERIFY(!h.binding.hasRequest());
        QVERIFY(!h.binding.busy());
        QVERIFY(!h.binding.canSave());
        QVERIFY(!h.binding.canRetry());
        QVERIFY(!h.binding.canRetrySave());
        QVERIFY(!h.binding.canDismiss());
        QVERIFY(h.binding.candidates().isEmpty());
        QVERIFY(h.binding.requestToken().isEmpty());
        QVERIFY(h.binding.drained());
        QCOMPARE(h.store->loads.load(), 0);
        QCOMPARE(h.store->saves.load(), 0);
        QVERIFY(h.network.requests.isEmpty());
        const auto* meta = h.binding.metaObject();
        for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
            const auto property = meta->property(index);
            QVERIFY(!property.isWritable());
            QVERIFY(QByteArray(property.name()) != "path");
            QVERIFY(QByteArray(property.name()) != "jobId");
        }
        for (int index = meta->methodOffset(); index < meta->methodCount(); ++index)
            QVERIFY(meta->method(index).name() != QByteArray("offerPublished"));
    }
    void explicitOfferProducesAllPreviewsAndNeverSavesAutomatically() {
        Harness h;
        const auto request = h.request();
        QVERIFY(h.binding.offerPublished(request));
        QVERIFY(!h.binding.requestToken().isEmpty());
        QVERIFY(h.binding.hasRequest());
        QVERIFY(h.binding.busy());
        QVERIFY(!h.binding.canSave());
        QVERIFY(h.binding.canDismiss());
        QCOMPARE(h.binding.title(), request.title);
        QCOMPARE(h.binding.artist(), request.artist);
        QVERIFY(h.binding.statusText().contains("Letras", Qt::CaseInsensitive));
        QVERIFY(h.binding.statusText().contains("LRCLIB", Qt::CaseInsensitive));
        QCOMPARE(h.network.requests.size(), 1);
        QCOMPARE(h.network.requests.front().url().host(), QStringLiteral("lrclib.net"));
        h.network.replies.back()->finish(200, payload());
        QVERIFY(!h.binding.busy());
        QVERIFY(h.binding.canSave());
        const auto rows = h.binding.candidates();
        QCOMPARE(rows.size(), 2);
        for (int index = 0; index < rows.size(); ++index) {
            const auto row = rows[index].toMap();
            QCOMPARE(row.value("index").toInt(), index);
            QVERIFY(!row.value("label").toString().isEmpty());
            QVERIFY(!row.value("text").toString().isEmpty());
            QVERIFY(!row.contains("path"));
            QVERIFY(!row.contains("jobId"));
            QVERIFY(!row.contains("language"));
            QVERIFY(!row.contains("script"));
        }
        QVERIFY(rows[0].toMap().value("synced").toBool());
        QVERIFY(!rows[1].toMap().value("synced").toBool());
        QCOMPARE(rows[0].toMap().value("text").toString(), QStringLiteral("日本語\n次の行"));
        QCOMPARE(rows[1].toMap().value("text").toString(), QStringLiteral("Nihongo\nTsugi no gyou"));
        QCOMPARE(h.store->loads.load(), 0);
        QCOMPARE(h.store->saves.load(), 0);
        QVERIFY(h.playback_network.requests.isEmpty());
    }
    void missingMetadataCanBeCorrectedByAnExplicitRetry() {
        Harness h;
        auto request = h.request();
        request.artist.clear();
        QVERIFY(h.binding.offerPublished(request));
        const auto token = h.binding.requestToken();
        QVERIFY(!token.isEmpty());
        QVERIFY(h.binding.hasRequest());
        QVERIFY(h.binding.canRetry());
        QVERIFY(!h.binding.canSave());
        QVERIFY(h.network.requests.isEmpty());
        QVERIFY(h.binding.retry("Corrected title", "Corrected artist"));
        QCOMPARE(h.binding.requestToken(), token);
        QCOMPARE(h.binding.title(), QStringLiteral("Corrected title"));
        QCOMPARE(h.binding.artist(), QStringLiteral("Corrected artist"));
        QCOMPARE(h.network.requests.size(), 1);
        h.network.replies.back()->finish(200, payload());
        QVERIFY(h.binding.canSave());
        QCOMPARE(h.store->saves.load(), 0);
    }
    void anExistingSearchingOrReadyPreviewCannotBeReplacedBeforeDismiss() {
        Harness h;
        const auto first = h.request();
        auto second = h.request("published_2");
        second.title = "Second";
        QVERIFY(h.binding.offerPublished(first));
        const auto token = h.binding.requestToken();
        QVERIFY(!token.isEmpty());
        QVERIFY(!h.binding.offerPublished(second));
        QCOMPARE(h.binding.requestToken(), token);
        QVERIFY(!h.network.replies.back()->aborted);
        QCOMPARE(h.network.requests.size(), 1);
        h.network.replies.back()->finish(200, payload());
        QVERIFY(!h.binding.offerPublished(second));
        QCOMPARE(h.binding.title(), first.title);
        QVERIFY(h.binding.dismiss());
        QVERIFY(!h.binding.hasRequest());
        QVERIFY(h.binding.candidates().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(h.service.canSearch(), 1500);
        QVERIFY(h.binding.offerPublished(second));
        QVERIFY(!h.binding.requestToken().isEmpty());
        QVERIFY(h.binding.requestToken() != token);
        QCOMPARE(h.binding.title(), second.title);
        QCOMPARE(h.network.requests.size(), 2);
    }
    void finishingAReviewReportsItsCapturedJobOnceAcrossReentrantOffers() {
        Harness h;
        QVERIFY(h.ready());
        QSignalSpy finished(&h.binding, &DownloadLyricsBinding::reviewFinished);
        QVERIFY(finished.isValid());
        const auto first = h.request();
        auto next = h.request("published_2");
        next.title = "Next review";
        bool observed_reset{}, offered{};
        connect(&h.binding, &DownloadLyricsBinding::changed, &h.binding, [&] {
            if (observed_reset || h.binding.hasRequest())
                return;
            observed_reset = true;
            QVERIFY(h.binding.requestToken().isEmpty());
            QVERIFY(h.binding.candidates().isEmpty());
            offered = h.binding.offerPublished(next);
        });
        QVERIFY(h.binding.dismiss());
        QVERIFY(observed_reset);
        QVERIFY(offered);
        QCOMPARE(h.binding.title(), next.title);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.front().front().toString(), QString::fromStdString(first.job_id));
        QVERIFY(h.binding.dismiss());
        QVERIFY(!h.binding.dismiss());
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.back().front().toString(), QString::fromStdString(next.job_id));
    }
    void explicitFormsAreSavedOneAtATimeWithoutChangingPlaybackOrLosingOtherCandidates() {
        Harness h;
        const auto current = h.directory.filePath("current.mp3");
        h.writer.setTrack(current, "Current song", "Current artist", {}, 0);
        QTRY_VERIFY(h.writer.variantsReady());
        QVERIFY(h.ready());
        const auto request = h.request();
        QVERIFY(h.binding.saveCandidate(0, "Original", "ja", "Jpan", "original"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        const auto first_ticket = h.writer.acquiredLyricsSave().ticket;
        QVERIFY(!QUuid::fromString(QString::fromStdString(first_ticket)).isNull());
        QCOMPARE(h.writer.acquiredLyricsSave().target, request.final_mp3);
        auto saved = h.store->read(request.final_mp3);
        QCOMPARE(saved.size(), std::size_t{1});
        QCOMPARE(saved.front().name, std::string{"Original"});
        QCOMPARE(saved.front().language, std::string{"ja"});
        QCOMPARE(saved.front().script, std::string{"Jpan"});
        QCOMPARE(saved.front().kind, LyricsVariantKind::Original);
        QCOMPARE(saved.front().source, std::string{"LRCLIB"});
        QVERIFY(!saved.front().generated);
        const auto parsed = parseLrc(saved.front().content);
        QCOMPARE(parsed.error, LyricsParseError::None);
        QCOMPARE(parsed.document.lines.front().timestamp_ms, std::int64_t{1000});
        const auto original_id = saved.front().id;
        QCOMPARE(h.binding.candidates().size(), 2);
        QVERIFY(h.binding.canSave());
        QVERIFY(h.binding.saveCandidate(1, "Romanized", "ja", "Latn", "romanized"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QVERIFY(h.writer.acquiredLyricsSave().ticket != first_ticket);
        saved = h.store->read(request.final_mp3);
        QCOMPARE(saved.size(), std::size_t{2});
        QCOMPARE(saved.front().id, original_id);
        QCOMPARE(saved.back().kind, LyricsVariantKind::Romanized);
        QCOMPARE(saved.back().script, std::string{"Latn"});
        QCOMPARE(saved.back().content, std::string{"Nihongo\nTsugi no gyou"});
        QCOMPARE(h.network.requests.size(), 1);
        QCOMPARE(h.writer.title(), QStringLiteral("Current song"));
        QCOMPARE(h.writer.artist(), QStringLiteral("Current artist"));
        QVERIFY(h.playback_network.requests.isEmpty());
    }
    void pastedFormsCanBeSavedToThePublishedTrackWithoutOnlineCandidates_data() {
        QTest::addColumn<int>("http_status");
        QTest::newRow("not-found") << 404;
        QTest::newRow("provider-unavailable") << 503;
    }
    void pastedFormsCanBeSavedToThePublishedTrackWithoutOnlineCandidates() {
        QFETCH(int, http_status);
        Harness h;
        const auto current = h.directory.filePath("current.mp3");
        h.writer.setTrack(current, "Current song", "Current artist", {}, 0);
        QTRY_VERIFY(h.writer.variantsReady());
        const auto request = h.request();
        QVERIFY(h.binding.offerPublished(request));
        const auto token = h.binding.requestToken();
        h.network.replies.back()->finish(http_status, "{}");
        QVERIFY(!h.binding.canSave());
        QVERIFY(h.binding.candidates().isEmpty());
        QVERIFY(h.binding.property("canSavePasted").toBool());

        QVERIFY(savePasted(h.binding, token, QStringLiteral("[00:01.25]日本語\r\n[00:02.10]次の行"), "Original", "ja",
                           "Jpan"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QVERIFY(savePasted(h.binding, token, "Nihongo\nTsugi no gyou", "Romanizada", "ja", "Latn", "romanized"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);

        const auto saved = h.store->read(request.final_mp3);
        QCOMPARE(saved.size(), std::size_t{2});
        QCOMPARE(saved[0].kind, LyricsVariantKind::Original);
        QCOMPARE(saved[0].script, std::string{"Jpan"});
        QCOMPARE(saved[0].source, std::string{"manual"});
        QVERIFY(!saved[0].generated);
        const auto parsed = parseLrc(saved[0].content);
        QCOMPARE(parsed.error, LyricsParseError::None);
        QCOMPARE(parsed.document.lines.size(), std::size_t{2});
        QCOMPARE(parsed.document.lines.front().timestamp_ms, std::int64_t{1250});
        QCOMPARE(parsed.document.lines.front().text, std::string{"日本語"});
        QCOMPARE(saved[1].kind, LyricsVariantKind::Romanized);
        QCOMPARE(saved[1].script, std::string{"Latn"});
        QCOMPARE(saved[1].source, std::string{"manual"});
        QCOMPARE(saved[1].content, std::string{"Nihongo\nTsugi no gyou"});

        QVERIFY(savePasted(h.binding, token, QStringLiteral("[00:01.250]日本語\n[00:02.100]次の行"), "Mesmo conteúdo",
                           "ja", "Jpan"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Unchanged);
        QCOMPARE(h.store->saves.load(), 2);
        QVERIFY(h.store->read(current).empty());
        QCOMPARE(h.writer.title(), QStringLiteral("Current song"));
        QCOMPARE(h.writer.artist(), QStringLiteral("Current artist"));
        QCOMPARE(h.network.requests.size(), 1);
        QVERIFY(h.playback_network.requests.isEmpty());
    }

    void linksAndPastedAttributionBelongToTheCurrentPublishedReview() {
        ExternalUrlCapture browser;
        Harness h;
        auto request = h.request();
        QVERIFY(h.binding.offerPublished(request));
        const auto token = h.binding.requestToken();
        QVERIFY(h.binding.property("hasLyricsPage").toBool());
        QVERIFY(browser.urls.isEmpty());
        const auto openPage = [&](const QString& expected) {
            bool accepted{};
            return QMetaObject::invokeMethod(&h.binding, "openLyricsPage", Qt::DirectConnection,
                                             Q_RETURN_ARG(bool, accepted), Q_ARG(QString, expected)) &&
                   accepted;
        };
        const auto saveWithPage = [&](const QString& expected) {
            bool accepted{};
            return QMetaObject::invokeMethod(&h.binding, "savePasted", Qt::DirectConnection,
                                             Q_RETURN_ARG(bool, accepted), Q_ARG(QString, expected),
                                             Q_ARG(QString, QStringLiteral("Synthetic romaji")),
                                             Q_ARG(QString, QStringLiteral("Romanizada")), Q_ARG(QString, QString{}),
                                             Q_ARG(QString, QStringLiteral("Latn")),
                                             Q_ARG(QString, QStringLiteral("romanized")), Q_ARG(bool, true)) &&
                   accepted;
        };
        QVERIFY(!openPage(QStringLiteral("stale")));
        QVERIFY(openPage(token));
        QCOMPARE(browser.urls, QList<QUrl>{QUrl(request.lyrics_page)});
        h.network.replies.back()->finish(404, "{}");
        QVERIFY(!saveWithPage(QStringLiteral("stale")));
        QVERIFY(saveWithPage(token));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        const auto saved = h.store->read(request.final_mp3);
        QCOMPARE(saved.size(), std::size_t{1});
        QCOMPARE(saved.front().source, request.lyrics_page.toStdString());
        QCOMPARE(saved.front().kind, LyricsVariantKind::Romanized);
        QCOMPARE(h.network.requests.size(), 1);

        QVERIFY(h.binding.dismiss());
        auto next = h.request("published_2");
        next.lyrics_page.clear();
        QVERIFY(h.binding.offerPublished(next));
        QVERIFY(!h.binding.property("hasLyricsPage").toBool());
        QVERIFY(!openPage(token));
        QVERIFY(!openPage(h.binding.requestToken()));
        QVERIFY(!saveWithPage(h.binding.requestToken()));
        QCOMPARE(browser.urls.size(), 1);
        QCOMPARE(h.store->saves.load(), 1);
    }
    void letrasVersionsAreSavedTogetherForThePublishedTrack() {
        Harness h;
        const auto request = h.request();
        QVERIFY(h.binding.offerPublished(request));
        h.network.replies.back()->finish(404, "{}");
        const auto token = h.binding.requestToken();
        const QVariantList versions{QVariantMap{{"content", "Original text"},
                                                {"name", "Original"},
                                                {"language", "ja"},
                                                {"script", "Jpan"},
                                                {"kind", "original"}},
                                    QVariantMap{{"content", "Romanized text"},
                                                {"name", "Romaji"},
                                                {"language", "ja"},
                                                {"script", "Latn"},
                                                {"kind", "romanized"}},
                                    QVariantMap{{"content", "Translated text"},
                                                {"name", "Português"},
                                                {"language", "pt"},
                                                {"script", "Latn"},
                                                {"kind", "translation"}}};
        const auto save = [&](const QString& expected, const QVariantList& batch) {
            bool accepted{};
            return QMetaObject::invokeMethod(&h.binding, "saveLetrasVersions", Qt::DirectConnection,
                                             Q_RETURN_ARG(bool, accepted), Q_ARG(QString, expected),
                                             Q_ARG(QVariantList, batch)) &&
                   accepted;
        };
        QVERIFY(!save("stale", versions));
        auto invalid = versions;
        auto wrong = invalid.last().toMap();
        wrong["content"] = "[00:99.00]Invalid timestamp";
        invalid.last() = wrong;
        QVERIFY(!save(token, invalid));
        QVERIFY(h.binding.statusText().contains("3"));
        QCOMPARE(h.store->saves.load(), 0);

        QVERIFY(save(token, versions));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        const auto saved = h.store->read(request.final_mp3);
        QCOMPARE(saved.size(), std::size_t{3});
        for (const auto& item : saved)
            QCOMPARE(item.source, request.lyrics_page.toStdString());
        QCOMPARE(saved[2].language, std::string{"pt"});
        QCOMPARE(saved[2].content, std::string{"Translated text"});
        QCOMPARE(h.store->saves.load(), 1);
        QVERIFY(save(token, versions));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Unchanged);
        QCOMPARE(h.store->saves.load(), 1);
        QCOMPARE(h.network.requests.size(), 1);
    }
    void pastedSaveRejectsStaleInvalidAndReentrantActionsAndRetriesOnlyTheSameDraft() {
        Harness h;
        auto request = h.request();
        request.artist.clear();
        QVERIFY(h.binding.offerPublished(request));
        const auto token = h.binding.requestToken();
        QVERIFY(!savePasted(h.binding, "stale", "Lyrics", "Name"));
        QVERIFY(!savePasted(h.binding, token, "  \n", "Name"));
        QVERIFY(!savePasted(h.binding, token, "[00:99.00]Invalid timestamp", "Name"));
        QVERIFY(!savePasted(h.binding, token, QString(QChar{0xd800}), "Name"));
        QVERIFY(!savePasted(h.binding, token, QString(lyrics_max_input_bytes / 3 + 1, QChar{0x65e5}), "Name"));
        QVERIFY(!savePasted(h.binding, token, "Lyrics", " "));
        QCOMPARE(h.store->loads.load(), 0);
        QCOMPARE(h.store->saves.load(), 0);
        QVERIFY(!h.binding.canRetrySave());

        h.store->save_status = LyricsVariantsSaveStatus::Failed;
        bool notified{}, nested_save{}, nested_dismiss{};
        connect(&h.writer, &LyricsBinding::changed, &h.binding, [&] {
            if (notified || h.writer.acquiredLyricsSave().state != AcquiredLyricsSaveState::Loading)
                return;
            notified = true;
            nested_save = savePasted(h.binding, token, "Different", "Other");
            nested_dismiss = h.binding.dismiss();
        });
        QVERIFY(savePasted(h.binding, token, "Original text", "Name"));
        QVERIFY(notified);
        QVERIFY(!nested_save);
        QVERIFY(!nested_dismiss);
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::SaveFailed);
        QVERIFY(!h.binding.property("canSavePasted").toBool());
        QVERIFY(!savePasted(h.binding, token, "Different", "Other"));
        QVERIFY(h.binding.canRetrySave());
        const auto ticket = h.writer.acquiredLyricsSave().ticket;
        h.store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(h.binding.retrySave());
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(h.writer.acquiredLyricsSave().ticket, ticket);
        const auto attempts = h.store->attempts();
        QCOMPARE(attempts.size(), std::size_t{2});
        QCOMPARE(attempts[0], attempts[1]);
        QVERIFY(h.binding.dismiss());
        request.job_id = "replacement";
        request.final_mp3 = h.directory.filePath("replacement.mp3");
        QVERIFY(h.binding.offerPublished(request));
        QVERIFY(!savePasted(h.binding, token, "Lyrics", "Name"));
        QCOMPARE(h.store->saves.load(), 2);
        QVERIFY(h.network.requests.isEmpty());
        QVERIFY(h.playback_network.requests.isEmpty());
    }
    void invalidPublicationNeverBecomesTheReviewTarget() {
        Harness h;
        const auto invalid = h.request("invalid.job");
        bool observed{};
        connect(&h.binding, &DownloadLyricsBinding::changed, &h.binding,
                [&] { observed |= h.binding.hasRequest() || h.binding.canSavePasted(); });
        QVERIFY(!h.binding.offerPublished(invalid));
        QVERIFY(!observed);
        QVERIFY(!h.binding.canSavePasted());
        QVERIFY(!savePasted(h.binding, h.binding.requestToken(), "Lyrics", "Name"));
        QVERIFY(!h.binding.hasRequest());
        QCOMPARE(h.store->loads.load(), 0);
        QCOMPARE(h.store->saves.load(), 0);
        QVERIFY(h.network.requests.isEmpty());
    }
    void invalidIndexNameAndUnconfirmedOrInvalidFormNeverReachTheWriter_data() {
        QTest::addColumn<int>("scenario");
        for (int index = 0; index < 8; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void invalidIndexNameAndUnconfirmedOrInvalidFormNeverReachTheWriter() {
        QFETCH(int, scenario);
        Harness h;
        QVERIFY(h.ready());
        int index{};
        QString name = "Explicit", language, script, kind = "original";
        switch (scenario) {
        case 0:
            index = -1;
            break;
        case 1:
            index = 2;
            break;
        case 2:
            name.clear();
            break;
        case 3:
            name = "  ";
            break;
        case 4:
            kind.clear();
            break;
        case 5:
            kind = "guessed";
            break;
        case 6:
            language = QString(65, 'a');
            break;
        default:
            name = QString(129, 'a');
            break;
        }
        QVERIFY(!h.binding.saveCandidate(index, name, language, script, kind));
        QCOMPARE(h.store->loads.load(), 0);
        QCOMPARE(h.store->saves.load(), 0);
        QCOMPARE(h.binding.candidates().size(), 2);
        QCOMPARE(h.network.requests.size(), 1);
    }
    void aFailedSaveRetainsItsExactTicketAndCandidateForRetryWithoutHttpOrDismissal() {
        Harness h;
        QVERIFY(h.ready());
        QSignalSpy finished(&h.binding, &DownloadLyricsBinding::reviewFinished);
        QVERIFY(finished.isValid());
        h.store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(h.binding.saveCandidate(0, "Explicit", "", "", "original"));
        QVERIFY(!h.binding.dismiss());
        QVERIFY(finished.isEmpty());
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::SaveFailed);
        const auto pending = h.writer.acquiredLyricsSave();
        QVERIFY(!h.binding.busy());
        QVERIFY(h.binding.canRetrySave());
        QVERIFY(h.binding.property("pendingDiscardToken").toString().isEmpty());
        QVERIFY(!discardPendingSave(h.binding, h.binding.requestToken(), QString::fromStdString(pending.ticket)));
        QVERIFY(!h.binding.canSave());
        QVERIFY(!h.binding.canDismiss());
        QVERIFY(!h.binding.dismiss());
        QVERIFY(finished.isEmpty());
        QVERIFY(!h.binding.offerPublished(h.request("replacement")));
        QVERIFY(!h.binding.retry("Other title", "Other artist"));
        const auto before = h.store->attempts();
        QCOMPARE(before.size(), std::size_t{1});
        h.store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(h.binding.retrySave());
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(h.writer.acquiredLyricsSave().ticket, pending.ticket);
        QCOMPARE(h.writer.acquiredLyricsSave().target, pending.target);
        const auto after = h.store->attempts();
        QCOMPARE(after.size(), std::size_t{2});
        QCOMPARE(after[0], after[1]);
        QCOMPARE(h.network.requests.size(), 1);
        QVERIFY(h.binding.canDismiss());
        QCOMPARE(h.binding.candidates().size(), 2);
        QVERIFY(finished.isEmpty());
        QVERIFY(h.binding.dismiss());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.front().front().toString(), QString::fromStdString(h.request().job_id));
    }
    void loadFailureRetainsTheSameSaveTicketAndCanRetryOnlyTheSharedWriter() {
        Harness h;
        QVERIFY(h.ready());
        h.store->load_status = LyricsVariantsLoadStatus::Unavailable;
        QVERIFY(h.binding.saveCandidate(1, "Untimed", "", "", "original"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::LoadFailed);
        const auto ticket = h.writer.acquiredLyricsSave().ticket;
        QVERIFY(h.binding.canRetrySave());
        QVERIFY(!h.binding.dismiss());
        QCOMPARE(h.store->saves.load(), 0);
        h.store->load_status = LyricsVariantsLoadStatus::Missing;
        QVERIFY(h.binding.retrySave());
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(h.writer.acquiredLyricsSave().ticket, ticket);
        QCOMPARE(h.network.requests.size(), 1);
    }
    void discardingAnInvalidAttemptPreservesTheFullCatalogueAndKeepsTheReviewOpen() {
        Harness h;
        auto request = h.request();
        request.artist.clear();
        LyricsVariants existing;
        for (std::size_t i = 0; i < maximum_lyrics_variants; ++i) {
            const auto suffix = std::to_string(i + 1);
            existing.push_back({i + 1, "Existing " + suffix, "", "", "manual", "Saved verse " + suffix,
                                LyricsVariantKind::Original, false});
        }
        QCOMPARE(h.store->save(nativePath(request.final_mp3), existing), LyricsVariantsSaveStatus::Saved);
        const auto initial_saves = h.store->saves.load();
        QVERIFY(h.binding.offerPublished(request));
        const auto request_token = h.binding.requestToken();
        QSignalSpy finished(&h.binding, &DownloadLyricsBinding::reviewFinished);
        QVERIFY(finished.isValid());

        QVERIFY(savePasted(h.binding, request_token, "New lyrics", "Another version"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Invalid);
        QVERIFY(!h.binding.canDismiss());
        const auto first_discard_token = h.binding.property("pendingDiscardToken").toString();
        QVERIFY(!first_discard_token.isEmpty());
        QVERIFY(!discardPendingSave(h.binding, "stale request", first_discard_token));
        QVERIFY(!discardPendingSave(h.binding, request_token, "stale attempt"));
        QVERIFY(h.writer.variantsHasPendingChange());
        QVERIFY(discardPendingSave(h.binding, request_token, first_discard_token));
        QCOMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Abandoned);
        QCOMPARE(h.binding.requestToken(), request_token);
        QVERIFY(h.binding.hasRequest());
        QVERIFY(h.binding.canSavePasted());
        QVERIFY(h.binding.canDismiss());
        QVERIFY(h.binding.property("pendingDiscardToken").toString().isEmpty());
        QVERIFY(finished.isEmpty());

        QVERIFY(savePasted(h.binding, request_token, "Different lyrics", "Another attempt"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Invalid);
        const auto second_discard_token = h.binding.property("pendingDiscardToken").toString();
        QVERIFY(!second_discard_token.isEmpty());
        QVERIFY(second_discard_token != first_discard_token);
        QVERIFY(!discardPendingSave(h.binding, request_token, first_discard_token));
        QVERIFY(h.writer.variantsHasPendingChange());
        QVERIFY(discardPendingSave(h.binding, request_token, second_discard_token));
        QVERIFY(!h.writer.variantsHasPendingChange());
        QCOMPARE(h.store->read(request.final_mp3), existing);
        QCOMPARE(h.store->saves.load(), initial_saves);
        QVERIFY(h.network.requests.isEmpty());
        QVERIFY(h.playback_network.requests.isEmpty());
        QVERIFY(finished.isEmpty());
        QVERIFY(h.binding.dismiss());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.front().front().toString(), QString::fromStdString(request.job_id));
    }
    void dismissingAPreviewNeverAbandonsAnUnrelatedPendingWrite() {
        Harness h;
        const auto foreign = h.directory.filePath("foreign.mp3");
        h.store->block_save = true;
        QVERIFY(h.writer.appendAcquiredLyrics("foreign", foreign, foreignDraft(), true));
        QTRY_VERIFY(h.store->save_entered.load());
        QVERIFY(h.binding.offerPublished(h.request()));
        h.network.replies.back()->finish(200, payload());
        QVERIFY(!h.binding.canSave());
        QVERIFY(h.binding.canDismiss());
        QVERIFY(h.binding.dismiss());
        QVERIFY(h.writer.variantsHasPendingChange());
        QCOMPARE(h.writer.acquiredLyricsSave().ticket, std::string{"foreign"});
        h.store->release_save.release();
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(h.store->read(foreign).size(), std::size_t{1});
        QVERIFY(h.store->read(h.request().final_mp3).empty());
    }
    void revokingTheLookupInvalidatesSaveAuthorityForItsOldPreview() {
        Harness h;
        QVERIFY(h.ready());
        h.lookup.clear();
        QVERIFY(!h.binding.canSave());
        QVERIFY(!h.binding.saveCandidate(0, "Stale", "", "", "original"));
        QVERIFY(h.binding.candidates().isEmpty());
        QCOMPARE(h.store->loads.load(), 0);
        QCOMPARE(h.store->saves.load(), 0);
    }
    void aForeignWriterAcknowledgementCannotAuthorizeRetryOfThisBindingsCandidate() {
        Harness h;
        QVERIFY(h.ready());
        h.store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(h.binding.saveCandidate(0, "Owned", "", "", "original"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::SaveFailed);
        const auto owned_ticket = h.writer.acquiredLyricsSave().ticket;
        // The writer can abandon a failed write through its explicit reload action.
        QVERIFY(h.writer.reloadVariants());
        h.store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(h.writer.appendAcquiredLyrics("foreign", h.request().final_mp3, foreignDraft(), true));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QVERIFY(h.writer.acquiredLyricsSave().ticket != owned_ticket);
        QVERIFY(!h.binding.canRetrySave());
        QVERIFY(!h.binding.retrySave());
        QCOMPARE(h.store->saves.load(), 2);
        QCOMPARE(h.network.requests.size(), 1);
    }
    void reentrantWriterNotificationsCannotDismissReplaceOrMutateAnAdmittedCandidate() {
        Harness h;
        QVERIFY(h.ready());
        bool acted{}, dismissed{}, replaced{}, retried{};
        QString name = "Explicit", kind = "original";
        connect(&h.writer, &LyricsBinding::changed, &h.binding, [&] {
            if (acted || h.writer.acquiredLyricsSave().state != AcquiredLyricsSaveState::Loading)
                return;
            acted = true;
            name.fill('X');
            kind = "romanized";
            dismissed = h.binding.dismiss();
            replaced = h.binding.offerPublished(h.request("replacement"));
            retried = h.binding.retry("Other", "Artist");
        });
        QVERIFY(h.binding.saveCandidate(0, name, "", "", kind));
        QVERIFY(acted);
        QVERIFY(!dismissed);
        QVERIFY(!replaced);
        QVERIFY(!retried);
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        const auto saved = h.store->read(h.request().final_mp3);
        QCOMPARE(saved.size(), std::size_t{1});
        QCOMPARE(saved.front().name, std::string{"Explicit"});
        QCOMPARE(saved.front().kind, LyricsVariantKind::Original);
        QCOMPARE(h.network.requests.size(), 1);
    }
    void aReentrantDismissDuringOfferRevokesItBeforeHttpCanEscape() {
        Harness h;
        bool acted{}, dismissed{};
        connect(&h.binding, &DownloadLyricsBinding::changed, &h.binding, [&] {
            if (acted || !h.binding.hasRequest() || !h.binding.busy())
                return;
            acted = true;
            dismissed = h.binding.dismiss();
        });
        QVERIFY(!h.binding.offerPublished(h.request()));
        QVERIFY(acted);
        QVERIFY(dismissed);
        QVERIFY(!h.binding.hasRequest());
        QVERIFY(h.binding.candidates().isEmpty());
        QVERIFY(h.network.requests.isEmpty());
        QCOMPARE(h.store->loads.load(), 0);
    }
    void exitDelegatesLookupCancellationWithoutAutomaticallySearchingAfterResume() {
        Harness h;
        QVERIFY(h.binding.offerPublished(h.request()));
        QVERIFY(!h.binding.prepareExit());
        QVERIFY(!h.binding.offerPublished(h.request("during_exit")));
        QVERIFY(!h.binding.retry("Other", "Artist"));
        QVERIFY(!h.binding.saveCandidate(0, "Closed", "", "", "original"));
        QVERIFY(h.binding.candidates().isEmpty());
        QTRY_VERIFY(h.binding.drained());
        QVERIFY(h.binding.prepareExit());
        h.binding.resume();
        QCOMPARE(h.network.requests.size(), 1);
        if (h.binding.hasRequest())
            QVERIFY(h.binding.dismiss());
        QTRY_VERIFY_WITH_TIMEOUT(h.service.canSearch(), 1500);
        QVERIFY(h.binding.offerPublished(h.request("resumed")));
        h.network.replies.back()->finish(200, payload());
        QVERIFY(h.binding.canSave());
        h.binding.beginShutdown();
        h.binding.resume();
        QVERIFY(!h.binding.offerPublished(h.request("closed")));
        QVERIFY(!h.binding.retry("Song", "Artist"));
        QVERIFY(!h.binding.canSave());
        QVERIFY(h.binding.candidates().isEmpty());
        QVERIFY(h.binding.drained());
    }
    void pasteRemainsAvailableAfterCancellingExitDuringTheInitialLookup() {
        Harness h;
        bool preparing{};
        connect(&h.binding, &DownloadLyricsBinding::changed, &h.binding, [&] {
            if (!preparing && h.binding.hasRequest()) {
                preparing = true;
                QVERIFY(!h.binding.prepareExit());
            }
        });
        QVERIFY(!h.binding.offerPublished(h.request()));
        QVERIFY(preparing);
        QVERIFY(h.binding.hasRequest());
        QTRY_VERIFY(h.binding.drained());
        h.binding.resume();
        QVERIFY(h.binding.canSavePasted());
        QVERIFY(h.network.requests.isEmpty());
        QVERIFY(savePasted(h.binding, h.binding.requestToken(), "Local lyrics", "Original"));
        QTRY_COMPARE(h.writer.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QVERIFY(h.network.requests.isEmpty());
    }
};
QTEST_GUILESS_MAIN(DownloadLyricsBindingTests)
#include "DownloadLyricsBindingTests.moc"
