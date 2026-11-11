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

[[nodiscard]] std::filesystem::path receiptPath() {
#ifdef _WIN32
    return LR"(C:\Downloads\Faixa 日本語.mp3)";
#else
    return "/downloads/Faixa 日本語.mp3";
#endif
}

TEST_CASE("session import receipts survive manual queue removal and missing media",
          "[core][session][download-receipts]") {
    SessionSnapshot snapshot;
    snapshot.download_import_receipts = {{"job-1", receiptPath()}, {"job-2", receiptPath()}};
    CHECK(isValidSessionSnapshot(snapshot));
    CHECK(isValidDownloadImportReceipts(snapshot.download_import_receipts));
    CHECK(snapshot == SessionSnapshot{.download_import_receipts = snapshot.download_import_receipts});
    auto changed = snapshot;
    changed.download_import_receipts.front().job_id = "another";
    CHECK_FALSE(changed == snapshot);
}

TEST_CASE("session import receipts reject malformed identity and paths", "[core][session][download-receipts]") {
    SessionSnapshot snapshot{.download_import_receipts = {{"job-1", receiptPath()}}};
    SECTION("empty job") { snapshot.download_import_receipts.front().job_id.clear(); }
    SECTION("long job") { snapshot.download_import_receipts.front().job_id.assign(65, 'a'); }
    SECTION("space job") { snapshot.download_import_receipts.front().job_id = "job 1"; }
    SECTION("unicode job") { snapshot.download_import_receipts.front().job_id = "job-日本語"; }
    SECTION("NUL job") { snapshot.download_import_receipts.front().job_id = std::string{"job\0a", 5}; }
    SECTION("duplicate job") { snapshot.download_import_receipts.push_back(snapshot.download_import_receipts.front()); }
    SECTION("empty path") { snapshot.download_import_receipts.front().source_path.clear(); }
    SECTION("relative path") { snapshot.download_import_receipts.front().source_path = "relative.mp3"; }
    SECTION("URL path") { snapshot.download_import_receipts.front().source_path = "https://example.test/file.mp3"; }
    SECTION("unnormalized path") {
        snapshot.download_import_receipts.front().source_path = receiptPath().parent_path() / ".." / "file.mp3";
    }
    SECTION("non MP3") { snapshot.download_import_receipts.front().source_path.replace_extension(".mp4"); }
    SECTION("NUL path") {
        auto native = receiptPath().native();
        native.push_back(std::filesystem::path::value_type{});
        native.append(std::filesystem::path{"other.mp3"}.native());
        snapshot.download_import_receipts.front().source_path = native;
    }
    SECTION("invalid Unicode path") {
        auto native = receiptPath().native();
#ifdef _WIN32
        native.insert(native.size() - 4, 1, static_cast<wchar_t>(0xd800));
#else
        native.insert(native.size() - 4, 1, static_cast<char>(0xff));
#endif
        snapshot.download_import_receipts.front().source_path = native;
    }
    CHECK_FALSE(isValidDownloadImportReceipts(snapshot.download_import_receipts));
    CHECK_FALSE(isValidSessionSnapshot(snapshot));
}

TEST_CASE("session import receipt limits are inclusive without eviction", "[core][session][download-receipts]") {
    SessionSnapshot snapshot;
    for (std::size_t index = 0; index < maximum_download_import_receipts; ++index)
        snapshot.download_import_receipts.push_back({"job-" + std::to_string(index), receiptPath()});
    REQUIRE(isValidSessionSnapshot(snapshot));
    snapshot.download_import_receipts.push_back({"extra", receiptPath()});
    CHECK_FALSE(isValidSessionSnapshot(snapshot));

    const auto fixed = receiptPath().parent_path().native();
    constexpr auto unit_bytes = sizeof(std::filesystem::path::value_type);
    // Choose an ASCII job length that exactly fills the native-unit byte boundary.
    const std::string job_id(unit_bytes, 'a');
    const auto units = (maximum_download_import_receipt_bytes - job_id.size()) / unit_bytes;
    auto native = fixed;
    native.push_back(std::filesystem::path::preferred_separator);
    native.append(units - native.size() - 4, static_cast<std::filesystem::path::value_type>('a'));
    native.append(std::filesystem::path{".mp3"}.native());
    snapshot.download_import_receipts = {{job_id, std::filesystem::path{native}}};
    REQUIRE(isValidSessionSnapshot(snapshot));
    native.insert(native.size() - 4, 1, static_cast<std::filesystem::path::value_type>('a'));
    snapshot.download_import_receipts.front().source_path = native;
    CHECK_FALSE(isValidSessionSnapshot(snapshot));
}

TEST_CASE("session import receipt MP3 suffix and longest ASCII identity are accepted",
          "[core][session][download-receipts]") {
    auto path = receiptPath();
    path.replace_extension(".MP3");
    const SessionSnapshot snapshot{
        .download_import_receipts = {{std::string(64, 'Z'), path}, {"a-z_A0-9", receiptPath()}}};
    CHECK(isValidSessionSnapshot(snapshot));
}

TEST_CASE("session retired download IDs must be valid and distinct from live receipts",
          "[core][session][download-receipts]") {
    SessionSnapshot snapshot{.download_import_receipts = {{"live", receiptPath()}},
                             .retired_download_import_job_ids = {"old-1", "OLD-1", std::string(64, 'z')}};
    REQUIRE(isValidSessionSnapshot(snapshot));
    auto changed = snapshot;
    changed.retired_download_import_job_ids.front() = "other";
    CHECK_FALSE(changed == snapshot);
    auto& retired = snapshot.retired_download_import_job_ids;
    SECTION("duplicate") { retired.back() = retired.front(); }
    SECTION("live collision") { retired.back() = "live"; }
    SECTION("empty") { retired.back().clear(); }
    SECTION("overlength") { retired.back().assign(65, 'a'); }
    SECTION("not ASCII") { retired.back() = "faixa-日本語"; }
    SECTION("forbidden punctuation") { retired.back() = "old.1"; }
    SECTION("embedded null") { retired.back() = std::string{"old\0id", 6}; }
    CHECK_FALSE(isValidSessionSnapshot(snapshot));
}

TEST_CASE("session retired download IDs have a separate bounded capacity", "[core][session][download-receipts]") {
    SessionSnapshot snapshot{.download_import_receipts = {{"live", receiptPath()}}};
    auto& retired = snapshot.retired_download_import_job_ids;
    for (std::size_t index = 0; index < maximum_retired_download_import_job_ids; ++index)
        retired.push_back("retired-" + std::to_string(index));
    REQUIRE(isValidSessionSnapshot(snapshot));
    retired.push_back("overflow");
    CHECK_FALSE(isValidSessionSnapshot(snapshot));
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

TEST_CASE("session snapshot round trip preserves a relocated path and resets only transient availability",
          "[m0][qt][core][session][relocate][FR-043][NFR-REL-005]") {
    LocalFileQueue queue;
    const auto original = validSnapshot();
    REQUIRE(queue.restore(original.queue));
    REQUIRE(queue.markUnavailable(1));
    const std::filesystem::path relocated{LR"(D:\Movida\Final 日本語.flac)"};
    const auto occurrence = queue.items()[1].occurrence_id;
    REQUIRE(queue.replaceUnavailableSource(occurrence, relocated));
    const SessionSnapshot persisted{.queue = queue.snapshot(), .position_ms = original.position_ms};

    REQUIRE(isValidSessionSnapshot(persisted));
    CHECK(persisted.queue.current_occurrence_id == original.queue.current_occurrence_id);
    CHECK(persisted.queue.items[0] == original.queue.items[0]);
    CHECK(persisted.queue.items[1].source_path == relocated);
    CHECK(persisted.queue.items[1].occurrence_id == occurrence);
    CHECK(persisted.queue.items[2] == original.queue.items[2]);
    CHECK(persisted.position_ms == original.position_ms);
    CHECK_FALSE(queue.items()[1].available);
    LocalFileQueue restored;
    REQUIRE(restored.restore(persisted.queue));
    CHECK(restored.snapshot() == persisted.queue);
    CHECK(std::ranges::all_of(restored.items(), &LocalFileQueueItem::available));
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

TEST_CASE("wrapped allocation after restore never revives a removed occurrence id",
          "[m0][qt][core][session][FR-016][FR-043]") {
    constexpr auto largest = std::numeric_limits<std::uint64_t>::max();
    constexpr LocalQueueOccurrenceId removed{1};
    const std::filesystem::path duplicate{LR"(C:\Music\Duplicate.wav)"};
    LocalFileQueue queue;

    SECTION("removing a restored row before inserting at the front") {
        REQUIRE(queue.restore({{{removed, duplicate}, {{largest}, duplicate}}, LocalQueueOccurrenceId{largest}}));
        REQUIRE(queue.remove(removed));
        REQUIRE(queue.insert({duplicate}, 0));
        CHECK(queue.currentOccurrenceId() == LocalQueueOccurrenceId{largest});
    }

    SECTION("clearing restored rows before replacing the queue") {
        REQUIRE(queue.restore({{{removed, duplicate}, {{largest}, duplicate}}, std::nullopt}));
        REQUIRE(queue.clear());
        REQUIRE(queue.replace({duplicate}));
    }

    SECTION("removing a restored row before the next allocation wraps") {
        REQUIRE(queue.restore({{{removed, duplicate}, {{largest - 1}, duplicate}}, std::nullopt}));
        REQUIRE(queue.remove(removed));
        REQUIRE(queue.append({duplicate, duplicate}));
        REQUIRE(queue.size() == 3);
        CHECK(queue.items()[1].occurrence_id == LocalQueueOccurrenceId{largest});
    }

    const auto before_stale_action = queue.snapshot();
    CHECK_FALSE(queue.remove(removed));
    CHECK(queue.snapshot() == before_stale_action);
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
