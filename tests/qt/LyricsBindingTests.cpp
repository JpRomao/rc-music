#include "adapters/QtLyricsService.hpp"
#include "m0/ports/LyricsVariantsStore.hpp"
#include "presentation/LyricsBinding.hpp"

#include <QFile>
#include <QMetaProperty>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

using namespace music_player::m0;
namespace {
void save(const QString& path, const QByteArray& text) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text), text.size());
}
std::filesystem::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().toStdString());
#endif
}
class MemoryVariantsStore final : public LyricsVariantsStore {
  public:
    std::atomic_int load_calls{}, save_calls{};
    std::atomic_bool block_load{}, block_save{}, throw_load{}, throw_save{}, commit_before_failure{};
    std::atomic<LyricsVariantsLoadStatus> load_status{LyricsVariantsLoadStatus::Missing};
    std::atomic<LyricsVariantsSaveStatus> save_status{LyricsVariantsSaveStatus::Saved};
    QSemaphore load_entered, load_release, save_entered, save_release;
    std::mutex mutex;
    std::map<std::filesystem::path, LyricsVariants> disk;
    std::vector<std::pair<std::filesystem::path, LyricsVariants>> attempts;
    LyricsVariantsLoadResult load(const std::filesystem::path& path) override {
        ++load_calls;
        if (block_load.exchange(false)) {
            load_entered.release();
            static_cast<void>(load_release.tryAcquire(1, 2000));
        }
        if (throw_load)
            throw std::runtime_error("load refused");
        std::lock_guard lock(mutex);
        if (const auto found = disk.find(path); found != disk.end())
            return {.variants = found->second, .status = LyricsVariantsLoadStatus::Loaded};
        return {.status = load_status.load()};
    }
    LyricsVariantsSaveStatus save(const std::filesystem::path& path, const LyricsVariants& variants) override {
        ++save_calls;
        if (block_save.exchange(false)) {
            save_entered.release();
            static_cast<void>(save_release.tryAcquire(1, 2000));
        }
        std::lock_guard lock(mutex);
        attempts.emplace_back(path, variants);
        if (save_status == LyricsVariantsSaveStatus::Saved || commit_before_failure)
            disk[path] = variants;
        if (throw_save)
            throw std::runtime_error("save refused");
        return save_status;
    }
    LyricsVariants read(const std::filesystem::path& path) {
        std::lock_guard lock(mutex);
        return disk[path];
    }
};
LyricsVariants acquiredDrafts() {
    return {{0, "Original", "ja", "Jpan", "Synthetic provider", "[00:01.000]独自のテスト", LyricsVariantKind::Original,
             false},
            {0, "Romanized", "ja", "Latn", "Synthetic provider", "[00:01.000]Dokujino test",
             LyricsVariantKind::Romanized, false}};
}
struct ReleaseBlockedStore final {
    std::shared_ptr<MemoryVariantsStore> store;
    ~ReleaseBlockedStore() {
        store->load_release.release();
        store->save_release.release();
    }
};
class OnlineReply final : public QNetworkReply {
  public:
    OnlineReply(const QNetworkRequest& request, QByteArray bytes, QObject* parent)
        : QNetworkReply(parent), bytes_(std::move(bytes)) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
            setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {
        if (isFinished())
            return;
        setError(OperationCanceledError, "cancelled fake");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return bytes_.size() - offset_ + QNetworkReply::bytesAvailable(); }

  protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = std::min(maximum, qint64{bytes_.size() - offset_});
        if (count <= 0)
            return isFinished() ? -1 : 0;
        std::memcpy(data, bytes_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    const QByteArray bytes_;
    qint64 offset_{};
};
class OnlineNetwork final : public QNetworkAccessManager {
  public:
    int requests{};
    QByteArray body{
        R"({"syncedLyrics":"[00:01.000]Online original\n[00:04.000]Online next","plainLyrics":"Online original\nOnline next","instrumental":false})"};

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice*) override {
        if (operation != GetOperation)
            return nullptr;
        ++requests;
        return new OnlineReply(request, body, this);
    }
};
} // namespace
class LyricsBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void queueRecordingSearchSurvivesPlaybackAdvanceAndSavesOnlyTheOriginalTarget() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto playing = temp.filePath("playing.mp3");
        const auto next = temp.filePath("next.mp3");
        const auto queued = temp.filePath("queued.mp3");
        auto target = queued;
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(playing)] = {{1, "Playing", {}, {}, "Manual", "Playing lyric"}};
        store->disk[nativePath(next)] = {{2, "Next", {}, {}, "Manual", "Next lyric"}};
        OnlineNetwork network;
        network.body = R"([
          {"id":15,"trackName":"Queued","artistName":"Artist","albumName":"Album","duration":82,
           "instrumental":false,"plainLyrics":"Queue words","syncedLyrics":"[00:01.00]Timed queue words"}
        ])";
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setQueueLyricsSource(
            [&](const QString& occurrence) { return occurrence == "queued-occurrence" ? target : QString{}; });
        binding.setTrack(playing, "Playing", {}, {}, 1000, "first");
        QTRY_VERIFY(binding.variantsCanEdit());
        QVERIFY(binding.selectVariant("1"));
        const auto manager = binding.openQueuedManager("queued-occurrence");
        QVERIFY(!manager.isEmpty());
        QVERIFY(binding.property("queuedManagerSearchAvailable").toBool());
        QTRY_VERIFY(binding.queuedManagerCanEdit());
        QCOMPARE(network.requests, 0);
        QVERIFY(binding.searchQueuedRecordings(manager, "Queued", "Artist", "Album", 82'000));
        QCOMPARE(binding.plainText(), QStringLiteral("Playing lyric"));
        binding.setTrack(next, "Next", {}, {}, 1000, "second");
        QTRY_VERIFY(!binding.recordingSearchBusy() && binding.variantsReady());
        QVERIFY(binding.selectVariant("2"));
        QCOMPARE(binding.recordingCandidates().size(), 1);
        const auto revision = binding.recordingSearchRevision();
        QCOMPARE(binding.previewRecording(0, 1, revision), QStringLiteral("Queue words"));
        QVERIFY(!binding.useRecording(0, 1, revision));
        QVERIFY(!binding.saveRecording(0, 1, "Wrong target", revision));
        QVERIFY(!binding.saveQueuedRecording("wrong manager", 0, 1, "Queue original", revision));
        QCOMPARE(store->save_calls.load(), 0);
        store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(binding.saveQueuedRecording(manager, 0, 1, "Queue original", revision));
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QCOMPARE(binding.pendingRecordingSaveRevision(), revision);
        QVERIFY(!binding.retryRecordingSave("wrong revision"));
        QCOMPARE(binding.queuedManagerVariants().size(), 0);
        QCOMPARE(binding.plainText(), QStringLiteral("Next lyric"));
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryRecordingSave(revision));
        QTRY_COMPARE(binding.queuedManagerVariants().size(), 1);
        const auto saved = store->read(nativePath(queued));
        QCOMPARE(saved.front().content, std::string{"Queue words"});
        QCOMPARE(saved.front().source, std::string{"LRCLIB"});
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("2"));
        QCOMPARE(binding.plainText(), QStringLiteral("Next lyric"));
        QCOMPARE(network.requests, 1);

        target = temp.filePath("relocated.mp3");
        binding.refreshQueuedLyricsTarget();
        QVERIFY(!binding.property("queuedManagerSearchAvailable").toBool());
        QVERIFY(binding.recordingCandidates().isEmpty());
        QVERIFY(binding.previewRecording(0, 1, revision).isEmpty());
        QVERIFY(!binding.saveQueuedRecording(manager, 0, 1, "Queue original", revision));
        QVERIFY(store->read(nativePath(target)).empty());
    }
    void removingTheQueuedTargetAbortsPendingSearchAndRevokesItsToken() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto target = temp.filePath("queued.mp3");
        const auto original = target;
        auto store = std::make_shared<MemoryVariantsStore>();
        OnlineNetwork network;
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setQueueLyricsSource([&](const QString&) { return target; });
        const auto manager = binding.openQueuedManager("queued-occurrence");
        QVERIFY(!manager.isEmpty());
        QVERIFY(binding.searchQueuedRecordings(manager, "Song", {}, {}, 0));
        QCOMPARE(network.requests, 1);
        QVERIFY(binding.recordingSearchBusy());
        const auto revision = binding.recordingSearchRevision();
        target.clear();
        binding.refreshQueuedLyricsTarget();
        QVERIFY(!binding.queuedManagerSearchAvailable());
        QVERIFY(!binding.recordingSearchBusy());
        QVERIFY(binding.recordingSearchRevision() != revision);
        QCoreApplication::processEvents();
        QVERIFY(binding.recordingCandidates().isEmpty());
        QVERIFY(binding.recordingSearchStatus().isEmpty());
        QVERIFY(binding.previewRecording(0, 0, revision).isEmpty());
        target = original;
        binding.refreshQueuedLyricsTarget();
        QVERIFY(!binding.queuedManagerSearchAvailable());
        QVERIFY(!binding.searchQueuedRecordings(manager, "Song", {}, {}, 0));
        QCOMPARE(network.requests, 1);
        QCOMPARE(store->save_calls.load(), 0);
    }
    void managedRecordingAddsToThePlayingCatalogueWithoutSelectingTheNewLyric() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath("playing.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(path)] = {{12, "Playing", {}, {}, "Manual", "Playing lyric"}};
        OnlineNetwork network;
        network.body = R"([
          {"id":15,"trackName":"Played","artistName":"Artist","albumName":"Album","duration":82,
           "instrumental":false,"plainLyrics":"Alternative","syncedLyrics":null}
        ])";
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setTrack(path, "Played", "Artist", {}, 82'000, "first");
        QTRY_VERIFY(binding.variantsCanEdit());
        QVERIFY(binding.selectVariant("12"));
        QVERIFY(binding.searchRecordings("Played", "Artist", {}, 82'000));
        QTRY_COMPARE(binding.recordingCandidates().size(), 1);
        const auto revision = binding.recordingSearchRevision();
        QCOMPARE(binding.previewRecording(0, 0, revision), QStringLiteral("Alternative"));
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.saveManagedRecording(0, 0, "Alternative", revision));
        QTRY_COMPARE(binding.variants().size(), 2);
        QCOMPARE(store->read(nativePath(path)).back().content, std::string{"Alternative"});
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));
        QCOMPARE(binding.plainText(), QStringLiteral("Playing lyric"));
        QVERIFY(binding.pendingRecordingSaveRevision().isEmpty());
        QVERIFY(!binding.retryRecordingSave(revision));
    }
    void importsAnLrcAsAnUncommittedDraftAndRejectsDifferentTrackOrNonLocalUrl() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = directory.filePath("Original.lrc");
        save(file, "[00:01.000]One\n[00:02.000]Two");
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(directory.filePath("manual.mp3"), "Manual", "Artist", {}, 5000, "one");
        QTRY_VERIFY(binding.variantsCanEdit());
        QSignalSpy results(&binding, &LyricsBinding::importedVariantLrc);
        QVERIFY(!binding.importVariantLrc(QUrl("https://example.invalid/lyrics.lrc"), "one", "url"));
        QVERIFY(!binding.importVariantLrc(QUrl::fromLocalFile(file), "two", "stale"));
        QVERIFY(binding.importVariantLrc(QUrl::fromLocalFile(file), "one", "draft"));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.front().at(0).toString(), "draft");
        QCOMPARE(results.front().at(1).toString(), "Original");
        const auto document = parseLrc(results.front().at(2).toString().toUtf8().toStdString());
        QCOMPARE(document.document.lines.size(), 2U);
        QCOMPARE(document.document.lines.front().timestamp_ms, 1000);
        QVERIFY(binding.variants().isEmpty());
        QCOMPARE(store->save_calls.load(), 0);
    }
    void queuedManagerEditsAndRemovesWithoutChangingThePlayingLyric() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto playing = temp.filePath("playing.mp3");
        const auto target = temp.filePath("published.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(playing)] = {{1, "Playing", {}, {}, "Manual", "Playing lyric"}};
        store->disk[nativePath(target)] = {{12, "Reviewed", "ja", "Jpan", "LRCLIB", "[00:01.000]Original"},
                                           {13, "Other", {}, {}, "Manual", "Other lyric"}};
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setQueueLyricsSource(
            [&](const QString& occurrence) { return occurrence == "queued" ? target : QString{}; });
        binding.setTrack(playing, "Playing", {}, {}, 1000, "current");
        QTRY_VERIFY(binding.variantsCanEdit());
        QVERIFY(binding.selectVariant("1"));
        QVERIFY(binding.metaObject()->indexOfMethod("openQueuedManager(QString)") >= 0);
        QString manager;
        QVERIFY(
            QMetaObject::invokeMethod(&binding, "openQueuedManager", qReturnArg(manager), QStringLiteral("queued")));
        QVERIFY(!manager.isEmpty());
        QVERIFY(!manager.contains(target));
        QTRY_COMPARE(binding.property("queuedManagerVariants").toList().size(), 2);
        QVariantMap draft;
        QVERIFY(QMetaObject::invokeMethod(&binding, "prepareQueuedVariantEdit", qReturnArg(draft), manager,
                                          QStringLiteral("12")));
        const auto token = draft.value("token").toString();
        QVERIFY(!token.isEmpty());
        QCOMPARE(draft.value("source").toString(), QStringLiteral("LRCLIB"));
        QVERIFY(!binding.saveVariantEdit(token, "Corrected", "ja", "Jpan", "original", "[00:02.000]Incorrect", false,
                                         false));
        QVERIFY(binding.saveVariantEdit(token, "Corrected", "ja", "Jpan", "original", "[00:01.000]Corrected", false,
                                        false));
        QTRY_VERIFY(!binding.property("queuedManagerBusy").toBool());
        QCOMPARE(store->read(nativePath(target))[0].name, std::string{"Corrected"});
        QCOMPARE(store->read(nativePath(target))[0].id, std::uint64_t{12});
        QCOMPARE(binding.plainText(), QStringLiteral("Playing lyric"));

        store->save_status = LyricsVariantsSaveStatus::Failed;
        bool accepted = false;
        QVERIFY(QMetaObject::invokeMethod(&binding, "removeQueuedVariant", qReturnArg(accepted), manager,
                                          QStringLiteral("12")));
        QVERIFY(accepted);
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QCOMPARE(binding.property("queuedManagerVariants").toList().size(), 2);
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_COMPARE(binding.property("queuedManagerVariants").toList().size(), 1);
        QCOMPARE(store->read(nativePath(target))[0].id, std::uint64_t{13});
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("1"));
        QVERIFY(
            binding.addQueuedManagerVariant(manager, "Imported", {}, {}, "Manual", "original", "Third lyric", false));
        QTRY_COMPARE(binding.queuedManagerVariants().size(), 2);
        QCOMPARE(store->read(nativePath(target)).back().id, std::uint64_t{14});
        QCOMPARE(binding.plainText(), QStringLiteral("Playing lyric"));
    }
    void queuedManagerRejectsRelocatedOccurrencesAndFreshlyChangedCatalogs() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto target = temp.filePath("manual.flac");
        const auto path = target;
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(path)] = {{8, "Before", {}, {}, "Manual", "Before lyric"}};
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setQueueLyricsSource([&](const QString&) { return target; });
        QVERIFY(binding.metaObject()->indexOfMethod("openQueuedManager(QString)") >= 0);
        QString manager;
        QVERIFY(
            QMetaObject::invokeMethod(&binding, "openQueuedManager", qReturnArg(manager), QStringLiteral("queued")));
        QTRY_COMPARE(binding.property("queuedManagerVariants").toList().size(), 1);
        QVariantMap draft;
        QVERIFY(QMetaObject::invokeMethod(&binding, "prepareQueuedVariantEdit", qReturnArg(draft), manager,
                                          QStringLiteral("8")));
        const auto edit = draft.value("token").toString();
        QVERIFY(!edit.isEmpty());
        target = temp.filePath("relocated.flac");
        QVERIFY(!binding.saveVariantEdit(edit, "Stale", {}, {}, "original", "Stale lyric", false, false));
        bool accepted = false;
        QVERIFY(QMetaObject::invokeMethod(&binding, "removeQueuedVariant", qReturnArg(accepted), manager,
                                          QStringLiteral("8")));
        QVERIFY(!accepted);
        QCOMPARE(store->save_calls.load(), 0);
        target = path;
        QVERIFY(
            QMetaObject::invokeMethod(&binding, "openQueuedManager", qReturnArg(manager), QStringLiteral("queued")));
        QTRY_COMPARE(binding.property("queuedManagerVariants").toList().size(), 1);
        {
            std::lock_guard lock(store->mutex);
            store->disk[nativePath(path)].front().content = "Newer on disk";
        }
        QVERIFY(QMetaObject::invokeMethod(&binding, "removeQueuedVariant", qReturnArg(accepted), manager,
                                          QStringLiteral("8")));
        QVERIFY(accepted);
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QCOMPARE(store->save_calls.load(), 0);
        QCOMPARE(store->read(nativePath(path)).front().content, std::string{"Newer on disk"});
        QVERIFY(QMetaObject::invokeMethod(&binding, "reloadQueuedManager", qReturnArg(accepted), manager));
        QVERIFY(accepted);
        QTRY_VERIFY(!binding.variantsBusy() && !binding.variantsHasPendingChange());
        QVERIFY(QMetaObject::invokeMethod(&binding, "prepareQueuedVariantEdit", qReturnArg(draft), manager,
                                          QStringLiteral("8")));
        QCOMPARE(draft.value("content").toString(), QStringLiteral("Newer on disk"));
    }
    void editSavedVersionPreservesTimesAndOriginAndCommitsAfterSaved() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("manual.mp3");
        const auto other = directory.filePath("other.mp3");
        save(path, "untouched audio");
        save(directory.filePath("manual.lrc"), "[00:00.000]Sibling");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(path)] = {{42, "Reviewed", "ja", "Jpan", "LRCLIB", "[00:01.000]One\n[00:04.000]Two",
                                          LyricsVariantKind::Original, false}};
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QVERIFY(binding.metaObject()->indexOfMethod("prepareVariantEdit(QString)") >= 0);
        auto prepare = [&]() {
            QVariantMap snapshot;
            QMetaObject::invokeMethod(&binding, "prepareVariantEdit", Q_RETURN_ARG(QVariantMap, snapshot),
                                      Q_ARG(QString, "42"));
            return snapshot.value("token").toString();
        };
        auto submit = [&](const QString& token, const QString& lrc, const bool as_new, const bool edit_timing) {
            bool accepted{};
            QMetaObject::invokeMethod(&binding, "saveVariantEdit", Q_RETURN_ARG(bool, accepted), Q_ARG(QString, token),
                                      Q_ARG(QString, "Corrected"), Q_ARG(QString, "en"), Q_ARG(QString, "Latn"),
                                      Q_ARG(QString, "original"), Q_ARG(QString, lrc), Q_ARG(bool, as_new),
                                      Q_ARG(bool, edit_timing));
            return accepted;
        };
        binding.setTrack(path, "Manual", "Artist", {}, 5000, "now");
        QTRY_VERIFY(binding.variantsCanEdit());
        QVERIFY(binding.selectVariant("42"));
        binding.setPosition(2000);
        const auto token = prepare();
        QVERIFY(!token.isEmpty());
        QVERIFY(!submit(token, "[00:02.000]Unapproved new time\n[00:04.000]Two", false, false));
        QCOMPARE(store->save_calls.load(), 0);
        store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(submit(token, "[00:01.000]Fixed\n[00:04.000]Two", false, false));
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QCOMPARE(binding.currentLine(), QString("One"));
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_COMPARE(binding.currentLine(), QString("Fixed"));
        QCOMPARE(store->read(nativePath(path)).front().id, 42ULL);
        QCOMPARE(store->read(nativePath(path)).front().source, "LRCLIB");
        const auto copy = prepare();
        QVERIFY(submit(copy, "[00:02.000]Fixed\n[00:04.000]Two", true, true));
        QTRY_COMPARE(binding.variants().size(), 2);
        QCOMPARE(store->read(nativePath(path)).back().source, "LRCLIB");
        const auto stale = prepare();
        binding.setTrack(other, "Other", "Artist", {}, 5000, "next");
        QTRY_VERIFY(binding.variantsCanEdit());
        QVERIFY(!submit(stale, "stale content", false, false));
        QCOMPARE(store->read(nativePath(path)).size(), 2U);
        QFile audio(path);
        QVERIFY(audio.open(QIODevice::ReadOnly));
        QCOMPARE(audio.readAll(), QByteArray("untouched audio"));
        QFile sibling(directory.filePath("manual.lrc"));
        QVERIFY(sibling.open(QIODevice::ReadOnly));
        QCOMPARE(sibling.readAll(), QByteArray("[00:00.000]Sibling"));
    }
    void recordingSearchKeepsTheSavedLyricUntilAnExactRecordingAndFormatAreUsed() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath("current.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(path)] = {{12, "Current", {}, {}, "Manual", "Current lyric"}};
        OnlineNetwork network;
        network.body = R"([
            {"id":81,"trackName":"Same title","artistName":"Other artist","albumName":"Other album","duration":200,
             "instrumental":false,"plainLyrics":"Other recording","syncedLyrics":null},
            {"id":82,"trackName":"Same title","artistName":"Right artist","albumName":"Album","duration":180,
             "instrumental":false,"plainLyrics":"Chosen plain","syncedLyrics":"[00:01.000]Chosen timed"}
        ])";
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setTrack(path, "Same title", "Right artist", "Album", 180000, "first");
        QTRY_VERIFY(binding.variantsReady() && !binding.busy());
        QVERIFY(binding.selectVariant("12"));
        QCOMPARE(binding.plainText(), QStringLiteral("Current lyric"));
        QCOMPARE(network.requests, 0);
        QCOMPARE(binding.property("album").toString(), QStringLiteral("Album"));
        QCOMPARE(binding.property("durationMs").toLongLong(), qint64{180000});

        QVERIFY(binding.metaObject()->indexOfProperty("recordingCandidates") >= 0);
        bool accepted = false;
        QVERIFY(QMetaObject::invokeMethod(&binding, "searchRecordings", qReturnArg(accepted),
                                          QStringLiteral("Same title"), QStringLiteral("Right artist"),
                                          QStringLiteral("Album"), qint64{180000}));
        QVERIFY(accepted);
        QVERIFY(binding.property("recordingSearchBusy").toBool());
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));
        QTRY_VERIFY(!binding.property("recordingSearchBusy").toBool());
        QCOMPARE(network.requests, 1);
        const auto candidates = binding.property("recordingCandidates").toList();
        QCOMPARE(candidates.size(), 2);
        QCOMPARE(candidates.front().toMap().value("id").toString(), QStringLiteral("82"));
        QCOMPARE(candidates.front().toMap().value("formats").toList().size(), 2);
        const auto revision = binding.property("recordingSearchRevision").toString();
        QVERIFY(!revision.isEmpty());

        QString preview;
        QVERIFY(QMetaObject::invokeMethod(&binding, "previewRecording", qReturnArg(preview), 0, 0, revision));
        QCOMPARE(preview, QStringLiteral("Chosen timed"));
        QCOMPARE(binding.plainText(), QStringLiteral("Current lyric"));
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(QMetaObject::invokeMethod(&binding, "useRecording", qReturnArg(accepted), 0, 0, revision));
        QVERIFY(accepted);
        QCOMPARE(binding.selectedVariantId(), QString{});
        QCOMPARE(binding.readerVerses().size(), 1);
        QCOMPARE(binding.readerVerses().front().toMap().value("timeMs").toLongLong(), qint64{1000});
        QSignalSpy readerChanged(&binding, &LyricsBinding::readerDocumentChanged);
        binding.setPosition(1500);
        QCOMPARE(binding.currentLine(), QStringLiteral("Chosen timed"));
        QCOMPARE(binding.activeReaderVerse(), 0);
        QCOMPARE(readerChanged.count(), 0);
        binding.setPosition(500);
        QCOMPARE(binding.activeReaderVerse(), -1);
        QCOMPARE(readerChanged.count(), 0);
        QCOMPARE(network.requests, 1);
        QTRY_VERIFY(binding.canSearch());
        network.body = "invalid fixture";
        QVERIFY(binding.searchRecordings("Same title", "Right artist", "Album", 180000));
        QTRY_VERIFY(!binding.recordingSearchBusy());
        QVERIFY(!binding.recordingSearchStatus().isEmpty());
        QCOMPARE(binding.readerVerses().front().toMap().value("text").toString(), QStringLiteral("Chosen timed"));
        QTRY_VERIFY(binding.canSearch());
        network.body = "[]";
        QVERIFY(binding.searchRecordings("Same title", {}, {}, 0));
        binding.cancelRecordingSearch();
        QCoreApplication::processEvents();
        QVERIFY(binding.recordingSearchStatus().isEmpty());
        QVERIFY(binding.recordingCandidates().isEmpty());
        QCOMPARE(binding.readerVerses().front().toMap().value("text").toString(), QStringLiteral("Chosen timed"));
        QCOMPARE(readerChanged.count(), 0);
    }
    void recordingSaveUsesTheExistingWriterAndRejectsActionsFromARevokedSearch() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath("current.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(path)] = {{12, "Current", {}, {}, "Manual", "Current lyric"}};
        OnlineNetwork network;
        network.body = R"([
            {"id":73,"trackName":"Song","artistName":"Artist","albumName":"Album","duration":50,
             "instrumental":false,"plainLyrics":"New plain text","syncedLyrics":null}
        ])";
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setTrack(path, "Song", "Artist", "Album", 50000, "first");
        QTRY_VERIFY(binding.variantsReady() && !binding.busy());
        QVERIFY(binding.selectVariant("12"));
        QVERIFY(binding.metaObject()->indexOfProperty("recordingCandidates") >= 0);
        const auto search = [&] {
            bool accepted = false;
            return QMetaObject::invokeMethod(&binding, "searchRecordings", qReturnArg(accepted), QStringLiteral("Song"),
                                             QString{}, QString{}, qint64{}) &&
                   accepted;
        };
        QVERIFY(search());
        QTRY_COMPARE(binding.property("recordingCandidates").toList().size(), 1);
        const auto first = binding.property("recordingSearchRevision").toString();
        binding.setTrack(path, "Song", "Artist", "Album", 50000, "same path, new occurrence");
        QCOMPARE(binding.property("recordingCandidates").toList().size(), 0);
        QVERIFY(first != binding.property("recordingSearchRevision").toString());
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));

        QTRY_VERIFY(service.canSearch());
        QVERIFY(search());
        QTRY_COMPARE(binding.property("recordingCandidates").toList().size(), 1);
        bool accepted = false;
        QVERIFY(QMetaObject::invokeMethod(&binding, "useRecording", qReturnArg(accepted), 0, 0, first));
        QVERIFY(!accepted);
        QVERIFY(QMetaObject::invokeMethod(&binding, "saveRecording", qReturnArg(accepted), 0, 0,
                                          QStringLiteral("Chosen"), first));
        QVERIFY(!accepted);
        QCOMPARE(store->save_calls.load(), 0);

        const auto second = binding.property("recordingSearchRevision").toString();
        store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(QMetaObject::invokeMethod(&binding, "saveRecording", qReturnArg(accepted), 0, 0,
                                          QStringLiteral("Chosen"), second));
        QVERIFY(accepted);
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));
        QCOMPARE(binding.plainText(), QStringLiteral("Current lyric"));
        QVERIFY(QMetaObject::invokeMethod(&binding, "cancelRecordingSearch"));
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_VERIFY(!binding.variantsBusy() && !binding.variantsHasPendingChange());
        QCOMPARE(network.requests, 2);
        const auto saved = store->read(nativePath(path));
        QCOMPARE(saved.size(), std::size_t{2});
        QCOMPARE(saved.back().name, std::string{"Chosen"});
        QCOMPARE(saved.back().source, std::string{"LRCLIB"});
        QCOMPARE(saved.back().content, std::string{"New plain text"});
        QCOMPARE(binding.plainText(), QStringLiteral("New plain text"));
    }
    void queuedVariantKeepsTheCurrentLyricAndRetriesTheExactUnicodeAudioPath() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto current = temp.filePath("current.mp3");
        const auto target = temp.filePath(QStringLiteral("別の曲.FLAC"));
        save(target, "preserved audio bytes");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->disk[nativePath(current)] = {{12, "Current", {}, {}, "Manual", "Current lyric"}};
        store->disk[nativePath(target)] = {{45, "Original", "ja", "Jpan", "Manual", "[00:01.000]風"}};
        ReleaseBlockedStore release{store};
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setQueueLyricsSource([&](const QString& id) { return id == "second" ? target : QString{}; });
        binding.setTrack(current, "Current", {}, {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.selectVariant("12"));
        QVERIFY(binding.queueVariantAvailable());
        const auto token = binding.prepareQueuedVariant("second");
        QVERIFY(!token.isEmpty());
        QVERIFY(!token.contains(target));
        store->block_save = true;
        store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(binding.addQueuedVariant(token, "Romanizada", "ja", "Latn", "Manual", "romanized", "Kaze", false));
        QTRY_COMPARE(store->save_entered.available(), 1);
        QVERIFY(!binding.queueVariantAvailable());
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));
        QCOMPARE(binding.plainText(), QStringLiteral("Current lyric"));
        store->save_release.release();
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QVERIFY(!binding.addQueuedVariant(token, "Outra", {}, {}, "Manual", "original", "Other", false));
        QCOMPARE(store->read(nativePath(target)).size(), std::size_t{1});
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_VERIFY(!binding.variantsBusy() && !binding.variantsHasPendingChange());
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));
        QCOMPARE(binding.plainText(), QStringLiteral("Current lyric"));
        QCOMPARE(store->attempts.size(), std::size_t{2});
        QCOMPARE(store->attempts[0].first, nativePath(target));
        QCOMPARE(store->attempts[1].first, nativePath(target));
        const auto saved = store->read(nativePath(target));
        QCOMPARE(saved.size(), std::size_t{2});
        QCOMPARE(saved[0].id, std::uint64_t{45});
        QCOMPARE(saved[1].content, std::string{"Kaze"});
        QCOMPARE(saved[1].kind, LyricsVariantKind::Romanized);
        QCOMPARE(store->read(nativePath(current)).size(), std::size_t{1});
        QFile audio(target);
        QVERIFY(audio.open(QIODevice::ReadOnly));
        QCOMPARE(audio.readAll(), QByteArray("preserved audio bytes"));
    }
    void queuedVariantRejectsRemovedRelocatedCancelledAndSupersededTargets() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto target = temp.filePath("queued.ogg");
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QVERIFY(!binding.queueVariantAvailable());
        binding.setQueueLyricsSource([&](const QString& id) { return id == "queued" ? target : QString{}; });
        QVERIFY(binding.prepareQueuedVariant("unknown").isEmpty());
        auto submit = [&](const QString& token) {
            return binding.addQueuedVariant(token, "Original", {}, {}, "Manual", "original", "Text", false);
        };
        const auto relocated = binding.prepareQueuedVariant("queued");
        QVERIFY(!relocated.isEmpty());
        target = temp.filePath("different.ogg");
        QVERIFY(!submit(relocated));
        const auto removed = binding.prepareQueuedVariant("queued");
        QVERIFY(!removed.isEmpty());
        target.clear();
        QVERIFY(!submit(removed));
        target = temp.filePath("new.wav");
        const auto first = binding.prepareQueuedVariant("queued");
        const auto second = binding.prepareQueuedVariant("queued");
        QVERIFY(!first.isEmpty() && !second.isEmpty() && first != second);
        QVERIFY(!submit(first));
        binding.cancelQueuedVariant(first);
        binding.cancelQueuedVariant(second);
        QVERIFY(!submit(second));
        const auto exit_token = binding.prepareQueuedVariant("queued");
        QVERIFY(!exit_token.isEmpty());
        QVERIFY(!binding.prepareExit());
        QVERIFY(!submit(exit_token));
        QCOMPARE(store->load_calls.load(), 0);
        QCOMPARE(store->save_calls.load(), 0);
    }
    void queuedSaveFeedbackSurvivesPlaybackAdvanceBeforeTheWriteIsConfirmed() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("queued.ogg");
        auto store = std::make_shared<MemoryVariantsStore>();
        ReleaseBlockedStore release{store};
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setQueueLyricsSource([&](const QString&) { return target; });
        binding.setTrack(temp.filePath("first.mp3"), "First", {}, {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        const auto token = binding.prepareQueuedVariant("queued");
        QVERIFY(!token.isEmpty());
        store->block_save = true;
        QVERIFY(binding.addQueuedVariant(token, "Original", {}, {}, {}, "original", "Queued text", false));
        QTRY_COMPARE(store->save_entered.available(), 1);
        binding.setTrack(temp.filePath("next.mp3"), "Next", {}, {}, 9000);
        QCOMPARE(binding.property("queuedVariantStatusText").toString(), QStringLiteral("Salvando versão da letra…"));
        store->save_release.release();
        QTRY_VERIFY(binding.variantsReady() && !binding.variantsBusy());
        QCOMPARE(binding.property("queuedVariantStatusText").toString(), QStringLiteral("Versão da letra salva."));
        QCOMPARE(binding.title(), QStringLiteral("Next"));
        QCOMPARE(store->read(nativePath(target)).size(), std::size_t{1});
    }
    void oneSearchPreviewsAndSavesTheTwoDistinctOnlineDocuments() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath("song.mp3");
        save(path, "preserved audio bytes");
        auto store = std::make_shared<MemoryVariantsStore>();
        OnlineNetwork network;
        network.body =
            R"({"syncedLyrics":"[00:01.000]風\n[00:04.000]空","plainLyrics":"Kaze\nSora","instrumental":false})";
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), -1);
        QVERIFY(binding.search("Song", "Artist"));
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.onlineLyricsCandidates().size(), 2);
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), 0);
        QVERIFY(binding.hasSyncedLyrics());
        const auto candidate_property =
            binding.metaObject()->property(binding.metaObject()->indexOfProperty("onlineLyricsCandidates"));
        QSignalSpy candidates_changed(&binding, candidate_property.notifySignal());
        QVERIFY(candidates_changed.isValid());
        binding.setPosition(4500);
        QCOMPARE(binding.currentLine(), QStringLiteral("空"));
        QCOMPARE(candidates_changed.count(), 0);
        const auto synced_revision = binding.currentLyricsRevision();
        QVERIFY(!synced_revision.isEmpty());

        QVERIFY(!binding.selectOnlineLyricsCandidate(2));
        QVERIFY(binding.selectOnlineLyricsCandidate(1));
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), 1);
        QCOMPARE(binding.plainText(), QStringLiteral("Kaze\nSora"));
        QVERIFY(!binding.hasSyncedLyrics());
        QVERIFY(binding.currentLine().isEmpty());
        QVERIFY(!binding.saveCurrentLyrics("Stale", "ja", "Jpan", "original", synced_revision));
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.saveCurrentLyrics("Romanizada", "ja", "Latn", "romanized", binding.currentLyricsRevision()));
        QTRY_COMPARE(binding.variants().size(), 1);
        QVERIFY(!binding.selectedVariantId().isEmpty());
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), -1);
        QVERIFY(binding.selectVariant({}));
        QVERIFY(binding.selectOnlineLyricsCandidate(0));
        QCOMPARE(binding.currentLine(), QStringLiteral("空"));
        QVERIFY(binding.saveCurrentLyrics("Original", "ja", "Jpan", "original", binding.currentLyricsRevision()));
        QTRY_COMPARE(binding.variants().size(), 2);
        QCOMPARE(network.requests, 1);
        QCOMPARE(store->save_calls.load(), 2);
        QCOMPARE(binding.title(), QStringLiteral("Song"));
        const auto saved = store->read(nativePath(path));
        QCOMPARE(saved[0].source, std::string{"LRCLIB"});
        QCOMPARE(saved[1].source, std::string{"LRCLIB"});
        const auto plain = parseLrc(saved[0].content);
        QVERIFY(plain.document.lines.empty());
        QCOMPARE(plain.document.plain_text, std::string{"Kaze\nSora"});
        QCOMPARE(parseLrc(saved[1].content).document.lines, service.onlineDocuments()[0].lines);

        QVERIFY(binding.reloadVariants());
        QTRY_COMPARE(binding.variants().size(), 2);
        QFile audio(path);
        QVERIFY(audio.open(QIODevice::ReadOnly));
        QCOMPARE(audio.readAll(), QByteArray("preserved audio bytes"));
    }
    void onlineSaveRevisionRejectsAnOldChoiceAfterASearchOrTrackChange() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        OnlineNetwork network;
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        const auto first = temp.filePath("first.mp3");
        binding.setTrack(first, "First", "Artist", {}, 0);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.search("First", "Artist"));
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.onlineLyricsCandidates().size(), 1);
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), 0);
        const auto previous_revision = binding.currentLyricsRevision();
        QTRY_VERIFY(binding.canSearch());
        QVERIFY(binding.search("Corrected", "Artist"));
        QVERIFY(binding.onlineLyricsCandidates().isEmpty());
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), -1);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), 0);
        QVERIFY(binding.currentLyricsRevision() != previous_revision);
        QVERIFY(!binding.saveCurrentLyrics("Stale", {}, {}, "original", previous_revision));
        QCOMPARE(store->save_calls.load(), 0);
        const auto second_revision = binding.currentLyricsRevision();

        binding.setTrack(temp.filePath("second.mp3"), "Second", "Artist", {}, 0);
        QVERIFY(binding.onlineLyricsCandidates().isEmpty());
        QCOMPARE(binding.selectedOnlineLyricsCandidate(), -1);
        QVERIFY(!binding.selectOnlineLyricsCandidate(0));
        QVERIFY(!binding.saveCurrentLyrics("Wrong track", {}, {}, "original", second_revision));
        QCOMPARE(store->save_calls.load(), 0);
        QCOMPARE(network.requests, 2);
    }
    void acquiredFormsAreSavedForThePublishedTargetWithoutChangingTheCurrentTrack() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto current = temp.filePath("current.mp3"), target = temp.filePath("downloaded.mp3");
        save(current, "preserved personal audio fixture");
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(current, "Current", "Artist", {}, 0);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.appendAcquiredLyrics("job_1_lookup_1", target, acquiredDrafts()));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(binding.acquiredLyricsSave().target, target);
        QCOMPARE(binding.title(), QStringLiteral("Current"));
        QCOMPARE(binding.variants().size(), 0);
        const auto saved = store->read(nativePath(target));
        QCOMPARE(saved.size(), std::size_t{2});
        QVERIFY(saved[0].id != 0 && saved[1].id != saved[0].id);
        QCOMPARE(saved[0].content, acquiredDrafts()[0].content);
        QCOMPARE(saved[1].content, acquiredDrafts()[1].content);
        QFile audio(current);
        QVERIFY(audio.open(QIODevice::ReadOnly));
        QCOMPARE(audio.readAll(), QByteArray("preserved personal audio fixture"));
    }
    void acquisitionLoadsFreshVersionsAndPreservesTheCurrentSelection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("current.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        auto base = acquiredDrafts();
        base.resize(1);
        base[0].id = 12;
        store->disk[nativePath(target)] = base;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(target, "Current", "Artist", {}, 0);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.selectVariant("12"));
        auto fresh = base;
        fresh.push_back({21, "Other", "en", "Latn", "manual", "Another version", LyricsVariantKind::Original, false});
        {
            const std::lock_guard lock(store->mutex);
            store->disk[nativePath(target)] = fresh;
        }
        auto drafts = acquiredDrafts();
        drafts.erase(drafts.begin());
        QVERIFY(binding.appendAcquiredLyrics("job_2", target, drafts));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(binding.variants().size(), 3);
        QCOMPARE(binding.selectedVariantId(), QStringLiteral("12"));
        QCOMPARE(store->read(nativePath(target))[1], fresh[1]);
    }
    void failedAcquiredSaveRetainsTheExactCandidateAndRetriesWithoutAnotherMerge() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("downloaded.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->save_status = LyricsVariantsSaveStatus::Failed;
        store->commit_before_failure = true;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QVERIFY(binding.appendAcquiredLyrics("job_3", target, acquiredDrafts()));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::SaveFailed);
        QVERIFY(binding.variantsHasPendingChange());
        QVERIFY(binding.variantsCanRetrySave());
        QCOMPARE(store->save_calls.load(), 1);
        const auto first = store->read(nativePath(target));
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QCOMPARE(store->read(nativePath(target)), first);
        QCOMPARE(store->save_calls.load(), 2);
        QCOMPARE(store->load_calls.load(), 1);
        QVERIFY(!binding.variantsHasPendingChange());
    }
    void futureCataloguesArePreservedAndAnExplicitReloadAbandonsTheAcquisition() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        store->load_status = LyricsVariantsLoadStatus::FutureVersion;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QVERIFY(binding.appendAcquiredLyrics("job_4", temp.filePath("downloaded.mp3"), acquiredDrafts()));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::BlockedByFutureVersion);
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.variantsHasPendingChange());
        QVERIFY(binding.reloadVariants());
        QCOMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Abandoned);
        QVERIFY(!binding.variantsHasPendingChange());
        QCOMPARE(store->save_calls.load(), 0);
    }
    void divergentVersionsRequireConfirmationAndExactReplayDoesNotWriteAgain() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("downloaded.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        auto base = acquiredDrafts();
        base.resize(1);
        base[0].id = 5;
        store->disk[nativePath(target)] = base;
        auto changed = base;
        changed[0].id = 0;
        changed[0].content = "Different content";
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QVERIFY(binding.appendAcquiredLyrics("job_5_a", target, changed));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Conflict);
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.variantsHasPendingChange());
        QVERIFY(binding.reloadVariants());
        QVERIFY(binding.appendAcquiredLyrics("job_5_b", target, changed, true));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        const auto saved = store->read(nativePath(target));
        QCOMPARE(saved.size(), std::size_t{2});
        QCOMPARE(saved[0], base[0]);
        QVERIFY(binding.appendAcquiredLyrics("job_5_c", target, changed));
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Unchanged);
        QCOMPARE(store->save_calls.load(), 1);
        QCOMPARE(store->read(nativePath(target)), saved);
    }
    void theSingleWriterRemainsReservedAcrossLoadSaveAndExitPreparation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        store->block_load = true;
        store->block_save = true;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        ReleaseBlockedStore cleanup{store};
        const auto target = temp.filePath("downloaded.mp3");
        QVERIFY(binding.appendAcquiredLyrics("job_6", target, acquiredDrafts()));
        bool load_observed{};
        QTRY_VERIFY(load_observed || (load_observed = store->load_entered.tryAcquire()));
        QVERIFY(!binding.appendAcquiredLyrics("overlap", target, acquiredDrafts()));
        QVERIFY(!binding.addVariant("Manual", {}, {}, {}, "original", "Manual text", false));
        QVERIFY(!binding.prepareExit());
        store->load_release.release();
        bool save_observed{};
        QTRY_VERIFY(save_observed || (save_observed = store->save_entered.tryAcquire()));
        QVERIFY(!binding.prepareExit());
        store->save_release.release();
        QTRY_COMPARE(binding.acquiredLyricsSave().state, AcquiredLyricsSaveState::Saved);
        QTRY_VERIFY(binding.prepareExit());
        binding.beginShutdown();
        QTRY_VERIFY(binding.stopped());
    }
    void invalidAcquisitionTargetsAndOversizedDraftsNeverReachTheStore() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QVERIFY(!binding.appendAcquiredLyrics("job_7", "relative.mp3", acquiredDrafts()));
        QVERIFY(!binding.appendAcquiredLyrics("job_7", "https://example.test/audio.mp3", acquiredDrafts()));
        QVERIFY(!binding.appendAcquiredLyrics("bad/ticket", temp.filePath("valid.mp3"), acquiredDrafts()));
        auto too_large = acquiredDrafts();
        too_large[0].content = std::string(lyrics_max_input_bytes + 1, 'a');
        QVERIFY(!binding.appendAcquiredLyrics("job_7", temp.filePath("valid.mp3"), too_large));
        QCOMPARE(store->load_calls.load(), 0);
        QCOMPARE(store->save_calls.load(), 0);
    }
    void confirmedPositionSelectsTheOriginalVerseAndSeeksBackwards() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:01.00]Original first\n[00:04.00]Original second\n[00:06.00]\n");
        QtLyricsService service;
        LyricsBinding binding(service);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", "Album", 8000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.title(), QStringLiteral("Song"));
        QCOMPARE(binding.artist(), QStringLiteral("Artist"));
        QVERIFY(binding.hasSyncedLyrics());
        QCOMPARE(binding.currentLine(), QString{});
        QCOMPARE(binding.nextLine(), QStringLiteral("Original first"));
        binding.setPosition(1000);
        QCOMPARE(binding.currentLine(), QStringLiteral("Original first"));
        QCOMPARE(binding.nextLine(), QStringLiteral("Original second"));
        QSignalSpy changed(&binding, &LyricsBinding::changed);
        binding.setPosition(1500);
        QCOMPARE(changed.count(), 0); // A pause/tick within the same verse is inert.
        binding.setPosition(4500);
        QCOMPARE(binding.previousLine(), QStringLiteral("Original first"));
        QCOMPARE(binding.currentLine(), QStringLiteral("Original second"));
        binding.setPosition(2000);
        QCOMPARE(binding.currentLine(), QStringLiteral("Original first"));
        binding.setPosition(6500);
        QCOMPARE(binding.currentLine(), QString{});
    }
    void changingTracksClearsTheOldLyricsAndUpdatesSearchMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("a.lrc"), "[00:00.00]A line");
        QtLyricsService service;
        LyricsBinding binding(service);
        binding.setTrack(temp.filePath("a.mp3"), "A", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.currentLine(), QStringLiteral("A line"));
        binding.setTrack(temp.filePath("b.mp3"), "B", "Other", {}, 1000);
        QCOMPARE(binding.currentLine(), QString{});
        QCOMPARE(binding.title(), QStringLiteral("B"));
        QCOMPARE(binding.artist(), QStringLiteral("Other"));
        QTRY_COMPARE(service.status(), QtLyricsService::Status::NotFound);
        QVERIFY(!binding.statusText().isEmpty());
        binding.setTrack({}, {}, {}, {}, 0);
        QVERIFY(!binding.canSearch());
    }
    void switchingDuplicateOccurrencesRevokesOnlyThePendingOnlineSearch() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:00.00]Local verse");
        const auto path = temp.filePath("song.mp3");
        auto store = std::make_shared<MemoryVariantsStore>();
        OnlineNetwork network;
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        binding.setTrack(path, "Song", "Artist", {}, 1000, "local-1");
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("Saved", {}, {}, {}, "original", "[00:00.00]Saved verse", false));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto saved = binding.variants();
        QVERIFY(binding.search("Different recording", "Artist"));
        QCOMPARE(network.requests, 1);

        binding.setTrack(path, "Song", "Artist", {}, 2000, "local-1");
        QVERIFY(binding.busy()); // A metadata refresh belongs to the same occurrence.
        binding.setTrack(path, "Song", "Artist", {}, 2000, "local-2");
        QTRY_VERIFY(!binding.busy());
        QTRY_VERIFY(binding.canSearch());
        QCOMPARE(binding.currentLine(), QStringLiteral("Saved verse"));
        QVERIFY(binding.onlineLyricsCandidates().isEmpty());
        QCOMPARE(binding.variants(), saved);
        QCOMPARE(store->load_calls.load(), 1);
        QCOMPARE(network.requests, 1);
        QVERIFY(binding.selectVariant({}));
        QCOMPARE(binding.currentLine(), QStringLiteral("Local verse"));

        QVERIFY(binding.search("Song", "Artist"));
        QCOMPARE(network.requests, 2);
        QTRY_COMPARE(service.source(), QtLyricsService::Source::Online);
        binding.setPosition(1000);
        QCOMPARE(binding.currentLine(), QStringLiteral("Online original"));
    }
    void plainLyricsStayUnsynchronizedAndShutdownDrainsTheReader() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("plain.lrc"), "[Chorus]\nOriginal text & <markup>\n");
        QtLyricsService service;
        LyricsBinding binding(service);
        binding.setTrack(temp.filePath("plain.mp3"), "Plain", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QVERIFY(!binding.hasSyncedLyrics());
        QVERIFY(binding.plainText().contains("Original text & <markup>"));
        binding.beginShutdown();
        QTRY_VERIFY(binding.stopped());
        QVERIFY(!binding.search("Title", "Artist"));
    }
    void cancelledPlayerExitCanReloadLocalLyricsWithoutARequest() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:00.00]Local verse");
        QtLyricsService service;
        LyricsBinding binding(service);
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QVERIFY(!binding.prepareExit()); // First poll publishes an observable exit-preparation phase.
        QTRY_VERIFY_WITH_TIMEOUT(binding.prepareExit(), 1500);
        QVERIFY(!binding.canSearch());
        binding.setTrack(path, "Song", "Artist", {}, 1000);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.currentLine(), QStringLiteral("Local verse"));
        QVERIFY(binding.canSearch());
    }
    void addsAndSelectsATranslationWithItsLanguageAndLiteralText() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QtLyricsService service;
        auto store = std::make_shared<MemoryVariantsStore>();
        LyricsBinding binding(service, store);
        binding.setTrack(directory.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());

        QVERIFY(binding.addVariant("Tradução", "pt", "Latn", "Letras", "translation", "Linha traduzida", false));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto row = binding.variants().front().toMap();
        QCOMPARE(row.value("kind").toString(), "translation");
        QCOMPARE(row.value("language").toString(), "pt");
        QVERIFY(binding.selectVariant(row.value("id").toString()));
        QCOMPARE(binding.plainText(), "Linha traduzida");
    }
    void importsOriginalAndRomanizedOnlyAfterSavedAndKeepsTheirOwnTiming() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        QCOMPARE(store->load_calls.load(), 0); // Construction performs no I/O.
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QCOMPARE(store->save_calls.load(), 0);
        store->block_save = true;
        QVERIFY(binding.addVariant("Original", "ja", "Jpan", "Arquivo próprio", "original",
                                   QStringLiteral("[00:01.000]日本語\n[00:04.000]次の行"), false));
        QTRY_COMPARE(store->save_entered.available(), 1);
        QVERIFY(binding.variantsBusy());
        QVERIFY(binding.variantsHasPendingChange());
        QVERIFY(binding.variants().isEmpty());
        store->save_release.release();
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto original_id = binding.variants().front().toMap().value("id").toString();
        QVERIFY(binding.addVariant("Original", "ja", "Latn", "Transliteração revisada", "romanized",
                                   "[00:02.000]Nihongo\n[00:05.000]Tsugi no gyou", true));
        QTRY_COMPARE(binding.variants().size(), 2);
        const auto romanized = binding.variants().back().toMap();
        const auto romanized_id = romanized.value("id").toString();
        QVERIFY(original_id != romanized_id);
        QCOMPARE(romanized.value("generated").toBool(), true);
        QCOMPARE(romanized.value("script").toString(), "Latn");
        QVERIFY(binding.selectVariant(romanized_id));
        binding.setPosition(3000);
        QCOMPARE(binding.currentLine(), "Nihongo");
        QCOMPARE(binding.nextLine(), "Tsugi no gyou");
        QVERIFY(binding.selectVariant(original_id));
        QCOMPARE(binding.currentLine(), QStringLiteral("日本語"));
        binding.setPosition(4500);
        QCOMPARE(binding.currentLine(), QStringLiteral("次の行"));
        QVERIFY(!binding.selectVariant("999999"));
        QCOMPARE(binding.selectedVariantId(), original_id);
        QVERIFY(binding.selectVariant({}));
        QCOMPARE(binding.selectedVariantId(), QString{});
        QCOMPARE(binding.currentLine(), QString{});
        QCOMPARE(store->save_calls.load(), 2); // Selecting never writes or searches.
    }
    void savesTheCurrentLocalLyricsWithTheirCuesWithoutReplacingTheOriginal() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:01.000]Local one\n[00:04.000]Local two\n[00:06.000]\n");
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady() && binding.canSaveCurrentLyrics());
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.saveCurrentLyrics("My original", "en", "Latn", "original", binding.currentLyricsRevision()));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto variant = store->read(nativePath(path)).front();
        QCOMPARE(variant.source, std::string{"arquivo local"});
        const auto parsed = parseLrc(variant.content);
        QCOMPARE(parsed.error, LyricsParseError::None);
        QCOMPARE(parsed.document.lines, service.document().lines);
        QVERIFY(binding.selectVariant(binding.variants().front().toMap().value("id").toString()));
        QVERIFY(!binding.canSaveCurrentLyrics());
        QVERIFY(!binding.saveCurrentLyrics("Wrong source", "en", "Latn", "original", binding.currentLyricsRevision()));
        QCOMPARE(store->save_calls.load(), 1);
        binding.setPosition(4500);
        QCOMPARE(binding.currentLine(), "Local two");
        binding.setPosition(6500);
        QCOMPARE(binding.currentLine(), QString{});
        QFile original(temp.filePath("song.lrc"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray{"[00:01.000]Local one\n[00:04.000]Local two\n[00:06.000]\n"});
    }
    void generatedRomajiIsEditableAndPreservesOriginalCuesAndProvenance() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto lrc = temp.filePath("song.lrc");
        const auto original = QStringLiteral("[00:01.000]にほんご\n[00:01.000]テスト\n[00:04.000]\n").toUtf8();
        save(lrc, original);
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        const auto owner_thread = QThread::currentThread();
        std::atomic_bool converted_off_thread{};
        binding.setJapaneseRomanizer(
            [&](const std::vector<std::string>& lines) -> std::optional<std::vector<std::string>> {
                if (lines != std::vector<std::string>{"にほんご", "テスト"})
                    return std::nullopt;
                converted_off_thread = QThread::currentThread() != owner_thread;
                return std::vector<std::string>{"nihongo", "tesuto"};
            });
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.canGenerateRomaji());
        QSignalSpy draft(&binding, &LyricsBinding::romajiDraftReady);
        const auto token = binding.generateRomaji();
        QVERIFY(!token.isEmpty());
        QTRY_COMPARE(draft.count(), 1);
        QVERIFY(converted_off_thread);
        QCOMPARE(draft.front()[0].toString(), token);
        QCOMPARE(store->save_calls.load(), 0);
        const auto generated = draft.front()[1].toString();
        QCOMPARE(generated, "[00:01.000]nihongo\n[00:01.000]tesuto\n[00:04.000]\n");
        QVERIFY(!binding.saveRomaji(token, "Romaji", "[00:02.000]nihongo\n[00:04.000]\n"));
        auto edited = generated;
        edited.replace("nihongo", "Nihongo");
        QVERIFY(binding.saveRomaji(token, "Edited Romaji", edited));
        QTRY_COMPARE(binding.variants().size(), 1);
        QVERIFY(!binding.saveRomaji(token, "Duplicate", edited));
        const auto saved = store->read(nativePath(path)).front();
        QCOMPARE(saved.kind, LyricsVariantKind::Romanized);
        QCOMPARE(saved.language, "ja");
        QCOMPARE(saved.script, "Latn");
        QCOMPARE(saved.source, "pykakasi");
        QVERIFY(saved.generated);
        QCOMPARE(saved.content, edited.toStdString());
        QFile source(lrc);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
    }
    void generatedRomajiUsesTheCurrentJapaneseVariantAndDropsStaleWork() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        QSemaphore entered, release;
        LyricsBinding binding(service, store);
        binding.setJapaneseRomanizer(
            [&](const std::vector<std::string>& lines) -> std::optional<std::vector<std::string>> {
                entered.release();
                if (!release.tryAcquire(1, 3000) || lines != std::vector<std::string>{"日本", "音楽"})
                    return std::nullopt;
                return std::vector<std::string>{"nihon", "ongaku"};
            });
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("Original", "ja", "Jpan", "Manual", "original", "日本\n\n音楽", false));
        QTRY_VERIFY(binding.canGenerateRomaji());
        QSignalSpy draft(&binding, &LyricsBinding::romajiDraftReady);
        const auto stale = binding.generateRomaji();
        QVERIFY(!stale.isEmpty());
        QTRY_COMPARE(entered.available(), 1);
        QVERIFY(binding.selectVariant({}));
        release.release();
        const auto id = binding.variants().front().toMap().value("id").toString();
        QVERIFY(binding.selectVariant(id));
        QTRY_VERIFY(binding.canGenerateRomaji());
        QCOMPARE(draft.count(), 0);
        QVERIFY(!binding.saveRomaji(stale, "Stale", "nihon"));
        const auto current = binding.generateRomaji();
        QVERIFY(!current.isEmpty());
        release.release();
        QTRY_COMPARE(draft.count(), 1);
        QCOMPARE(draft.front()[1].toString(), "nihon\n\nongaku");
        QCOMPARE(store->save_calls.load(), 1);
    }
    void romajiReportsAnUnavailableComponentOnlyForJapaneseLyrics() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("japanese.lrc"), QStringLiteral("ひらがな").toUtf8());
        save(temp.filePath("chinese.lrc"), QStringLiteral("中文").toUtf8());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(temp.filePath("japanese.mp3"), "Japanese", {}, {}, 9000);
        QTRY_VERIFY(binding.canSaveCurrentLyrics());
        QVERIFY(!binding.canGenerateRomaji());
        QVERIFY(!binding.romajiStatusText().isEmpty());
        QVERIFY(binding.generateRomaji().isEmpty());
        binding.setTrack(temp.filePath("chinese.mp3"), "Chinese", {}, {}, 9000);
        QTRY_VERIFY(binding.canSaveCurrentLyrics());
        QVERIFY(binding.romajiSourceRevision().isEmpty());
        QVERIFY(binding.romajiStatusText().isEmpty());
    }
    void anOnlineResultIsPersistedOnlyByAnExplicitSaveWithItsSourceAndCues() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        OnlineNetwork network;
        QtLyricsService service(&network);
        LyricsBinding binding(service, store);
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QCOMPARE(network.requests, 0);
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.search("Song", "Artist"));
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(network.requests, 1);
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(binding.canSaveCurrentLyrics());
        QVERIFY(
            binding.saveCurrentLyrics("Online original", "en", "Latn", "original", binding.currentLyricsRevision()));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto saved = store->read(nativePath(path)).front();
        QCOMPARE(saved.source, std::string{"LRCLIB"});
        QCOMPARE(parseLrc(saved.content).document.lines, service.document().lines);
        QCOMPARE(network.requests, 1);
        QCOMPARE(store->save_calls.load(), 1);
        const auto saved_id = binding.variants().front().toMap().value("id").toString();
        QVERIFY(binding.selectVariant(saved_id));
        QTRY_VERIFY_WITH_TIMEOUT(service.canSearch(), 1500);
        QVERIFY(binding.search("Another explicit query", "Artist"));
        QCOMPARE(binding.selectedVariantId(), saved_id);
        QVERIFY(!binding.search("Rejected while busy", "Artist"));
        QCOMPARE(binding.selectedVariantId(), saved_id);
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
        QCOMPARE(binding.selectedVariantId(),
                 QString{}); // A successful explicit direct lookup may replace the display.
        QCOMPARE(network.requests, 2);
        QCOMPARE(store->save_calls.load(), 1);
    }
    void aFailedSaveKeepsItsCandidateAndExitBlockedUntilExplicitRetry() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        store->save_status = LyricsVariantsSaveStatus::Failed;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("Romanized", "ja", "Latn", "Manual", "romanized", "Original text", false));
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QVERIFY(binding.variants().isEmpty());
        QVERIFY(!binding.variantsCanEdit());
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.retryVariantsSave()); // Exit closed admission.
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsCanRetrySave());
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_COMPARE(binding.variants().size(), 1);
        QCOMPARE(store->save_calls.load(), 2);
        QVERIFY(!binding.variantsHasPendingChange());
        QCOMPARE(store->attempts[0].second, store->attempts[1].second);
        QVERIFY(!binding.prepareExit());
        QTRY_VERIFY(binding.prepareExit());
    }
    void changingTracksDropsAStaleLoadAndSchedulesOnlyTheLatestTrack() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        store->block_load = true;
        const auto a = temp.filePath("a.mp3"), b = temp.filePath("b.mp3"), c = temp.filePath("c.mp3");
        store->disk[nativePath(a)] = {{1, "A", "ja", "Jpan", "Manual", "A only"}};
        store->disk[nativePath(c)] = {{3, "C", "ja", "Latn", "Manual", "C only"}};
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(a, "A", "Artist", {}, 9000);
        QTRY_COMPARE(store->load_entered.available(), 1);
        binding.setTrack(b, "B", "Artist", {}, 9000);
        binding.setTrack(c, "C", "Artist", {}, 9000);
        QVERIFY(binding.variants().isEmpty());
        store->load_release.release();
        QTRY_VERIFY(binding.variantsReady());
        QCOMPARE(binding.variants().size(), 1);
        QCOMPARE(binding.variants().front().toMap().value("name").toString(), "C");
        QCOMPARE(store->load_calls.load(), 2);
        QVERIFY(binding.selectVariant("3"));
        QCOMPARE(binding.plainText(), "C only");
    }
    void aFailedSaveForThePreviousTrackCannotBeLostOrPublishedOnTheCurrentOne() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        const auto a = temp.filePath("a.mp3"), b = temp.filePath("b.mp3");
        binding.setTrack(a, "A", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        store->block_save = true;
        store->save_status = LyricsVariantsSaveStatus::Failed;
        QVERIFY(binding.addVariant("A original", "ja", "Jpan", "Manual", "original", "A only", false));
        QTRY_COMPARE(store->save_entered.available(), 1);
        binding.setTrack(b, "B", "Artist", {}, 9000);
        store->save_release.release();
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QVERIFY(binding.variants().isEmpty());
        QVERIFY(!binding.variantsCanEdit());
        QVERIFY(binding.variantsStatusText().contains("outra faixa"));
        store->save_status = LyricsVariantsSaveStatus::Saved;
        QVERIFY(binding.retryVariantsSave());
        QTRY_VERIFY(binding.variantsReady() && !binding.variantsHasPendingChange());
        QVERIFY(binding.variants().isEmpty());
        QCOMPARE(store->read(nativePath(a)).size(), std::size_t{1});
        QVERIFY(store->read(nativePath(b)).empty());
        QCOMPARE(store->attempts[0].first, nativePath(a));
        QCOMPARE(store->attempts[1].first, nativePath(a));
    }
    void reloadingAnAmbiguousSaveReadsTheActualDiskStateWithoutClaimingRollback() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        store->save_status = LyricsVariantsSaveStatus::Failed;
        store->commit_before_failure = true;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("Original", "ja", "Jpan", "Manual", "original", "Saved physically", false));
        QTRY_VERIFY(binding.variantsCanRetrySave());
        QVERIFY(binding.reloadVariants());
        QTRY_VERIFY(binding.variantsReady() && !binding.variantsBusy());
        QCOMPARE(binding.variants().size(), 1);
        QVERIFY(!binding.variantsHasPendingChange());
        QCOMPARE(store->save_calls.load(), 1);
    }
    void futureDocumentsAndThrownLoadsBlockEditsWhilePlaybackLyricsRemainUsable() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        save(temp.filePath("song.lrc"), "[00:00.000]Still available");
        auto store = std::make_shared<MemoryVariantsStore>();
        store->load_status = LyricsVariantsLoadStatus::FutureVersion;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_COMPARE(store->load_calls.load(), 1);
        QTRY_VERIFY(!binding.variantsBusy());
        QTRY_COMPARE(binding.currentLine(), "Still available");
        QVERIFY(!binding.variantsReady());
        QVERIFY(!binding.variantsCanEdit());
        QVERIFY(!binding.addVariant("New", "ja", "Latn", "Manual", "original", "New text", false));
        QCOMPARE(store->save_calls.load(), 0);
        QVERIFY(!binding.variantsStatusText().isEmpty());
        store->throw_load = true;
        QVERIFY(binding.reloadVariants());
        QTRY_VERIFY(!binding.variantsBusy());
        QVERIFY(!binding.variantsReady());
        QCOMPARE(binding.currentLine(), "Still available");
    }
    void plainOnlyVariantsCanBeAddedLaterAndRemovingOneNeverRemovesAnother() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("Latin", "ja", "Latn", "Manual", "romanized", "Only Latin\n<not markup>", false));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto first = binding.variants().front().toMap().value("id").toString();
        QVERIFY(binding.selectVariant(first));
        QVERIFY(!binding.hasSyncedLyrics());
        QCOMPARE(binding.plainText(), "Only Latin\n<not markup>");
        QVERIFY(binding.addVariant("Native", "ja", "Jpan", "Manual", "original", QStringLiteral("日本語"), false));
        QTRY_COMPARE(binding.variants().size(), 2);
        const auto second = binding.variants().back().toMap().value("id").toString();
        QVERIFY(binding.removeVariant(first));
        QTRY_COMPARE(binding.variants().size(), 1);
        QCOMPARE(binding.variants().front().toMap().value("id").toString(), second);
        QVERIFY(binding.selectedVariantId() != first);
        QVERIFY(!binding.addVariant("New", "ja", "Latn", "Manual", "unsupported", "Not enabled", false));
        QCOMPARE(store->save_calls.load(), 3);
    }
    void deletingTheHighestIdDoesNotReuseItForAnotherVersionInTheSameSession() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        QtLyricsService service;
        LyricsBinding binding(service, store);
        binding.setTrack(temp.filePath("song.mp3"), "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("A", "ja", "Jpan", "Manual", "original", "A text", false));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto first = binding.variants().front().toMap().value("id").toString();
        QVERIFY(binding.addVariant("B", "ja", "Latn", "Manual", "romanized", "B text", false));
        QTRY_COMPARE(binding.variants().size(), 2);
        const auto removed = binding.variants().back().toMap().value("id").toString();
        QVERIFY(binding.removeVariant(removed));
        QTRY_COMPARE(binding.variants().size(), 1);
        QVERIFY(binding.addVariant("C", "ja", "Latn", "Manual", "romanized", "C text", false));
        QTRY_COMPARE(binding.variants().size(), 2);
        QCOMPARE(binding.variants().front().toMap().value("id").toString(), first);
        QVERIFY(binding.variants().back().toMap().value("id").toString() != removed);
        QVERIFY(!binding.selectVariant(removed));
    }
    void aFailedCandidateCanBeAbandonedAfterClearingTheTrackWithoutWritingOrRollingBack() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<MemoryVariantsStore>();
        store->save_status = LyricsVariantsSaveStatus::Failed;
        store->commit_before_failure = true;
        QtLyricsService service;
        LyricsBinding binding(service, store);
        const auto path = temp.filePath("song.mp3");
        binding.setTrack(path, "Song", "Artist", {}, 9000);
        QTRY_VERIFY(binding.variantsReady());
        QVERIFY(binding.addVariant("Original", "ja", "Jpan", "Manual", "original", "Already written", false));
        QTRY_VERIFY(binding.variantsCanRetrySave());
        binding.setTrack({}, {}, {}, {}, 0);
        QVERIFY(binding.variantsHasPendingChange());
        QVERIFY(binding.variants().isEmpty());
        QVERIFY(!binding.variantsCanEdit());
        const auto pending_notice = binding.statusText();
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.prepareExit());
        binding.setTrack({}, {}, {}, {}, 0); // Cancelled exit reopens resolution actions.
        QVERIFY(binding.variantsCanRetrySave());
        const auto loads_before = store->load_calls.load(), saves_before = store->save_calls.load();
        const bool abandoned = binding.reloadVariants(); // No selected track exists to reload.
        QVERIFY2(
            pending_notice.contains("pendente") && abandoned,
            qPrintable(QStringLiteral("pending notice='%1'; abandon accepted=%2").arg(pending_notice).arg(abandoned)));
        QVERIFY(!binding.variantsHasPendingChange());
        QVERIFY(!binding.variantsReady());
        QVERIFY(!binding.variantsBusy());
        QCOMPARE(store->load_calls.load(), loads_before);
        QCOMPARE(store->save_calls.load(), saves_before);
        QCOMPARE(store->read(nativePath(path)).front().content, std::string{"Already written"});
        QVERIFY(!binding.prepareExit());
        QTRY_VERIFY(binding.prepareExit());
    }
};
QTEST_GUILESS_MAIN(LyricsBindingTests)
#include "LyricsBindingTests.moc"
