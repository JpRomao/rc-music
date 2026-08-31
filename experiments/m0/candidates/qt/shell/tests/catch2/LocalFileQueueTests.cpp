#include "m0/domain/LocalFileQueue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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
