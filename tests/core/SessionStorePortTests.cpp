#include "m0/application/SessionStartup.hpp"
#include "m0/ports/SessionStore.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <type_traits>
#include <utility>

namespace music_player::m0::test {
namespace {

class CountingSessionStore final : public SessionStore {
  public:
    explicit CountingSessionStore(SessionLoadResult load_result) : load_result_{std::move(load_result)} {}

    SessionLoadResult load() override {
        ++load_calls;
        return load_result_;
    }

    SessionSaveStatus save(const SessionSaveRequest&) override { return SessionSaveStatus::Saved; }

    std::size_t load_calls{0};

  private:
    SessionLoadResult load_result_;
};

[[nodiscard]] SessionSnapshot startupSnapshot() {
    return {
        .queue =
            {
                .items = {{{17}, std::filesystem::path{LR"(C:\Music\opening.flac)"}}},
                .current_occurrence_id = LocalQueueOccurrenceId{17},
            },
        .position_ms = 12'345,
    };
}

} // namespace

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

TEST_CASE("startup does not load a session when restore-last-session is disabled",
          "[m0][qt][core][session][startup][FR-043]") {
    CountingSessionStore store{{.snapshot = startupSnapshot(), .status = SessionLoadStatus::Loaded}};

    const auto result = loadSessionForStartup(store, false);

    CHECK(store.load_calls == 0);
    CHECK(result.status == SessionLoadStatus::Missing);
    CHECK(result.snapshot == SessionSnapshot{});
}

TEST_CASE("startup loads exactly once and forwards the store result when restore-last-session is enabled",
          "[m0][qt][core][session][startup][FR-043]") {
    const SessionLoadResult expected{.snapshot = startupSnapshot(), .status = SessionLoadStatus::RecoveredFromBackup};
    CountingSessionStore store{expected};

    const auto result = loadSessionForStartup(store, true);

    CHECK(store.load_calls == 1);
    CHECK(result.status == expected.status);
    CHECK(result.snapshot == expected.snapshot);
}

} // namespace music_player::m0::test
