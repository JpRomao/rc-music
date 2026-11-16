#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {

// ADR-0019: the permanent "Minha música" collection, independent of the queue,
// playlists and download history. It is a catalog of references; it never
// touches audio files.
inline constexpr std::size_t maximum_library_records = 20'000;
inline constexpr std::size_t maximum_library_text_bytes = 512;
inline constexpr std::size_t maximum_library_path_units = 32'768;
// UTF-8 text bytes + native path code-unit bytes, excluding container overhead.
inline constexpr std::size_t maximum_library_payload_bytes = 24 * 1024 * 1024;
// Download jobs already admitted, newest last; older entries are evicted.
inline constexpr std::size_t maximum_library_download_jobs = 1024;
inline constexpr std::size_t maximum_library_job_id_bytes = 256;

struct LibraryMetadata final {
    std::string title{};
    std::string artist{};
    std::string album{};
    std::optional<std::uint64_t> duration_ms{};
    [[nodiscard]] bool operator==(const LibraryMetadata&) const = default;
};

struct LibraryRecord final {
    std::uint64_t id{};
    std::filesystem::path source_path{};
    // Empty text means unknown; the presentation falls back to the file name.
    std::string title{};
    std::string artist{};
    std::string album{};
    std::optional<std::uint64_t> duration_ms{};
    // Unavailable files stay catalogued until the user removes or relocates them.
    bool available{true};
    [[nodiscard]] bool operator==(const LibraryRecord&) const = default;
};

struct LibrarySnapshot final {
    std::vector<LibraryRecord> records{};
    // Persisted so a removed record's ID is never handed to another file.
    std::uint64_t next_id{1};
    // Ledger of admitted download jobs: a replayed or reconciled import never
    // resurrects a record the user removed.
    std::vector<std::string> download_jobs{};
    [[nodiscard]] bool operator==(const LibrarySnapshot&) const = default;
};

// Lexical and bounded validation only, without filesystem access. Paths are
// unique by exact comparison; text is empty or already in the single-line
// display policy's canonical form.
[[nodiscard]] bool isValidLibrarySnapshot(const LibrarySnapshot& snapshot);

struct LibraryAdmission final {
    // One ID per requested path, in order; repeated paths share an ID.
    std::vector<std::uint64_t> ids{};
    std::size_t added{};
};

enum class LibraryRelocation { Relocated, NotFound, InvalidPath, Conflict };

class LocalLibrary final {
  public:
    [[nodiscard]] LibrarySnapshot snapshot() const;
    // Bootstrap only: an invalid snapshot leaves the current state intact.
    [[nodiscard]] bool restore(LibrarySnapshot snapshot);
    // Idempotent by exact path. Transactional: an invalid path or exceeding the
    // record limit refuses the whole batch.
    [[nodiscard]] std::optional<LibraryAdmission> admit(const std::vector<std::filesystem::path>& source_paths);
    // A published download: admitted once per job ID. A job already in the
    // ledger returns an empty admission and changes nothing.
    [[nodiscard]] std::optional<LibraryAdmission> admitDownload(const std::filesystem::path& source_path,
                                                                const std::string& job_id);
    // Removes only the catalog record; a later explicit admission adds it again.
    [[nodiscard]] bool remove(std::uint64_t id);
    [[nodiscard]] bool setMetadata(std::uint64_t id, LibraryMetadata metadata);
    [[nodiscard]] bool setAvailable(std::uint64_t id, bool available);
    // Keeps the ID and marks the record available; never merges two records.
    [[nodiscard]] LibraryRelocation relocate(std::uint64_t id, std::filesystem::path source_path);
    // Borrowed until the next successful mutation.
    [[nodiscard]] const LibraryRecord* find(std::uint64_t id) const noexcept;
    [[nodiscard]] const LibraryRecord* findByPath(const std::filesystem::path& source_path) const noexcept;

  private:
    LibrarySnapshot snapshot_{};
};

} // namespace music_player::m0
