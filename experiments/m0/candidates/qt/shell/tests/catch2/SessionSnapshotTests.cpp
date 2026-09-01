#include "m0/domain/SessionSnapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

[[nodiscard]] std::vector<LocalFileQueueItem> copyItems(const LocalFileQueue& queue) {
    return {queue.items().begin(), queue.items().end()};
}

void checkItemsEqual(const std::vector<LocalFileQueueItem>& expected, const LocalFileQueue& actual) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(actual.items()[index].occurrence_id == expected[index].occurrence_id);
        CHECK(actual.items()[index].source_path == expected[index].source_path);
        CHECK(actual.items()[index].available == expected[index].available);
    }
}

[[nodiscard]] SessionSnapshot validSnapshot() {
    const std::filesystem::path duplicate{LR"(C:\Música\Abertura 日本語.mp3)"};
    return {
        .queue =
            {
                .items =
                    {
                        {{91}, duplicate},
                        {{7}, LR"(C:\Música\Final.flac)"},
                        {{42}, duplicate},
                    },
                .current_occurrence_id = LocalQueueOccurrenceId{42},
            },
        .position_ms = 123'456,
    };
}

TEST_CASE("session snapshot restores duplicate paths by occurrence id without transient availability",
          "[m0][qt][core][session][FR-043]") {
    const auto persisted = validSnapshot();
    REQUIRE(isValidSessionSnapshot(persisted));

    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Old\Unavailable.wav)"}));
    REQUIRE(queue.markUnavailable(0));

    REQUIRE(queue.restore(persisted.queue));

    CHECK(queue.snapshot() == persisted.queue);
    CHECK(queue.currentIndex() == std::optional<std::size_t>{2});
    CHECK(queue.currentOccurrenceId() == persisted.queue.current_occurrence_id);
    CHECK(std::ranges::all_of(queue.items(), &LocalFileQueueItem::available));
    CHECK(persisted.position_ms == 123'456);
}

TEST_CASE("invalid queue snapshots are rejected transactionally", "[m0][qt][core][session][FR-043][NFR-REL-003]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Current\A.wav)", LR"(C:\Current\B.wav)"}));
    REQUIRE(queue.select(1));
    REQUIRE(queue.markUnavailable(0));
    const auto before_items = copyItems(queue);
    const auto before_current = queue.currentOccurrenceId();

    auto invalid = validSnapshot().queue;
    SECTION("zero occurrence id") { invalid.items[0].occurrence_id = {}; }
    SECTION("duplicate occurrence id") { invalid.items[1].occurrence_id = invalid.items[0].occurrence_id; }
    SECTION("empty source path") { invalid.items[1].source_path.clear(); }
    SECTION("missing current occurrence") { invalid.current_occurrence_id = LocalQueueOccurrenceId{999}; }
    SECTION("empty queue with a current occurrence") { invalid.items.clear(); }

    CHECK_FALSE(isValidLocalFileQueueSnapshot(invalid));
    CHECK_FALSE(isValidSessionSnapshot({.queue = invalid, .position_ms = 0}));
    CHECK_FALSE(queue.restore(std::move(invalid)));
    checkItemsEqual(before_items, queue);
    CHECK(queue.currentOccurrenceId() == before_current);
    REQUIRE(queue.append({LR"(C:\Current\C.wav)"}));
    CHECK(queue.items().back().occurrence_id == LocalQueueOccurrenceId{3});
}

TEST_CASE("session snapshot validation rejects impossible positions", "[m0][qt][core][session][FR-043]") {
    auto snapshot = validSnapshot();
    SECTION("negative position") { snapshot.position_ms = -1; }
    SECTION("position without a current occurrence") { snapshot.queue.current_occurrence_id.reset(); }

    CHECK_FALSE(isValidSessionSnapshot(snapshot));
}

TEST_CASE("empty and unselected session snapshots require position zero", "[m0][qt][core][session][FR-043]") {
    const SessionSnapshot empty{};
    CHECK(isValidSessionSnapshot(empty));

    auto unselected = validSnapshot();
    unselected.queue.current_occurrence_id.reset();
    unselected.position_ms = 0;
    CHECK(isValidSessionSnapshot(unselected));
    LocalFileQueue unselected_queue;
    REQUIRE(unselected_queue.restore(unselected.queue));
    CHECK(unselected_queue.size() == unselected.queue.items.size());
    CHECK_FALSE(unselected_queue.currentOccurrenceId().has_value());

    auto maximum_position = validSnapshot();
    maximum_position.position_ms = std::numeric_limits<std::int64_t>::max();
    CHECK(isValidSessionSnapshot(maximum_position));
}

TEST_CASE("restoring an empty queue snapshot clears existing runtime state", "[m0][qt][core][session][FR-043]") {
    LocalFileQueue queue;
    REQUIRE(queue.replace({LR"(C:\Música\A.wav)", LR"(C:\Música\B.wav)"}));
    REQUIRE(queue.markUnavailable(1));

    REQUIRE(queue.restore({}));

    CHECK(queue.empty());
    CHECK_FALSE(queue.currentOccurrenceId().has_value());
    CHECK(queue.snapshot() == LocalFileQueueSnapshot{});
    REQUIRE(queue.append({LR"(C:\Música\Nova.wav)"}));
    CHECK(queue.items().front().occurrence_id.value > 0);
}

TEST_CASE("append after wrapped restored ids chooses only free positive occurrence ids",
          "[m0][qt][core][session][FR-043]") {
    LocalFileQueue queue;
    const LocalFileQueueSnapshot persisted{
        .items =
            {
                {{1}, LR"(C:\Música\Um.wav)"},
                {{3}, LR"(C:\Música\Três.wav)"},
                {{std::numeric_limits<std::uint64_t>::max()}, LR"(C:\Música\Máximo.wav)"},
            },
        .current_occurrence_id = LocalQueueOccurrenceId{3},
    };
    REQUIRE(queue.restore(persisted));

    REQUIRE(queue.append({LR"(C:\Música\Dois.wav)", LR"(C:\Música\Quatro.wav)"}));

    REQUIRE(queue.size() == 5);
    CHECK(queue.items()[3].occurrence_id == LocalQueueOccurrenceId{2});
    CHECK(queue.items()[4].occurrence_id == LocalQueueOccurrenceId{4});
    CHECK(queue.currentOccurrenceId() == persisted.current_occurrence_id);
}

TEST_CASE("queue snapshot omits runtime availability and restore revalidates every source",
          "[m0][qt][core][session][FR-043]") {
    LocalFileQueue source;
    REQUIRE(source.replace({LR"(C:\Música\A.wav)", LR"(D:\Removível\B.flac)"}));
    REQUIRE(source.select(1));
    REQUIRE(source.markUnavailable(1));
    const auto persisted = source.snapshot();

    LocalFileQueue restored;
    REQUIRE(restored.restore(persisted));

    CHECK(restored.snapshot() == persisted);
    CHECK(restored.currentOccurrenceId() == persisted.current_occurrence_id);
    CHECK(std::ranges::all_of(restored.items(), &LocalFileQueueItem::available));
}

} // namespace
} // namespace music_player::m0::test
