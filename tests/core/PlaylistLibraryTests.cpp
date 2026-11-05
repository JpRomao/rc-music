#include "m0/domain/PlaylistLibrary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace music_player::m0::test {
namespace {
using Path = std::filesystem::path;

Path source(const std::size_t index = 0) { return Path{"C:/Music/track-" + std::to_string(index) + ".mp3"}; }
PlaylistLibrarySnapshot sample() {
    return {{{7, "Primeira 日本", {{15, source()}, {16, source(1)}}}, {9, "Segunda", {{18, source(2)}}}}};
}
PlaylistLibrarySnapshot fullEntrySnapshot() {
    PlaylistLibrarySnapshot snapshot{{{1, "All entries", {}}}};
    for (std::size_t index = 0; index < maximum_playlist_entries; ++index)
        snapshot.playlists.front().entries.push_back({index + 1, source()});
    return snapshot;
}
PlaylistLibrarySnapshot payloadBoundary() {
    PlaylistLibrarySnapshot snapshot{{{1, "ab", {}}}};
    auto remaining = maximum_playlist_payload_bytes - snapshot.playlists.front().name.size();
    while (remaining > 0) {
        const auto units = std::min(remaining / sizeof(Path::value_type), maximum_playlist_path_units);
        if (units == 0)
            break;
        snapshot.playlists.front().entries.push_back(
            {snapshot.playlists.front().entries.size() + 1,
             Path{Path::string_type(units, static_cast<Path::value_type>('x'))}});
        remaining -= units * sizeof(Path::value_type);
    }
    REQUIRE(remaining == 0);
    return snapshot;
}

TEST_CASE("playlists start empty and snapshot validation accepts an empty library", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    CHECK(library.snapshot().playlists.empty());
    CHECK(isValidPlaylistLibrarySnapshot({}));
    CHECK(library.find(1) == nullptr);
}

TEST_CASE("playlists create independent named lists and preserve duplicate Unicode sources",
          "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    const Path unicode{u8"C:/Música/日本語 🎵.mp3"};
    const auto first = library.create("Aberturas 日本 🎵", {unicode, unicode, source()});
    const auto second = library.create("Treino");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first != 0);
    CHECK(*second != 0);
    CHECK(first != second);
    const auto* playlist = library.find(*first);
    REQUIRE(playlist);
    CHECK(playlist->name == "Aberturas 日本 🎵");
    REQUIRE(playlist->entries.size() == 3);
    CHECK(playlist->entries[0].source_path == unicode);
    CHECK(playlist->entries[1].source_path == unicode);
    CHECK(playlist->entries[2].source_path == source());
    CHECK(playlist->entries[0].id != 0);
    CHECK(playlist->entries[0].id != playlist->entries[1].id);
    CHECK(library.find(*second)->entries.empty());
    CHECK(isValidPlaylistLibrarySnapshot(library.snapshot()));
}

TEST_CASE("playlists allow duplicate names while stable ids distinguish their contents", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    const auto first = library.create("Favoritas", {source()});
    const auto second = library.create("Favoritas", {source(1)});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK(library.find(*first)->entries.front().id != library.find(*second)->entries.front().id);
    CHECK(library.find(*first)->entries.front().source_path == source());
    CHECK(library.find(*second)->entries.front().source_path == source(1));
}

TEST_CASE("playlists rename preserves identity entries and the other lists", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    REQUIRE(library.restore(sample()));
    const auto before = library.snapshot();
    REQUIRE(library.rename(7, "Renomeada 日本"));
    const auto after = library.snapshot();
    CHECK(after.playlists[0].id == before.playlists[0].id);
    CHECK(after.playlists[0].name == "Renomeada 日本");
    CHECK(after.playlists[0].entries == before.playlists[0].entries);
    CHECK(after.playlists[1] == before.playlists[1]);
    CHECK_FALSE(library.rename(7, "Renomeada 日本"));
    CHECK(library.snapshot() == after);
}

TEST_CASE("playlists append keeps existing entries and gives repeated paths new global entry ids",
          "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    REQUIRE(library.restore(sample()));
    const auto before = library.snapshot();
    REQUIRE(library.append(7, {source(), source()}));
    const auto after = library.snapshot();
    REQUIRE(after.playlists[0].entries.size() == 4);
    CHECK(after.playlists[0].entries[0] == before.playlists[0].entries[0]);
    CHECK(after.playlists[0].entries[1] == before.playlists[0].entries[1]);
    CHECK(after.playlists[1] == before.playlists[1]);
    CHECK(after.playlists[0].entries[2].id != after.playlists[0].entries[3].id);
    CHECK(after.playlists[0].entries[2].source_path == source());
    CHECK(after.playlists[0].entries[3].source_path == source());
    CHECK(isValidPlaylistLibrarySnapshot(after));
}

TEST_CASE("playlists remove a duplicate by entry identity without removing the other copy",
          "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    const auto id = library.create("Repeated", {source(), source(), source(1)});
    REQUIRE(id);
    const auto before = library.snapshot().playlists.front();
    REQUIRE(library.removeEntry(*id, before.entries[1].id));
    const auto after = library.snapshot().playlists.front();
    REQUIRE(after.entries.size() == 2);
    CHECK(after.entries[0] == before.entries[0]);
    CHECK(after.entries[1] == before.entries[2]);
    CHECK_FALSE(library.removeEntry(*id, before.entries[1].id));
}

TEST_CASE("playlists move an entry to its final index in either direction", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    const auto id = library.create("Order", {source(), source(1), source(2), source(3)});
    REQUIRE(id);
    const auto original = library.snapshot().playlists.front().entries;
    REQUIRE(library.moveEntry(*id, original[0].id, 3));
    CHECK(library.find(*id)->entries == std::vector<PlaylistEntry>{original[1], original[2], original[3], original[0]});
    REQUIRE(library.moveEntry(*id, original[0].id, 0));
    CHECK(library.find(*id)->entries == original);
    const auto before = library.snapshot();
    CHECK_FALSE(library.moveEntry(*id, original[0].id, 0));
    CHECK_FALSE(library.moveEntry(*id, original[0].id, original.size()));
    CHECK_FALSE(library.moveEntry(*id, 0, 1));
    CHECK(library.snapshot() == before);
}

TEST_CASE("playlists deletion removes only its saved list and never reuses its id in the live library",
          "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    const auto first = library.create("Keep", {source()});
    const auto removed = library.create("Remove", {source(1)});
    REQUIRE(first);
    REQUIRE(removed);
    const auto kept = *library.find(*first);
    REQUIRE(library.remove(*removed));
    CHECK(library.find(*removed) == nullptr);
    CHECK(*library.find(*first) == kept);
    const auto created = library.create("New");
    REQUIRE(created);
    CHECK(created != removed);
    CHECK_FALSE(library.remove(*removed));
    CHECK(library.find(*first)->entries.front().source_path == source());
}

TEST_CASE("playlists reject invalid names without mutating state or allocation", "[core][playlists][FR-056]") {
    const std::vector<std::string> invalid{"",
                                           " ",
                                           " Leading",
                                           "Trailing ",
                                           "Two  spaces",
                                           "Line\nBreak",
                                           std::string("Nul\0Name", 8),
                                           std::string("\xc0\xaf", 2),
                                           std::string("\xed\xa0\x80", 3),
                                           "Invisible\xe2\x80\x8b",
                                           std::string(maximum_playlist_name_bytes + 1, 'n')};
    for (const auto& name : invalid) {
        CAPTURE(name.size());
        PlaylistLibrary library;
        REQUIRE(library.restore(sample()));
        PlaylistLibrary control = library;
        const auto before = library.snapshot();
        CHECK_FALSE(library.create(name));
        CHECK_FALSE(library.rename(7, name));
        CHECK(library.snapshot() == before);
        CHECK(library.create("Valid") == control.create("Valid"));
    }
}

TEST_CASE("playlists accept the inclusive canonical name byte limit", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    const std::string maximum_name(maximum_playlist_name_bytes, 'N');
    const auto id = library.create(maximum_name);
    REQUIRE(id);
    CHECK(library.find(*id)->name == maximum_name);
}

TEST_CASE("playlists reject invalid path batches atomically", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    REQUIRE(library.restore(sample()));
    PlaylistLibrary control = library;
    const auto before = library.snapshot();
    const Path with_nul{
        Path::string_type{static_cast<Path::value_type>('a'), Path::value_type{}, static_cast<Path::value_type>('b')}};
    const Path too_long{Path::string_type(maximum_playlist_path_units + 1, static_cast<Path::value_type>('x'))};
    for (const auto& path : {Path{}, with_nul, too_long}) {
        CHECK_FALSE(library.create("Invalid", {source(), path}));
        CHECK_FALSE(library.append(7, {source(), path}));
        CHECK(library.snapshot() == before);
    }
    CHECK_FALSE(library.append(7, {}));
    CHECK(library.snapshot() == before);
    REQUIRE(library.append(7, {source(9)}));
    REQUIRE(control.append(7, {source(9)}));
    CHECK(library.create("Next") == control.create("Next"));
    CHECK(library.snapshot() == control.snapshot());
}

TEST_CASE("playlists reject unknown ids without mutating any list", "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    REQUIRE(library.restore(sample()));
    const auto before = library.snapshot();
    CHECK_FALSE(library.rename(0, "No"));
    CHECK_FALSE(library.remove(100));
    CHECK_FALSE(library.append(100, {source()}));
    CHECK_FALSE(library.removeEntry(100, 15));
    CHECK_FALSE(library.removeEntry(7, 18)); // Entry belongs to another playlist.
    CHECK_FALSE(library.moveEntry(100, 15, 0));
    CHECK_FALSE(library.moveEntry(7, 18, 0));
    CHECK(library.snapshot() == before);
}

TEST_CASE("playlists snapshot is detached and round trips ids names order and duplicates",
          "[core][playlists][FR-056]") {
    PlaylistLibrary library;
    REQUIRE(library.restore(sample()));
    auto copy = library.snapshot();
    PlaylistLibrary restored;
    REQUIRE(restored.restore(copy));
    CHECK(restored.snapshot() == library.snapshot());
    copy.playlists.front().name = "Changed copy";
    copy.playlists.front().entries.clear();
    CHECK(library.snapshot() == sample());
    CHECK(restored.snapshot() == sample());
    REQUIRE(restored.restore({}));
    CHECK(restored.snapshot().playlists.empty());
    CHECK(library.snapshot() == sample());
}

TEST_CASE("playlists restore rejects zero duplicate or foreign entry identities atomically",
          "[core][playlists][FR-056]") {
    const auto seed = sample();
    std::vector<PlaylistLibrarySnapshot> invalid;
    auto value = seed;
    value.playlists.front().id = 0;
    invalid.push_back(value);
    value = seed;
    value.playlists.back().id = value.playlists.front().id;
    invalid.push_back(value);
    value = seed;
    value.playlists.front().entries.front().id = 0;
    invalid.push_back(value);
    value = seed;
    value.playlists.front().entries.back().id = value.playlists.front().entries.front().id;
    invalid.push_back(value);
    value = seed;
    value.playlists.back().entries.front().id = value.playlists.front().entries.front().id;
    invalid.push_back(value);
    for (const auto& snapshot : invalid) {
        PlaylistLibrary library;
        REQUIRE(library.restore(seed));
        PlaylistLibrary control = library;
        CHECK_FALSE(isValidPlaylistLibrarySnapshot(snapshot));
        CHECK_FALSE(library.restore(snapshot));
        CHECK(library.snapshot() == seed);
        CHECK(library.create("Still valid", {source(4)}) == control.create("Still valid", {source(4)}));
        CHECK(library.snapshot() == control.snapshot());
    }
}

TEST_CASE("playlists restore validates names and paths before accepting any part", "[core][playlists][FR-056]") {
    for (const bool invalid_name : {true, false}) {
        auto invalid = sample();
        if (invalid_name)
            invalid.playlists.back().name = "\xff";
        else
            invalid.playlists.back().entries.front().source_path = Path{};
        PlaylistLibrary library;
        REQUIRE(library.restore(sample()));
        CHECK_FALSE(isValidPlaylistLibrarySnapshot(invalid));
        CHECK_FALSE(library.restore(invalid));
        CHECK(library.snapshot() == sample());
    }
}

TEST_CASE("playlists allocate unused nonzero ids after restoring maximum uint64 values", "[core][playlists][FR-056]") {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const PlaylistLibrarySnapshot snapshot{{{maximum, "Maximum", {{maximum, source()}}}, {1, "One", {{1, source(1)}}}}};
    PlaylistLibrary library;
    REQUIRE(library.restore(snapshot));
    const auto created = library.create("Next", {source(2)});
    REQUIRE(created);
    CHECK(*created != 0);
    CHECK(*created != maximum);
    CHECK(*created != 1);
    const auto entry_id = library.find(*created)->entries.front().id;
    CHECK(entry_id != 0);
    CHECK(entry_id != maximum);
    CHECK(entry_id != 1);
    CHECK(isValidPlaylistLibrarySnapshot(library.snapshot()));
}

TEST_CASE("playlists enforce the library count inclusively", "[core][playlists][FR-056]") {
    PlaylistLibrarySnapshot maximum;
    for (std::size_t index = 0; index < maximum_named_playlists; ++index)
        maximum.playlists.push_back({index + 1, "List", {}});
    CHECK(isValidPlaylistLibrarySnapshot(maximum));
    PlaylistLibrary library;
    REQUIRE(library.restore(maximum));
    CHECK_FALSE(library.create("Too many"));
    CHECK(library.snapshot() == maximum);
    maximum.playlists.push_back({maximum_named_playlists + 1, "Over", {}});
    CHECK_FALSE(isValidPlaylistLibrarySnapshot(maximum));
}

TEST_CASE("playlists enforce the aggregate entry count across different saved lists", "[core][playlists][FR-056]") {
    auto maximum = fullEntrySnapshot();
    CHECK(isValidPlaylistLibrarySnapshot(maximum));
    PlaylistLibrary library;
    REQUIRE(library.restore(maximum));
    const auto empty = library.create("Empty");
    REQUIRE(empty);
    const auto before = library.snapshot();
    CHECK_FALSE(library.append(*empty, {source()}));
    CHECK_FALSE(library.create("Over", {source()}));
    CHECK(library.snapshot() == before);
    maximum.playlists.push_back({2, "Over", {{maximum_playlist_entries + 1, source()}}});
    CHECK_FALSE(isValidPlaylistLibrarySnapshot(maximum));
}

TEST_CASE("playlists bound retained payload inclusively before accepting a snapshot", "[core][playlists][FR-056]") {
    auto boundary = payloadBoundary();
    CHECK(isValidPlaylistLibrarySnapshot(boundary));
    PlaylistLibrary library;
    REQUIRE(library.restore(boundary));
    const auto before = library.snapshot();
    CHECK_FALSE(library.rename(1, "abc"));
    CHECK(library.snapshot() == before);
    auto path = boundary.playlists.front().entries.back().source_path.native();
    path.push_back(static_cast<Path::value_type>('x'));
    boundary.playlists.front().entries.back().source_path = Path{std::move(path)};
    CHECK_FALSE(isValidPlaylistLibrarySnapshot(boundary));
    CHECK_FALSE(library.restore(boundary));
    CHECK(library.snapshot() == before);
}

TEST_CASE("playlists domain edits do not mutate the source queue snapshot used to create a list",
          "[core][playlists][FR-056]") {
    const std::vector<Path> queue_paths{source(), source(), source(1)};
    PlaylistLibrary library;
    const auto id = library.create("Saved queue", queue_paths);
    REQUIRE(id);
    const auto entry_id = library.find(*id)->entries[1].id;
    REQUIRE(library.removeEntry(*id, entry_id));
    REQUIRE(library.rename(*id, "Edited"));
    REQUIRE(library.remove(*id));
    CHECK(queue_paths == std::vector<Path>{source(), source(), source(1)});
}
} // namespace
} // namespace music_player::m0::test
