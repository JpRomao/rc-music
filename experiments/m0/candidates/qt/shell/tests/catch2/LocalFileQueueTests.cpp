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
    REQUIRE(queue.markUnavailable(1));
    CHECK_FALSE(queue.items()[1].available);
    CHECK_FALSE(queue.select(1));
    CHECK_FALSE(queue.select(3));
    CHECK_FALSE(queue.markUnavailable(3));
    CHECK(queue.currentIndex() == std::optional<std::size_t>{1});
    CHECK(queue.select(2));
    CHECK(queue.currentIndex() == std::optional<std::size_t>{2});
}

} // namespace
} // namespace music_player::m0::test
