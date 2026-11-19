#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {

inline constexpr std::size_t maximum_named_playlists = 128;
inline constexpr std::size_t maximum_playlist_entries = 10'000;
inline constexpr std::size_t maximum_playlist_name_bytes = 256;
inline constexpr std::size_t maximum_playlist_path_units = 32'768;
// UTF-8 name bytes + native path code-unit bytes, excluding container overhead.
inline constexpr std::size_t maximum_playlist_payload_bytes = 8 * 1024 * 1024;

struct PlaylistEntry final {
    std::uint64_t id{};
    std::filesystem::path source_path{};
    [[nodiscard]] bool operator==(const PlaylistEntry&) const = default;
};

// Who may see a list when a listening room publishes playlists. Lists start Private.
enum class PlaylistVisibility : std::uint8_t { Private, Room };

struct NamedPlaylist final {
    std::uint64_t id{};
    std::string name{};
    std::vector<PlaylistEntry> entries{};
    PlaylistVisibility visibility{PlaylistVisibility::Private};
    [[nodiscard]] bool operator==(const NamedPlaylist&) const = default;
};

struct PlaylistLibrarySnapshot final {
    std::vector<NamedPlaylist> playlists{};
    [[nodiscard]] bool operator==(const PlaylistLibrarySnapshot&) const = default;
};

// Lexical and bounded validation only, without filesystem access. Names must be
// nonempty, valid UTF-8 in the existing single-line display policy's canonical
// form: controls, trimmed/repeated separators and invisible layout controls are
// refused rather than silently changing a persisted name. Duplicate names and
// paths are allowed; playlist IDs and entry IDs each form a separate namespace.
[[nodiscard]] bool isValidPlaylistLibrarySnapshot(const PlaylistLibrarySnapshot& snapshot);

class PlaylistLibrary final {
  public:
    [[nodiscard]] PlaylistLibrarySnapshot snapshot() const;
    // Bootstrap only: an invalid snapshot leaves state and ID allocation intact.
    [[nodiscard]] bool restore(PlaylistLibrarySnapshot snapshot);
    [[nodiscard]] std::optional<std::uint64_t> create(std::string name,
                                                      std::vector<std::filesystem::path> source_paths = {});
    [[nodiscard]] bool rename(std::uint64_t playlist_id, std::string name);
    [[nodiscard]] bool remove(std::uint64_t playlist_id);
    // False for an unknown id or when the list already has that visibility.
    [[nodiscard]] bool setVisibility(std::uint64_t playlist_id, PlaylistVisibility visibility);
    [[nodiscard]] bool append(std::uint64_t playlist_id, std::vector<std::filesystem::path> source_paths);
    [[nodiscard]] bool replaceEntrySource(std::uint64_t playlist_id, std::uint64_t entry_id,
                                          std::filesystem::path source_path);
    [[nodiscard]] bool removeEntry(std::uint64_t playlist_id, std::uint64_t entry_id);
    // destination_index is the entry's final zero-based position.
    [[nodiscard]] bool moveEntry(std::uint64_t playlist_id, std::uint64_t entry_id, std::size_t destination_index);
    // Borrowed until the next successful mutation; callers copy before callbacks.
    [[nodiscard]] const NamedPlaylist* find(std::uint64_t playlist_id) const noexcept;

  private:
    PlaylistLibrarySnapshot snapshot_{};
    std::uint64_t next_playlist_id_{1};
    std::uint64_t next_entry_id_{1};
};

} // namespace music_player::m0
