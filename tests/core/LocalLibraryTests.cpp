#include "m0/domain/LocalLibrary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace music_player::m0::test {
namespace {
using Path = std::filesystem::path;

Path track(const std::size_t index) { return Path{"C:/Music/track-" + std::to_string(index) + ".mp3"}; }

LocalLibrary withTracks(const std::size_t count) {
    LocalLibrary library;
    std::vector<Path> paths;
    for (std::size_t index = 0; index < count; ++index)
        paths.push_back(track(index));
    REQUIRE(library.admit(paths).has_value());
    return library;
}
} // namespace

TEST_CASE("admitting new paths creates one stable record per exact path", "[library]") {
    LocalLibrary library;
    const auto first = library.admit({track(0), track(1), track(0)});
    REQUIRE(first);
    CHECK(first->added == 2);
    REQUIRE(first->ids.size() == 3);
    CHECK(first->ids[0] == first->ids[2]);
    CHECK(first->ids[0] != first->ids[1]);
    const auto again = library.admit({track(1)});
    REQUIRE(again);
    CHECK(again->added == 0);
    CHECK(again->ids == std::vector<std::uint64_t>{first->ids[1]});
    CHECK(library.snapshot().records.size() == 2);
    const auto* record = library.findByPath(track(0));
    REQUIRE(record);
    CHECK(record->id == first->ids[0]);
    CHECK(record->available);
    CHECK(record->title.empty());
}

TEST_CASE("paths are compared exactly, without case folding or normalization", "[library]") {
    LocalLibrary library;
    const auto result = library.admit({Path{"C:/Music/Song.mp3"}, Path{"C:/Music/song.mp3"}});
    REQUIRE(result);
    CHECK(result->added == 2);
}

TEST_CASE("removing a record never reuses its ID, even after a restart", "[library]") {
    auto library = withTracks(3);
    const auto last = library.findByPath(track(2))->id;
    REQUIRE(library.remove(last));
    CHECK_FALSE(library.remove(last));
    CHECK(library.findByPath(track(2)) == nullptr);
    LocalLibrary restarted;
    REQUIRE(restarted.restore(library.snapshot()));
    const auto readded = restarted.admit({track(2)});
    REQUIRE(readded);
    CHECK(readded->added == 1);
    CHECK(readded->ids.front() != last);
}

TEST_CASE("an invalid or oversized batch changes nothing", "[library]") {
    auto library = withTracks(1);
    const auto before = library.snapshot();
    CHECK_FALSE(library.admit({track(5), Path{}}));
    CHECK_FALSE(library.admit({Path{std::string("C:/Music/a\0b.mp3", 16)}}));
    CHECK(library.snapshot() == before);
    std::vector<Path> too_many;
    for (std::size_t index = 0; index < maximum_library_records; ++index)
        too_many.push_back(track(index + 10));
    CHECK_FALSE(library.admit(too_many));
    CHECK(library.snapshot() == before);
}

TEST_CASE("metadata and availability update one record", "[library]") {
    auto library = withTracks(2);
    const auto id = library.findByPath(track(1))->id;
    REQUIRE(library.setMetadata(id, {"Canção 日本", "Artista", "Álbum", 183'000}));
    REQUIRE(library.setAvailable(id, false));
    const auto* record = library.find(id);
    REQUIRE(record);
    CHECK(record->title == "Canção 日本");
    CHECK(record->artist == "Artista");
    CHECK(record->album == "Álbum");
    CHECK(record->duration_ms == std::optional<std::uint64_t>{183'000});
    CHECK_FALSE(record->available);
    CHECK_FALSE(library.setMetadata(id, {"line\nbreak", "", "", std::nullopt}));
    CHECK_FALSE(library.setMetadata(id, {std::string(maximum_library_text_bytes + 1, 'a'), "", "", std::nullopt}));
    CHECK_FALSE(library.setMetadata(999, {"x", "", "", std::nullopt}));
    CHECK(library.find(id)->title == "Canção 日本");
}

TEST_CASE("relocating keeps the ID and refuses another record's path", "[library]") {
    auto library = withTracks(2);
    const auto id = library.findByPath(track(0))->id;
    REQUIRE(library.setAvailable(id, false));
    CHECK(library.relocate(id, track(1)) == LibraryRelocation::Conflict);
    CHECK(library.relocate(id, Path{}) == LibraryRelocation::InvalidPath);
    CHECK(library.relocate(999, track(7)) == LibraryRelocation::NotFound);
    CHECK(library.relocate(id, track(7)) == LibraryRelocation::Relocated);
    const auto* moved = library.find(id);
    REQUIRE(moved);
    CHECK(moved->source_path == track(7));
    CHECK(moved->available);
    CHECK(library.findByPath(track(0)) == nullptr);
}

TEST_CASE("restore accepts only consistent snapshots", "[library]") {
    const LibrarySnapshot valid{{{3, track(0), "", "", "", std::nullopt, true}}, 4};
    LocalLibrary library;
    CHECK(library.restore(valid));
    auto duplicate_id = valid;
    duplicate_id.records.push_back({3, track(1), "", "", "", std::nullopt, true});
    auto duplicate_path = valid;
    duplicate_path.records.push_back({5, track(0), "", "", "", std::nullopt, true});
    auto zero_id = valid;
    zero_id.records.front().id = 0;
    auto zero_next = valid;
    zero_next.next_id = 0;
    auto bad_text = valid;
    bad_text.records.front().artist = " padded ";
    for (const auto& invalid : {duplicate_id, duplicate_path, zero_id, zero_next, bad_text}) {
        LocalLibrary other;
        CHECK_FALSE(other.restore(invalid));
        CHECK(other.snapshot().records.empty());
    }
}
TEST_CASE("a download job is admitted once and a later removal stays removed", "[library]") {
    LocalLibrary library;
    const auto first = library.admitDownload(track(1), "downloads-7");
    REQUIRE(first);
    CHECK(first->added == 1);
    const auto id = first->ids.front();
    REQUIRE(library.remove(id));
    // Replaying the same job (a retry or the startup reconciliation) must not resurrect it.
    const auto replay = library.admitDownload(track(1), "downloads-7");
    REQUIRE(replay);
    CHECK(replay->added == 0);
    CHECK(replay->ids.empty());
    CHECK(library.findByPath(track(1)) == nullptr);
    // An explicit local ingress of the same file still works.
    REQUIRE(library.admit({track(1)}));
    CHECK(library.findByPath(track(1)) != nullptr);
}

TEST_CASE("a new job for a known path reuses its record", "[library]") {
    auto library = withTracks(1);
    const auto existing = library.findByPath(track(0))->id;
    const auto result = library.admitDownload(track(0), "downloads-1");
    REQUIRE(result);
    CHECK(result->added == 0);
    CHECK(result->ids == std::vector<std::uint64_t>{existing});
    CHECK(library.snapshot().download_jobs == std::vector<std::string>{"downloads-1"});
}

TEST_CASE("the download job ledger is bounded and validated", "[library]") {
    LocalLibrary library;
    CHECK_FALSE(library.admitDownload(track(1), ""));
    CHECK_FALSE(library.admitDownload(track(1), "with space"));
    CHECK_FALSE(library.admitDownload(track(1), std::string(maximum_library_job_id_bytes + 1, 'a')));
    CHECK(library.snapshot().records.empty());
    for (std::size_t index = 0; index <= maximum_library_download_jobs; ++index)
        REQUIRE(library.admitDownload(track(1), "job-" + std::to_string(index)));
    const auto jobs = library.snapshot().download_jobs;
    CHECK(jobs.size() == maximum_library_download_jobs);
    CHECK(jobs.front() == "job-1"); // The oldest entry was evicted.
    CHECK(jobs.back() == "job-" + std::to_string(maximum_library_download_jobs));
    auto duplicate = library.snapshot();
    duplicate.download_jobs.push_back(duplicate.download_jobs.front());
    LocalLibrary other;
    CHECK_FALSE(other.restore(duplicate));
}

} // namespace music_player::m0::test
