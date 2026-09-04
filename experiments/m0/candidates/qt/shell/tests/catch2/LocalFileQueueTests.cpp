#include "m0/domain/LocalFileQueue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <limits>
#include <vector>

namespace music_player::m0::test {
namespace {

TEST_CASE("local file queue preserves order and gives duplicate paths distinct occurrence ids",
          "[m0][qt][core][local-queue][FR-001][FR-016]") {
    LocalFileQueue queue;
    const std::filesystem::path duplicate{LR"(C:\Música\Abertura 日本語.mp3)"};
    const std::filesystem::path third{LR"(C:\Música\Final.flac)"};

    REQUIRE(queue.replace({duplicate, duplicate, third}));

    REQUIRE(queue.size() == 3);
    REQUIRE(queue.currentIndex() == std::optional<std::size_t>{0});
    REQUIRE(queue.currentOccurrenceId() == std::optional{queue.items()[0].occurrence_id});
    CHECK(queue.items()[0].source_path == duplicate);
    CHECK(queue.items()[1].source_path == duplicate);
    CHECK(queue.items()[2].source_path == third);
    CHECK(queue.items()[0].occurrence_id.value != 0);
    CHECK(queue.items()[0].occurrence_id != queue.items()[1].occurrence_id);
    CHECK(queue.items()[1].occurrence_id != queue.items()[2].occurrence_id);
    CHECK(queue.items()[0].available);
    CHECK(queue.items()[1].available);
    CHECK(queue.items()[2].available);
}

TEST_CASE("invalid replacement leaves the local file queue unchanged", "[m0][qt][core][local-queue][FR-001]") {
    LocalFileQueue queue;
    const std::filesystem::path current{LR"(C:\Music\current.wav)"};
    REQUIRE(queue.replace({current}));
    const auto occurrence = queue.items().front().occurrence_id;

    CHECK_FALSE(queue.replace({}));
    CHECK_FALSE(queue.replace({current, {}}));

    REQUIRE(queue.size() == 1);
    CHECK(queue.items().front().source_path == current);
    CHECK(queue.items().front().occurrence_id == occurrence);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{0});
}

TEST_CASE("local file queue selection rejects unavailable and out of range occurrences",
          "[m0][qt][core][local-queue][FR-011][FR-016]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Music\first.wav)", LR"(C:\Music\second.wav)", LR"(C:\Music\third.wav)"}));

    REQUIRE(queue.select(1));
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    CHECK(queue.currentOccurrenceId() == std::optional{queue.items()[1].occurrence_id});
    REQUIRE(queue.markUnavailable(1));
    CHECK_FALSE(queue.items()[1].available);
    CHECK_FALSE(queue.select(1));
    CHECK_FALSE(queue.select(3));
    CHECK_FALSE(queue.markUnavailable(3));
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    CHECK(queue.currentOccurrenceId() == std::optional{queue.items()[1].occurrence_id});
    CHECK(queue.select(2));
    CHECK(queue.currentIndex() == std::optional<std::size_t>{2});
    CHECK(queue.currentOccurrenceId() == std::optional{queue.items()[2].occurrence_id});
    REQUIRE(queue.clearSelection());
    CHECK_FALSE(queue.currentIndex().has_value());
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
    CHECK(queue.size() == 3);
    CHECK_FALSE(queue.clearSelection());
}

TEST_CASE("local file queue appends valid paths atomically without changing the current occurrence",
          "[m0][qt][core][local-queue][FR-001][FR-016]") {
    LocalFileQueue queue;
    const std::filesystem::path duplicate{LR"(C:\Music\Duplicada 日本語.mp3)"};
    REQUIRE(queue.replace({duplicate, LR"(C:\Music\Segunda.flac)"}));
    REQUIRE(queue.select(1));
    const auto current = queue.currentOccurrenceId();
    const auto first_duplicate_id = queue.items().front().occurrence_id;

    REQUIRE(queue.append({duplicate, LR"(C:\Music\Final.ogg)"}));

    REQUIRE(queue.size() == 4);
    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    CHECK(queue.items()[2].source_path == duplicate);
    CHECK(queue.items()[2].occurrence_id != first_duplicate_id);
    CHECK(queue.items()[3].source_path == std::filesystem::path{LR"(C:\Music\Final.ogg)"});

    CHECK_FALSE(queue.append({}));
    CHECK_FALSE(queue.append({LR"(C:\Music\Aceita.wav)", {}}));
    CHECK(queue.size() == 4);
    CHECK(queue.currentOccurrenceId() == current);
}

TEST_CASE("retry selection targets one unavailable occurrence without claiming recovery",
          "[m0][qt][core][local-queue][retry][NFR-REL-005]") {
    LocalFileQueue queue;
    const std::filesystem::path duplicate{LR"(C:\Música\Duplicada 日本語.mp3)"};
    REQUIRE(queue.replace({duplicate, duplicate, LR"(C:\Music\Final.wav)"}));
    REQUIRE(queue.markUnavailable(1));
    const auto before = queue.snapshot();

    REQUIRE(queue.selectUnavailableForRetry(1));

    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    CHECK(queue.currentOccurrenceId() == std::optional{before.items[1].occurrence_id});
    CHECK(queue.snapshot().items == before.items);
    CHECK(queue.items()[0].available);
    CHECK_FALSE(queue.items()[1].available);
    CHECK(queue.items()[2].available);
    CHECK_FALSE(queue.select(1));
    CHECK(queue.selectUnavailableForRetry(1));
    CHECK_FALSE(queue.items()[1].available);
}

TEST_CASE("retry selection rejects available and missing occurrences without changing selection",
          "[m0][qt][core][local-queue][retry][NFR-REL-005]") {
    LocalFileQueue queue;
    CHECK_FALSE(queue.selectUnavailableForRetry(0));
    REQUIRE(queue.replace({LR"(C:\Music\First.wav)", LR"(C:\Music\Second.wav)"}));
    REQUIRE(queue.markUnavailable(1));
    const auto before = queue.snapshot();

    CHECK_FALSE(queue.selectUnavailableForRetry(0));
    CHECK_FALSE(queue.selectUnavailableForRetry(queue.size()));
    CHECK_FALSE(queue.selectUnavailableForRetry(std::numeric_limits<std::size_t>::max()));
    CHECK(queue.snapshot() == before);
    REQUIRE(queue.clearSelection());
    CHECK_FALSE(queue.selectUnavailableForRetry(0));
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
    REQUIRE(queue.selectUnavailableForRetry(1));
    CHECK(queue.currentOccurrenceId() == std::optional{before.items[1].occurrence_id});
}

TEST_CASE("recovered availability preserves occurrence identity order paths and selection",
          "[m0][qt][core][local-queue][retry][NFR-REL-005]") {
    LocalFileQueue queue;
    CHECK_FALSE(queue.markAvailable(0));
    const std::filesystem::path duplicate{LR"(C:\Música\Duplicada 日本語.mp3)"};
    REQUIRE(queue.replace({duplicate, duplicate}));
    REQUIRE(queue.markUnavailable(0));
    REQUIRE(queue.markUnavailable(1));
    const auto before = queue.snapshot();

    REQUIRE(queue.markAvailable(1));

    CHECK(queue.snapshot() == before);
    CHECK_FALSE(queue.items()[0].available);
    CHECK(queue.items()[1].available);
    CHECK_FALSE(queue.markAvailable(1));
    CHECK_FALSE(queue.markAvailable(queue.size()));
    CHECK_FALSE(queue.markAvailable(std::numeric_limits<std::size_t>::max()));
    CHECK(queue.snapshot() == before);
    REQUIRE(queue.select(1));
    CHECK(queue.currentOccurrenceId() == std::optional{before.items[1].occurrence_id});
    REQUIRE(queue.clearSelection());
    REQUIRE(queue.markAvailable(0));
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
}

TEST_CASE("local file queue inserts a batch at a requested position without changing the current occurrence",
          "[m0][qt][core][local-queue][FR-003][FR-016]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Music\A.wav)", LR"(C:\Music\D.wav)"}));
    REQUIRE(queue.select(1));
    const auto current = queue.currentOccurrenceId();
    const auto first_id = queue.items().front().occurrence_id;
    const auto last_id = queue.items().back().occurrence_id;

    REQUIRE(queue.insert({LR"(C:\Music\B.mp3)", LR"(C:\Music\C.flac)"}, 1));

    REQUIRE(queue.size() == 4);
    CHECK(queue.items()[0].occurrence_id == first_id);
    CHECK(queue.items()[0].source_path == std::filesystem::path{LR"(C:\Music\A.wav)"});
    CHECK(queue.items()[1].source_path == std::filesystem::path{LR"(C:\Music\B.mp3)"});
    CHECK(queue.items()[2].source_path == std::filesystem::path{LR"(C:\Music\C.flac)"});
    CHECK(queue.items()[3].occurrence_id == last_id);
    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{3});
    CHECK(queue.items()[1].occurrence_id != queue.items()[2].occurrence_id);

    CHECK_FALSE(queue.insert({}, 0));
    CHECK_FALSE(queue.insert({LR"(C:\Music\Aceita.wav)", {}}, 0));
    CHECK_FALSE(queue.insert({LR"(C:\Music\Fora.wav)"}, 5));
    CHECK(queue.size() == 4);
    CHECK(queue.currentOccurrenceId() == current);
}

TEST_CASE("relocating one unavailable source preserves identity selection order and unverified availability",
          "[m0][qt][core][local-queue][relocate][NFR-REL-005]") {
    LocalFileQueue queue;
    const std::filesystem::path original{LR"(C:\Música\Antiga 日本語.mp3)"};
    const std::filesystem::path relocated{LR"(D:\Movida\Nova 日本語.mp3)"};
    REQUIRE(queue.replace({original, original, relocated}));
    REQUIRE(queue.markUnavailable(0));
    REQUIRE(queue.markUnavailable(1));
    REQUIRE(queue.select(2));
    const auto before = queue.snapshot();
    const auto occurrence = before.items[1].occurrence_id;

    REQUIRE(queue.replaceUnavailableSource(occurrence, relocated));

    CHECK(queue.size() == 3);
    CHECK(queue.currentOccurrenceId() == before.current_occurrence_id);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{2});
    CHECK(queue.snapshot().items[0] == before.items[0]);
    CHECK(queue.snapshot().items[2] == before.items[2]);
    CHECK(queue.items()[1].occurrence_id == occurrence);
    CHECK(queue.items()[1].source_path == relocated);
    CHECK_FALSE(queue.items()[0].available);
    CHECK_FALSE(queue.items()[1].available);
    CHECK(queue.items()[2].available);
    CHECK_FALSE(queue.select(1));
    REQUIRE(queue.append({LR"(C:\Music\Next.wav)"}));
    CHECK(queue.items().back().occurrence_id == LocalQueueOccurrenceId{4});
}

TEST_CASE("relocating an unavailable source preserves its current or absent selection without filesystem IO",
          "[m0][qt][core][local-queue][relocate][NFR-REL-005]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Missing\Original.wav)"}));
    REQUIRE(queue.markUnavailable(0));
    const auto occurrence = queue.items()[0].occurrence_id;
    SECTION("selected occurrence stays selected") {}
    SECTION("no selection stays absent") { REQUIRE(queue.clearSelection()); }
    const auto current = queue.currentOccurrenceId();
    const std::filesystem::path unnormalized{LR"(Z:\Missing\Unused\..\Nova 日本語.wav)"};

    REQUIRE(queue.replaceUnavailableSource(occurrence, unnormalized));

    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.items()[0].source_path == unnormalized);
    CHECK_FALSE(queue.items()[0].available);
    REQUIRE(queue.selectUnavailableForRetry(0));
    CHECK_FALSE(queue.items()[0].available);
    REQUIRE(queue.markAvailable(0));
    CHECK(queue.items()[0].available);
    CHECK(queue.items()[0].source_path == unnormalized);
}

TEST_CASE("invalid or unchanged source relocation is transactional",
          "[m0][qt][core][local-queue][relocate][NFR-REL-005]") {
    LocalFileQueue queue;
    const std::filesystem::path original{LR"(C:\Music\Original.wav)"};
    const std::filesystem::path relocated{LR"(D:\Moved\Located.wav)"};
    CHECK_FALSE(queue.replaceUnavailableSource(LocalQueueOccurrenceId{1}, relocated));
    REQUIRE(queue.replace({original, original}));
    REQUIRE(queue.markUnavailable(1));
    const auto before = queue.snapshot();

    CHECK_FALSE(queue.replaceUnavailableSource({}, relocated));
    CHECK_FALSE(queue.replaceUnavailableSource(LocalQueueOccurrenceId{999'999}, relocated));
    CHECK_FALSE(queue.replaceUnavailableSource(before.items[0].occurrence_id, relocated));
    CHECK_FALSE(queue.replaceUnavailableSource(before.items[1].occurrence_id, {}));
    CHECK_FALSE(queue.replaceUnavailableSource(before.items[1].occurrence_id, original));

    CHECK(queue.snapshot() == before);
    CHECK(queue.items()[0].available);
    CHECK_FALSE(queue.items()[1].available);
    REQUIRE(queue.append({relocated}));
    CHECK(queue.items().back().occurrence_id == LocalQueueOccurrenceId{3});
}

TEST_CASE("local file queue moves occurrences while preserving the current id", "[m0][qt][core][local-queue][FR-016]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Music\A.wav)", LR"(C:\Music\B.wav)", LR"(C:\Music\C.wav)", LR"(C:\Music\D.wav)"}));
    REQUIRE(queue.select(2));
    const auto current = queue.currentOccurrenceId();
    const auto first = queue.items()[0].occurrence_id;

    REQUIRE(queue.move(first, 3));
    CHECK(queue.items()[0].source_path == std::filesystem::path{LR"(C:\Music\B.wav)"});
    CHECK(queue.items()[1].source_path == std::filesystem::path{LR"(C:\Music\C.wav)"});
    CHECK(queue.items()[3].source_path == std::filesystem::path{LR"(C:\Music\A.wav)"});
    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});

    REQUIRE(queue.move(*current, 3));
    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{3});
    CHECK_FALSE(queue.move(*current, 3));
    CHECK_FALSE(queue.move(LocalQueueOccurrenceId{999'999}, 0));
    CHECK_FALSE(queue.move(first, 4));
}

TEST_CASE("local file queue removals preserve unrelated ids and clearing never reuses an occurrence id",
          "[m0][qt][core][local-queue][FR-016]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Music\A.wav)", LR"(C:\Music\B.wav)", LR"(C:\Music\C.wav)", LR"(C:\Music\D.wav)"}));
    REQUIRE(queue.select(2));
    const auto current = queue.currentOccurrenceId();
    const auto highest_initial_id = queue.items().back().occurrence_id.value;
    const auto before = queue.items()[0].occurrence_id;
    const auto after = queue.items()[3].occurrence_id;

    REQUIRE(queue.remove(before));
    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    REQUIRE(queue.remove(after));
    CHECK(queue.currentOccurrenceId() == current);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    REQUIRE(queue.remove(*current));
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
    CHECK_FALSE(queue.currentIndex().has_value());
    CHECK(queue.size() == 1);
    CHECK_FALSE(queue.remove(*current));

    REQUIRE(queue.clear());
    CHECK(queue.empty());
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
    CHECK_FALSE(queue.clear());
    REQUIRE(queue.append({LR"(C:\Music\Nova.wav)"}));
    REQUIRE(queue.size() == 1);
    CHECK(queue.items().front().occurrence_id.value > highest_initial_id);
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
}

} // namespace
} // namespace music_player::m0::test
