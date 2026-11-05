#include "adapters/QtLyricsService.hpp"
#include "m0/ports/LyricsVariantsStore.hpp"
#include "presentation/LyricsBinding.hpp"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
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
class OnlineReply final : public QNetworkReply {
  public:
    OnlineReply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent) {
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
    const QByteArray bytes_{
        R"({"syncedLyrics":"[00:01.000]Online original\n[00:04.000]Online next","plainLyrics":"Online original\nOnline next","instrumental":false})"};
    qint64 offset_{};
};
class OnlineNetwork final : public QNetworkAccessManager {
  public:
    int requests{};

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice*) override {
        if (operation != GetOperation)
            return nullptr;
        ++requests;
        return new OnlineReply(request, this);
    }
};
} // namespace
class LyricsBindingTests final : public QObject {
    Q_OBJECT
  private slots:
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
        QVERIFY(binding.saveCurrentLyrics("My original", "en", "Latn", "original"));
        QTRY_COMPARE(binding.variants().size(), 1);
        const auto variant = store->read(nativePath(path)).front();
        QCOMPARE(variant.source, std::string{"arquivo local"});
        const auto parsed = parseLrc(variant.content);
        QCOMPARE(parsed.error, LyricsParseError::None);
        QCOMPARE(parsed.document.lines, service.document().lines);
        QVERIFY(binding.selectVariant(binding.variants().front().toMap().value("id").toString()));
        QVERIFY(!binding.canSaveCurrentLyrics());
        QVERIFY(!binding.saveCurrentLyrics("Wrong source", "en", "Latn", "original"));
        QCOMPARE(store->save_calls.load(), 1);
        binding.setPosition(4500);
        QCOMPARE(binding.currentLine(), "Local two");
        binding.setPosition(6500);
        QCOMPARE(binding.currentLine(), QString{});
        QFile original(temp.filePath("song.lrc"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray{"[00:01.000]Local one\n[00:04.000]Local two\n[00:06.000]\n"});
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
        QVERIFY(binding.saveCurrentLyrics("Online original", "en", "Latn", "original"));
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
        QCOMPARE(binding.selectedVariantId(), QString{}); // Accepted search reveals the requested result.
        QVERIFY(binding.selectVariant(saved_id));
        QVERIFY(!binding.search("Rejected while busy", "Artist"));
        QCOMPARE(binding.selectedVariantId(), saved_id); // A refusal preserves the selected saved version.
        QTRY_COMPARE(service.status(), QtLyricsService::Status::Ready);
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
        QVERIFY(!binding.addVariant("New", "ja", "Latn", "Manual", "translation", "Not enabled", false));
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
