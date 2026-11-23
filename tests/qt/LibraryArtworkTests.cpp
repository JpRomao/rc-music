#include "presentation/LibraryArtworkProvider.hpp"

#include <QImage>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <memory>

using namespace music_player::m0;
namespace {
class FakeExtractor final : public MetadataExtractor {
  public:
    mutable std::atomic<int> reads{0};
    MetadataReadResult read(const std::filesystem::path& path, const MetadataReadCancellation&) const override {
        ++reads;
        if (path.filename() == L"no-art.mp3")
            return TrackMetadata{.source_path = path};
        if (path.filename() == L"broken.mp3")
            return MetadataReadError{.code = MetadataReadErrorCode::InvalidData};
        // A 2x3 red thumbnail, or a 4x4 blue one for the relocated file.
        const bool moved = path.filename() == L"moved.mp3";
        const std::uint32_t width = moved ? 4 : 2, height = moved ? 4 : 3;
        auto pixels = std::make_shared<std::vector<std::uint8_t>>();
        for (std::uint32_t index = 0; index < width * height; ++index)
            pixels->insert(pixels->end(), {static_cast<std::uint8_t>(moved ? 0 : 255), 0,
                                           static_cast<std::uint8_t>(moved ? 255 : 0), 255});
        return TrackMetadata{
            .source_path = path,
            .artwork = ArtworkThumbnail{.width_pixels = width, .height_pixels = height, .rgba8888 = std::move(pixels)}};
    }
};
LibrarySnapshot snapshot(std::vector<LibraryRecord> records) { return {.records = std::move(records), .next_id = 9}; }
LibraryRecord record(std::uint64_t id, const wchar_t* path, bool available = true) {
    return {.id = id, .source_path = std::filesystem::path(path), .available = available};
}
QImage request(LibraryArtworkProvider& provider, const QString& id) {
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(id, {}));
    if (!response)
        return {};
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    if (!finished.wait(5000))
        return {};
    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
    return factory ? factory->image() : QImage{};
}
} // namespace

class LibraryArtworkTests final : public QObject {
    Q_OBJECT
  private slots:
    void theIndexListsOnlyAvailableRecords() {
        LibraryArtworkIndex index;
        QVERIFY(!index.find(1));
        index.update(snapshot({record(1, L"C:/Música/a.mp3"), record(2, L"D:/b.flac", false)}));
        QCOMPARE(index.find(1), std::optional{std::filesystem::path(L"C:/Música/a.mp3")});
        QVERIFY(!index.find(2));
        index.update(snapshot({}));
        QVERIFY(!index.find(1));
    }
    void replaceSwapsTheWholeIndex() {
        LibraryArtworkIndex index;
        const std::filesystem::path a(L"C:/Música/a.mp3"), b(L"D:/b.flac");
        index.replace({{7, a}, {9, b}});
        QCOMPARE(index.find(7), std::optional{a});
        QCOMPARE(index.find(9), std::optional{b});
        index.replace({{9, b}});
        QVERIFY(!index.find(7));
        QCOMPARE(index.find(9), std::optional{b});
    }
    void queueProviderUsesItsOwnScheme() {
        QCOMPARE(LibraryArtworkProvider::queueProviderId(), QStringLiteral("queueart"));
        QCOMPARE(LibraryArtworkProvider::queueSourceBase(), QStringLiteral("image://queueart/"));
        auto library_index = std::make_shared<LibraryArtworkIndex>();
        auto queue_index = std::make_shared<LibraryArtworkIndex>();
        queue_index->replace({{7, std::filesystem::path(L"C:/Música/capa.mp3")}});
        auto extractor = std::make_shared<FakeExtractor>();
        LibraryArtworkProvider library(library_index, extractor);
        LibraryArtworkProvider queue(queue_index, extractor);
        QCOMPARE(request(queue, QStringLiteral("7")).size(), QSize(2, 3));
        QCOMPARE(request(library, QStringLiteral("7")).size(), QSize(1, 1));
    }
    void embeddedArtIsReadOnceAndKeptInMemory() {
        auto index = std::make_shared<LibraryArtworkIndex>();
        index->update(snapshot({record(3, L"C:/Música/capa.mp3")}));
        auto extractor = std::make_shared<FakeExtractor>();
        LibraryArtworkProvider provider(index, extractor);
        QCOMPARE(LibraryArtworkProvider::sourceBase(), QStringLiteral("image://libraryart/"));
        auto image = request(provider, QStringLiteral("3"));
        QCOMPARE(image.size(), QSize(2, 3));
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(extractor->reads.load(), 1);
        QCOMPARE(request(provider, QStringLiteral("3")).size(), QSize(2, 3));
        QCOMPARE(extractor->reads.load(), 1);
    }
    void aRelocatedFileIsReadAgain() {
        auto index = std::make_shared<LibraryArtworkIndex>();
        index->update(snapshot({record(3, L"C:/Música/capa.mp3")}));
        auto extractor = std::make_shared<FakeExtractor>();
        LibraryArtworkProvider provider(index, extractor);
        QCOMPARE(request(provider, QStringLiteral("3")).size(), QSize(2, 3));
        index->update(snapshot({record(3, L"E:/Novo/moved.mp3")}));
        const auto moved = request(provider, QStringLiteral("3"));
        QCOMPARE(moved.size(), QSize(4, 4));
        QCOMPARE(moved.pixelColor(0, 0), QColor(0, 0, 255));
        QCOMPARE(extractor->reads.load(), 2);
    }
    void missingArtUnknownOrUnavailableRecordsAreTransparent() {
        auto index = std::make_shared<LibraryArtworkIndex>();
        index->update(
            snapshot({record(1, L"C:/no-art.mp3"), record(2, L"C:/broken.mp3"), record(4, L"C:/offline.mp3", false)}));
        auto extractor = std::make_shared<FakeExtractor>();
        LibraryArtworkProvider provider(index, extractor);
        for (const auto& id : {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("77"),
                               QStringLiteral("x"), QString{}}) {
            const auto image = request(provider, id);
            QCOMPARE(image.size(), QSize(1, 1));
            QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        }
        QCOMPARE(extractor->reads.load(), 2); // Unknown, unavailable and malformed IDs never read.
    }
};

QTEST_MAIN(LibraryArtworkTests)
#include "LibraryArtworkTests.moc"
