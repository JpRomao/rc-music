#include "m0/domain/LocalLibrary.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {
namespace {
bool validPath(const std::filesystem::path& path) {
    const auto& native = path.native();
    return !native.empty() && native.size() <= maximum_library_path_units &&
           native.find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}
bool validText(const std::string& text) {
    return text.empty() || (text.size() <= maximum_library_text_bytes &&
                            sanitizeMetadataText(text).disposition == MetadataTextDisposition::Unchanged);
}
bool validMetadata(const LibraryMetadata& metadata) {
    return validText(metadata.title) && validText(metadata.artist) && validText(metadata.album);
}
std::size_t payloadBytes(const LibraryRecord& record) {
    return record.source_path.native().size() * sizeof(std::filesystem::path::value_type) + record.title.size() +
           record.artist.size() + record.album.size();
}
std::uint64_t followingId(const std::uint64_t value) noexcept {
    return value == std::numeric_limits<std::uint64_t>::max() ? 1 : value + 1;
}
} // namespace

bool isValidLibrarySnapshot(const LibrarySnapshot& snapshot) {
    if (snapshot.next_id == 0 || snapshot.records.size() > maximum_library_records)
        return false;
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(snapshot.records.size());
    std::unordered_set<std::filesystem::path::string_type> paths;
    paths.reserve(snapshot.records.size());
    auto remaining = maximum_library_payload_bytes;
    for (const auto& record : snapshot.records) {
        if (record.id == 0 || !ids.insert(record.id).second || !validPath(record.source_path) ||
            !paths.insert(record.source_path.native()).second || !validText(record.title) ||
            !validText(record.artist) || !validText(record.album))
            return false;
        const auto bytes = payloadBytes(record);
        if (bytes > remaining)
            return false;
        remaining -= bytes;
    }
    return true;
}

LibrarySnapshot LocalLibrary::snapshot() const { return snapshot_; }

bool LocalLibrary::restore(LibrarySnapshot snapshot) {
    if (!isValidLibrarySnapshot(snapshot))
        return false;
    snapshot_ = std::move(snapshot);
    return true;
}

std::optional<LibraryAdmission> LocalLibrary::admit(const std::vector<std::filesystem::path>& source_paths) {
    if (!std::all_of(source_paths.begin(), source_paths.end(), validPath))
        return std::nullopt;
    std::map<std::filesystem::path::string_type, std::uint64_t> known;
    std::unordered_set<std::uint64_t> occupied;
    for (const auto& record : snapshot_.records) {
        known.emplace(record.source_path.native(), record.id);
        occupied.insert(record.id);
    }
    auto candidate = snapshot_;
    LibraryAdmission result;
    result.ids.reserve(source_paths.size());
    for (const auto& path : source_paths) {
        if (const auto found = known.find(path.native()); found != known.end()) {
            result.ids.push_back(found->second);
            continue;
        }
        if (candidate.records.size() >= maximum_library_records)
            return std::nullopt;
        // Skip any ID still in use after a wrap-around; IDs are never reused while live.
        while (occupied.contains(candidate.next_id))
            candidate.next_id = followingId(candidate.next_id);
        const auto id = candidate.next_id;
        candidate.next_id = followingId(id);
        occupied.insert(id);
        known.emplace(path.native(), id);
        candidate.records.push_back({.id = id, .source_path = path});
        result.ids.push_back(id);
        ++result.added;
    }
    if (!isValidLibrarySnapshot(candidate))
        return std::nullopt;
    snapshot_ = std::move(candidate);
    return result;
}

bool LocalLibrary::remove(const std::uint64_t id) {
    const auto found = std::find_if(snapshot_.records.begin(), snapshot_.records.end(),
                                    [id](const auto& record) { return record.id == id; });
    if (found == snapshot_.records.end())
        return false;
    snapshot_.records.erase(found);
    return true;
}

bool LocalLibrary::setMetadata(const std::uint64_t id, LibraryMetadata metadata) {
    if (!validMetadata(metadata))
        return false;
    auto candidate = snapshot_;
    const auto found = std::find_if(candidate.records.begin(), candidate.records.end(),
                                    [id](const auto& record) { return record.id == id; });
    if (found == candidate.records.end())
        return false;
    found->title = std::move(metadata.title);
    found->artist = std::move(metadata.artist);
    found->album = std::move(metadata.album);
    found->duration_ms = metadata.duration_ms;
    if (!isValidLibrarySnapshot(candidate))
        return false; // Payload limit.
    snapshot_ = std::move(candidate);
    return true;
}

bool LocalLibrary::setAvailable(const std::uint64_t id, const bool available) {
    const auto found = std::find_if(snapshot_.records.begin(), snapshot_.records.end(),
                                    [id](const auto& record) { return record.id == id; });
    if (found == snapshot_.records.end())
        return false;
    found->available = available;
    return true;
}

LibraryRelocation LocalLibrary::relocate(const std::uint64_t id, std::filesystem::path source_path) {
    if (!validPath(source_path))
        return LibraryRelocation::InvalidPath;
    auto candidate = snapshot_;
    const auto found = std::find_if(candidate.records.begin(), candidate.records.end(),
                                    [id](const auto& record) { return record.id == id; });
    if (found == candidate.records.end())
        return LibraryRelocation::NotFound;
    const auto owner = findByPath(source_path);
    if (owner && owner->id != id)
        return LibraryRelocation::Conflict;
    found->source_path = std::move(source_path);
    found->available = true;
    if (!isValidLibrarySnapshot(candidate))
        return LibraryRelocation::InvalidPath; // Payload limit.
    snapshot_ = std::move(candidate);
    return LibraryRelocation::Relocated;
}

const LibraryRecord* LocalLibrary::find(const std::uint64_t id) const noexcept {
    const auto found = std::find_if(snapshot_.records.begin(), snapshot_.records.end(),
                                    [id](const auto& record) { return record.id == id; });
    return found == snapshot_.records.end() ? nullptr : &*found;
}

const LibraryRecord* LocalLibrary::findByPath(const std::filesystem::path& source_path) const noexcept {
    const auto found =
        std::find_if(snapshot_.records.begin(), snapshot_.records.end(), [&source_path](const auto& record) {
            return record.source_path.native() == source_path.native();
        });
    return found == snapshot_.records.end() ? nullptr : &*found;
}

} // namespace music_player::m0
