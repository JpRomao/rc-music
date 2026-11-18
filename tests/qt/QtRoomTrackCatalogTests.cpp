#include "adapters/QtRoomTrackCatalog.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

using namespace music_player::m0;
namespace {

// sha256("abc")
constexpr auto abc_sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

class FakeExtractor final : public MetadataExtractor {
  public:
    [[nodiscard]] MetadataReadResult read(const std::filesystem::path& source,
                                          const MetadataReadCancellation&) const override {
        if (QThread::currentThread() != main_thread)
            read_off_main = true;
        if (fail)
            return MetadataReadError{MetadataReadErrorCode::UnsupportedFormat, {}};
        TrackMetadata metadata;
        metadata.source_path = source;
        metadata.tags = tags;
        metadata.display_title = source.stem().string();
        metadata.audio.duration_ms = 61'000;
        return metadata;
    }
    QThread* main_thread{QThread::currentThread()};
    MetadataTags tags{};
    bool fail{false};
    mutable std::atomic<bool> read_off_main{false};
};

class FakeLyricsStore final : public LyricsVariantsStore {
  public:
    [[nodiscard]] LyricsVariantsLoadResult load(const std::filesystem::path&) override { return result; }
    [[nodiscard]] LyricsVariantsSaveStatus save(const std::filesystem::path&, const LyricsVariants&) override {
        return LyricsVariantsSaveStatus::Failed;
    }
    LyricsVariantsLoadResult result{};
};

LyricsVariant variant(const std::uint64_t id, std::string content) {
    LyricsVariant v;
    v.id = id;
    v.name = "v" + std::to_string(id);
    v.content = std::move(content);
    return v;
}

} // namespace

class QtRoomTrackCatalogTests final : public QObject {
    Q_OBJECT

  private:
    QTemporaryDir dir_;
    std::filesystem::path track_;
    std::shared_ptr<FakeExtractor> extractor_;
    std::shared_ptr<FakeLyricsStore> lyrics_;

    std::optional<std::optional<RoomTrackDescription>> describe(const std::filesystem::path& source) {
        QtRoomTrackCatalog catalog{extractor_, lyrics_};
        std::optional<std::optional<RoomTrackDescription>> outcome;
        QThread* delivered_on = nullptr;
        catalog.describe(source, [&](std::optional<RoomTrackDescription> found) {
            delivered_on = QThread::currentThread();
            outcome = std::move(found);
        });
        if (!QTest::qWaitFor([&] { return outcome.has_value(); }, 5000))
            return std::nullopt;
        if (delivered_on != QThread::currentThread())
            return std::nullopt;
        return outcome;
    }

  private slots:
    void init() {
        QVERIFY(dir_.isValid());
        const QString path = QDir(dir_.path()).filePath(QStringLiteral("minha-faixa-secreta.mp3"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("abc"), qint64{3});
        file.close();
        track_ = std::filesystem::path{path.toStdWString()};
        extractor_ = std::make_shared<FakeExtractor>();
        lyrics_ = std::make_shared<FakeLyricsStore>();
    }

    void hashesAndReadsTagsOffTheCallingThread() {
        extractor_->tags.title = "CanÃ§Ã£o";
        extractor_->tags.artist = "Banda";
        extractor_->tags.album = "Disco";
        const auto outcome = describe(track_);
        QVERIFY(outcome.has_value());
        QVERIFY(outcome->has_value());
        const auto& item = (*outcome)->item;
        QCOMPARE(item.origin, RoomItemOrigin::HostTrack);
        QCOMPARE(QString::fromStdString(item.content_sha256), QString::fromLatin1(abc_sha256));
        QCOMPARE(item.size_bytes, std::uint64_t{3});
        QCOMPARE(item.summary.title, std::string{"CanÃ§Ã£o"});
        QCOMPARE(QString::fromStdString(item.summary.artist), QStringLiteral("Banda"));
        QCOMPARE(QString::fromStdString(item.summary.album), QStringLiteral("Disco"));
        QCOMPARE(item.summary.duration_ms, std::int64_t{61'000});
        QVERIFY(item.owner_fingerprint.empty());
        QVERIFY(item.added_by.empty());
        QVERIFY((*outcome)->lyrics_lrc.empty());
        QVERIFY(extractor_->read_off_main);
    }

    void neverSendsTheFileNameAndSanitizesTags() {
        extractor_->tags.artist = std::string{"Ban\x01"} + "da";
        const auto outcome = describe(track_);
        QVERIFY(outcome.has_value() && outcome->has_value());
        const auto& summary = (*outcome)->item.summary;
        QVERIFY(summary.title.empty());
        QVERIFY(summary.artist.find('\x01') == std::string::npos);
        QVERIFY(sanitizeMetadataText(summary.artist).disposition == MetadataTextDisposition::Unchanged);

        extractor_->fail = true;
        const auto untagged = describe(track_);
        QVERIFY(untagged.has_value() && untagged->has_value());
        QVERIFY((*untagged)->item.summary == RoomTrackSummary{});
        QCOMPARE(QString::fromStdString((*untagged)->item.content_sha256), QString::fromLatin1(abc_sha256));
    }

    void attachesTheLyricsTheHostChose() {
        lyrics_->result.status = LyricsVariantsLoadStatus::Loaded;
        lyrics_->result.variants = {variant(1, "[00:01.00]um"), variant(2, "[00:02.00]dois")};
        lyrics_->result.selection = {LyricsSelectionKind::Variant, 2};
        auto outcome = describe(track_);
        QVERIFY(outcome.has_value() && outcome->has_value());
        QCOMPARE(QString::fromStdString((*outcome)->lyrics_lrc), QStringLiteral("[00:02.00]dois"));

        lyrics_->result.selection = {};
        outcome = describe(track_);
        QCOMPARE(QString::fromStdString((*outcome)->lyrics_lrc), QStringLiteral("[00:01.00]um"));

        lyrics_->result.selection = {LyricsSelectionKind::Hidden, 0};
        outcome = describe(track_);
        QVERIFY((*outcome)->lyrics_lrc.empty());

        lyrics_->result.selection = {LyricsSelectionKind::Variant, 3};
        lyrics_->result.variants = {variant(3, std::string(maximum_room_lyrics_bytes + 1, 'a'))};
        outcome = describe(track_);
        QVERIFY(outcome->has_value());
        QVERIFY((*outcome)->lyrics_lrc.empty());

        lyrics_->result.variants = {variant(3, std::string(maximum_room_lyrics_bytes, 'a'))};
        outcome = describe(track_);
        QCOMPARE((*outcome)->lyrics_lrc.size(), maximum_room_lyrics_bytes);

        lyrics_->result.status = LyricsVariantsLoadStatus::Invalid;
        outcome = describe(track_);
        QVERIFY((*outcome)->lyrics_lrc.empty());
    }

    void aMissingFileIsNotDescribed() {
        const auto outcome = describe(track_.parent_path() / "nao-existe.mp3");
        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->has_value());
    }

    void destroyingTheCatalogDropsPendingCallbacks() {
        bool called = false;
        {
            QtRoomTrackCatalog catalog{extractor_, lyrics_};
            catalog.describe(track_, [&](std::optional<RoomTrackDescription>) { called = true; });
        }
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(50);
        QVERIFY(!called);
    }
};

QTEST_GUILESS_MAIN(QtRoomTrackCatalogTests)
#include "QtRoomTrackCatalogTests.moc"
