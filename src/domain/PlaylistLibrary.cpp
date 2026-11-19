#include "m0/domain/PlaylistLibrary.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {
namespace {
using IdSet = std::unordered_set<std::uint64_t>;

bool validName(const std::string& name) {
    return !name.empty() && name.size() <= maximum_playlist_name_bytes &&
           sanitizeMetadataText(name).disposition == MetadataTextDisposition::Unchanged;
}
bool validPath(const std::filesystem::path& path) {
    const auto& native = path.native();
    return !native.empty() && native.size() <= maximum_playlist_path_units &&
           native.find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}
std::uint64_t followingId(const std::uint64_t value) noexcept {
    return value == std::numeric_limits<std::uint64_t>::max() ? 1 : value + 1;
}
std::uint64_t allocateId(std::uint64_t& candidate, IdSet& occupied) {
    for (;;) {
        if (candidate == 0)
            candidate = 1;
        const auto result = candidate;
        candidate = followingId(candidate);
        if (occupied.insert(result).second)
            return result;
    }
}
IdSet playlistIds(const PlaylistLibrarySnapshot& snapshot) {
    IdSet result;
    result.reserve(snapshot.playlists.size() + 1);
    for (const auto& playlist : snapshot.playlists)
        result.insert(playlist.id);
    return result;
}
IdSet entryIds(const PlaylistLibrarySnapshot& snapshot, const std::size_t additional = 0) {
    IdSet result;
    std::size_t count = additional;
    for (const auto& playlist : snapshot.playlists)
        count += playlist.entries.size();
    result.reserve(count);
    for (const auto& playlist : snapshot.playlists)
        for (const auto& entry : playlist.entries)
            result.insert(entry.id);
    return result;
}
std::size_t entryCount(const PlaylistLibrarySnapshot& snapshot) noexcept {
    std::size_t result{};
    for (const auto& playlist : snapshot.playlists)
        result += playlist.entries.size();
    return result;
}
bool validBatch(const std::vector<std::filesystem::path>& source_paths) {
    return std::all_of(source_paths.begin(), source_paths.end(), validPath);
}
auto findPlaylist(PlaylistLibrarySnapshot& snapshot, const std::uint64_t id) {
    return std::find_if(snapshot.playlists.begin(), snapshot.playlists.end(),
                        [id](const auto& playlist) { return playlist.id == id; });
}
} // namespace

bool isValidPlaylistLibrarySnapshot(const PlaylistLibrarySnapshot& snapshot) {
    if (snapshot.playlists.size() > maximum_named_playlists)
        return false;
    std::size_t total_entries{};
    for (const auto& playlist : snapshot.playlists) {
        if (playlist.entries.size() > maximum_playlist_entries - total_entries)
            return false;
        total_entries += playlist.entries.size();
    }
    IdSet playlist_ids, entry_ids;
    playlist_ids.reserve(snapshot.playlists.size());
    entry_ids.reserve(total_entries);
    auto remaining = maximum_playlist_payload_bytes;
    for (const auto& playlist : snapshot.playlists) {
        if (playlist.id == 0 || !playlist_ids.insert(playlist.id).second || !validName(playlist.name) ||
            playlist.name.size() > remaining)
            return false;
        remaining -= playlist.name.size();
        for (const auto& entry : playlist.entries) {
            if (entry.id == 0 || !entry_ids.insert(entry.id).second || !validPath(entry.source_path))
                return false;
            // The path-unit bound is checked before the native-byte product.
            const auto bytes = entry.source_path.native().size() * sizeof(std::filesystem::path::value_type);
            if (bytes > remaining)
                return false;
            remaining -= bytes;
        }
    }
    return true;
}
PlaylistLibrarySnapshot PlaylistLibrary::snapshot() const { return snapshot_; }
bool PlaylistLibrary::restore(PlaylistLibrarySnapshot snapshot) {
    if (!isValidPlaylistLibrarySnapshot(snapshot))
        return false;
    std::uint64_t maximum_playlist_id{}, maximum_entry_id{};
    for (const auto& playlist : snapshot.playlists) {
        maximum_playlist_id = std::max(maximum_playlist_id, playlist.id);
        for (const auto& entry : playlist.entries)
            maximum_entry_id = std::max(maximum_entry_id, entry.id);
    }
    snapshot_ = std::move(snapshot);
    next_playlist_id_ = followingId(maximum_playlist_id);
    next_entry_id_ = followingId(maximum_entry_id);
    return true;
}
std::optional<std::uint64_t> PlaylistLibrary::create(std::string name,
                                                     std::vector<std::filesystem::path> source_paths) {
    if (!validName(name) || snapshot_.playlists.size() >= maximum_named_playlists ||
        source_paths.size() > maximum_playlist_entries - entryCount(snapshot_) || !validBatch(source_paths))
        return std::nullopt;
    auto playlist_ids = playlistIds(snapshot_);
    auto entry_ids = entryIds(snapshot_, source_paths.size());
    auto next_playlist = next_playlist_id_, next_entry = next_entry_id_;
    NamedPlaylist playlist{allocateId(next_playlist, playlist_ids), std::move(name), {}};
    playlist.entries.reserve(source_paths.size());
    for (auto& path : source_paths)
        playlist.entries.push_back({allocateId(next_entry, entry_ids), std::move(path)});
    const auto result = playlist.id;
    auto candidate = snapshot_;
    candidate.playlists.push_back(std::move(playlist));
    if (!isValidPlaylistLibrarySnapshot(candidate))
        return std::nullopt;
    snapshot_ = std::move(candidate);
    next_playlist_id_ = next_playlist;
    next_entry_id_ = next_entry;
    return result;
}
bool PlaylistLibrary::rename(const std::uint64_t playlist_id, std::string name) {
    if (!validName(name))
        return false;
    const auto* current = find(playlist_id);
    if (!current || current->name == name)
        return false;
    auto candidate = snapshot_;
    findPlaylist(candidate, playlist_id)->name = std::move(name);
    if (!isValidPlaylistLibrarySnapshot(candidate))
        return false;
    snapshot_ = std::move(candidate);
    return true;
}
bool PlaylistLibrary::setVisibility(const std::uint64_t playlist_id, const PlaylistVisibility visibility) {
    const auto found = findPlaylist(snapshot_, playlist_id);
    if (found == snapshot_.playlists.end() || found->visibility == visibility)
        return false;
    found->visibility = visibility;
    return true;
}
bool PlaylistLibrary::remove(const std::uint64_t playlist_id) {
    const auto found = findPlaylist(snapshot_, playlist_id);
    if (found == snapshot_.playlists.end())
        return false;
    snapshot_.playlists.erase(found);
    return true;
}
bool PlaylistLibrary::append(const std::uint64_t playlist_id, std::vector<std::filesystem::path> source_paths) {
    if (!find(playlist_id) || source_paths.empty() ||
        source_paths.size() > maximum_playlist_entries - entryCount(snapshot_) || !validBatch(source_paths))
        return false;
    auto candidate = snapshot_;
    auto& target = findPlaylist(candidate, playlist_id)->entries;
    target.reserve(target.size() + source_paths.size());
    auto occupied = entryIds(snapshot_, source_paths.size());
    auto next = next_entry_id_;
    for (auto& path : source_paths)
        target.push_back({allocateId(next, occupied), std::move(path)});
    if (!isValidPlaylistLibrarySnapshot(candidate))
        return false;
    snapshot_ = std::move(candidate);
    next_entry_id_ = next;
    return true;
}
bool PlaylistLibrary::replaceEntrySource(const std::uint64_t playlist_id, const std::uint64_t entry_id,
                                         std::filesystem::path source_path) {
    if (!validPath(source_path))
        return false;
    const auto* playlist = find(playlist_id);
    if (!playlist)
        return false;
    const auto entry = std::ranges::find(playlist->entries, entry_id, &PlaylistEntry::id);
    if (entry == playlist->entries.end() || entry->source_path == source_path)
        return false;
    const auto index = static_cast<std::size_t>(std::distance(playlist->entries.begin(), entry));
    auto candidate = snapshot_;
    findPlaylist(candidate, playlist_id)->entries[index].source_path = std::move(source_path);
    if (!isValidPlaylistLibrarySnapshot(candidate))
        return false;
    snapshot_ = std::move(candidate);
    return true;
}
bool PlaylistLibrary::removeEntry(const std::uint64_t playlist_id, const std::uint64_t entry_id) {
    auto found = findPlaylist(snapshot_, playlist_id);
    if (found == snapshot_.playlists.end())
        return false;
    auto& entries = found->entries;
    const auto entry =
        std::find_if(entries.begin(), entries.end(), [entry_id](const auto& value) { return value.id == entry_id; });
    if (entry == entries.end())
        return false;
    entries.erase(entry);
    return true;
}
bool PlaylistLibrary::moveEntry(const std::uint64_t playlist_id, const std::uint64_t entry_id,
                                const std::size_t destination_index) {
    auto found = findPlaylist(snapshot_, playlist_id);
    if (found == snapshot_.playlists.end() || destination_index >= found->entries.size())
        return false;
    auto& entries = found->entries;
    const auto entry =
        std::find_if(entries.begin(), entries.end(), [entry_id](const auto& value) { return value.id == entry_id; });
    if (entry == entries.end())
        return false;
    const auto source_index = static_cast<std::size_t>(std::distance(entries.begin(), entry));
    if (source_index == destination_index)
        return false;
    const auto destination = std::next(entries.begin(), static_cast<std::ptrdiff_t>(destination_index));
    if (source_index < destination_index)
        std::rotate(entry, std::next(entry), std::next(destination));
    else
        std::rotate(destination, entry, std::next(entry));
    return true;
}
const NamedPlaylist* PlaylistLibrary::find(const std::uint64_t playlist_id) const noexcept {
    const auto found = std::find_if(snapshot_.playlists.begin(), snapshot_.playlists.end(),
                                    [playlist_id](const auto& playlist) { return playlist.id == playlist_id; });
    return found == snapshot_.playlists.end() ? nullptr : &*found;
}
} // namespace music_player::m0
