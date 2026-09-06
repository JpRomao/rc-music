#include "m0/domain/QueueNavigation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace music_player::m0::test {
namespace {

PlaybackSnapshot playingAt(const std::int64_t position_ms, const bool seekable = true) {
    return {PlaybackStatus::Playing, position_ms, 248'000, seekable};
}

constexpr std::array<QueueNavigationItem, 3> available_queue{};
constexpr std::array<QueueDirection, 2> directions{QueueDirection::Previous, QueueDirection::Next};

} // namespace

TEST_CASE("queue previous restarts only after three confirmed seconds", "[m0][qt][core][navigation]") {
    for (const std::int64_t position_ms : {0, 2'999, 3'000}) {
        CAPTURE(position_ms);
        CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Previous, playingAt(position_ms)) ==
              std::optional<std::size_t>{0});
    }

    for (const std::int64_t position_ms : {3'001, 84'000}) {
        CAPTURE(position_ms);
        CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Previous, playingAt(position_ms)) ==
              std::optional<std::size_t>{1});
    }
}

TEST_CASE("queue next selects the nearest following available occurrence", "[m0][qt][core][navigation]") {
    constexpr std::array<QueueNavigationItem, 6> queue{{{true}, {true}, {false}, {false}, {true}, {true}}};

    CHECK(queueNavigationTarget(queue, 1, QueueDirection::Next, playingAt(84'000)) == std::optional<std::size_t>{4});
    CHECK(queueNavigationTarget(queue, 4, QueueDirection::Next, playingAt(0)) == std::optional<std::size_t>{5});
}

TEST_CASE("queue previous skips unavailable occurrences before the current item", "[m0][qt][core][navigation]") {
    constexpr std::array<QueueNavigationItem, 6> queue{{{true}, {true}, {false}, {false}, {true}, {true}}};

    CHECK(queueNavigationTarget(queue, 4, QueueDirection::Previous, playingAt(3'000)) == std::optional<std::size_t>{1});
    CHECK(queueNavigationTarget(queue, 4, QueueDirection::Previous, playingAt(3'001)) == std::optional<std::size_t>{4});
}

TEST_CASE("queue navigation never wraps at either boundary", "[m0][qt][core][navigation]") {
    CHECK_FALSE(queueNavigationTarget(available_queue, 0, QueueDirection::Previous, playingAt(3'000)));
    CHECK_FALSE(queueNavigationTarget(available_queue, 2, QueueDirection::Next, playingAt(84'000)));
    CHECK(queueNavigationTarget(available_queue, 0, QueueDirection::Previous, playingAt(3'001)) ==
          std::optional<std::size_t>{0});

    constexpr std::array<QueueNavigationItem, 3> leading_unavailable{{{false}, {false}, {true}}};
    constexpr std::array<QueueNavigationItem, 3> trailing_unavailable{{{true}, {false}, {false}}};
    CHECK_FALSE(queueNavigationTarget(leading_unavailable, 2, QueueDirection::Previous, playingAt(0)));
    CHECK_FALSE(queueNavigationTarget(trailing_unavailable, 0, QueueDirection::Next, playingAt(0)));
}

TEST_CASE("a singleton queue can restart but cannot select a different occurrence", "[m0][qt][core][navigation]") {
    constexpr std::array<QueueNavigationItem, 1> queue{};

    CHECK_FALSE(queueNavigationTarget(queue, 0, QueueDirection::Previous, playingAt(3'000)));
    CHECK_FALSE(queueNavigationTarget(queue, 0, QueueDirection::Next, playingAt(0)));
    CHECK(queueNavigationTarget(queue, 0, QueueDirection::Previous, playingAt(3'001)) == std::optional<std::size_t>{0});
}

TEST_CASE("unseekable previous navigates instead of requesting a restart", "[m0][qt][core][navigation]") {
    CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Previous, playingAt(84'000, false)) ==
          std::optional<std::size_t>{0});
    CHECK_FALSE(queueNavigationTarget(available_queue, 0, QueueDirection::Previous, playingAt(84'000, false)));
    CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Next, playingAt(84'000, false)) ==
          std::optional<std::size_t>{2});
}

TEST_CASE("queue navigation requires a current occurrence inside a nonempty queue", "[m0][qt][core][navigation]") {
    for (const QueueDirection direction : directions) {
        CHECK_FALSE(queueNavigationTarget({}, std::nullopt, direction, playingAt(84'000)));
        CHECK_FALSE(queueNavigationTarget({}, 0, direction, playingAt(84'000)));
        CHECK_FALSE(queueNavigationTarget(available_queue, std::nullopt, direction, playingAt(84'000)));
        CHECK_FALSE(queueNavigationTarget(available_queue, available_queue.size(), direction, playingAt(84'000)));
        CHECK_FALSE(queueNavigationTarget(available_queue, std::numeric_limits<std::size_t>::max(), direction,
                                          playingAt(84'000)));
    }
}

TEST_CASE("queue navigation rejects inactive playback states", "[m0][qt][core][navigation]") {
    for (const PlaybackStatus status : {PlaybackStatus::Empty, PlaybackStatus::Loading, PlaybackStatus::Error}) {
        const PlaybackSnapshot snapshot{status, 84'000, 248'000, true};
        for (const QueueDirection direction : directions)
            CHECK_FALSE(queueNavigationTarget(available_queue, 1, direction, snapshot));
    }
}

TEST_CASE("queue navigation is inert while seek preview or acknowledgement is active", "[m0][qt][core][navigation]") {
    for (const PlaybackStatus status : {PlaybackStatus::Ready, PlaybackStatus::Paused, PlaybackStatus::Playing}) {
        const PlaybackSnapshot snapshot{status, 84'000, 248'000, true};
        for (const QueueDirection direction : directions)
            CHECK_FALSE(queueNavigationTarget(available_queue, 1, direction, snapshot, true));
    }
}

TEST_CASE("queue navigation uses the same targets for ready paused and playing states", "[m0][qt][core][navigation]") {
    for (const PlaybackStatus status : {PlaybackStatus::Ready, PlaybackStatus::Paused, PlaybackStatus::Playing}) {
        const PlaybackSnapshot snapshot{status, 3'000, 248'000, true};
        CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Previous, snapshot) ==
              std::optional<std::size_t>{0});
        CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Next, snapshot) ==
              std::optional<std::size_t>{2});
    }
}

TEST_CASE("queue navigation compares large positions without overflow", "[m0][qt][core][navigation]") {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const PlaybackSnapshot snapshot{PlaybackStatus::Playing, maximum, maximum, true};

    CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Previous, snapshot) ==
          std::optional<std::size_t>{1});
    CHECK(queueNavigationTarget(available_queue, 1, QueueDirection::Next, snapshot) == std::optional<std::size_t>{2});
}

} // namespace music_player::m0::test
