#include "m0/ports/SessionStore.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace music_player::m0::test {

static_assert(std::is_abstract_v<SessionStore>);

TEST_CASE("session store port defaults to an empty local session and never replaces a future version",
          "[m0][qt][core][session][FR-043][FR-044]") {
    const SessionLoadResult load;
    CHECK(load.status == SessionLoadStatus::Missing);
    CHECK(isValidSessionSnapshot(load.snapshot));
    CHECK(load.snapshot == SessionSnapshot{});

    const SessionSaveRequest save;
    CHECK(isValidSessionSnapshot(save.snapshot));
    CHECK_FALSE(save.replace_future_version);
}

} // namespace music_player::m0::test
