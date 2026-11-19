#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

// Files received in a listening room (ADR-0022), kept in <config>/room-cache and named only by
// their content hash: a partial file is "<sha>.part" and becomes "<sha>" (no extension; FFmpeg
// probes the content) only after RoomFileReceiver has checked its SHA-256. Every method validates
// the hash itself (exactly 64 lowercase hex digits), so no caller-supplied text reaches a path.
// The whole cache is disposable: purgeAll() runs when leaving a room and at startup. The host keeps the
// files guests upload (S12.1) in another instance, <config>/room-uploads, under the same rules.
//
// Quota: what is on disk in the directory (complete files and .part files) plus the bytes
// reserved for transfers in flight stays within room_cache_quota_bytes. A receiver reserves the
// bytes it still expects and release()s them as they land on disk or when the transfer ends.
// To make room, reserve() deletes complete files passed to markPlayed(), oldest mark first; files
// not marked and .part files are never evicted. Used on the thread that owns it.
class RoomCache {
  public:
    // <config>/<directory_name>; directory_name is a fixed name chosen by the app, never a caller's text.
    explicit RoomCache(QString config_directory, const QString& directory_name = QStringLiteral("room-cache"));

    [[nodiscard]] static bool isValidSha256(std::string_view sha256) noexcept;
    [[nodiscard]] QString directory() const;
    // Only complete, verified files; nullopt for an invalid hash or a file still partial.
    [[nodiscard]] std::optional<QString> pathFor(std::string_view sha256) const;
    // "<dir>/<sha>.part" for a valid hash, whether it exists or not.
    [[nodiscard]] std::optional<QString> partialPathFor(std::string_view sha256) const;
    // false (and nothing reserved) when the bytes do not fit even after evicting played files.
    [[nodiscard]] bool reserve(std::uint64_t bytes);
    // Gives back reserved bytes that will not be written or that are now on disk.
    void release(std::uint64_t bytes) noexcept;
    // The item was played: its complete file may be evicted by reserve(). Invalid hashes are ignored.
    void markPlayed(std::string_view sha256);
    // The file is wanted again (S12.1: a guest item queued again from it): takes back its played mark.
    void keep(std::string_view sha256);
    // Removes every .part and complete file; reservations and played marks are forgotten.
    void purgeAll();
    // Removes the complete file and the .part of one hash (S12.1: a refused upload, or a guest item that left
    // the queue). A complete file that cannot go yet (still open elsewhere, on Windows) is marked as played, so
    // reserve() evicts it later. Invalid hashes are ignored.
    void remove(std::string_view sha256);

  private:
    [[nodiscard]] std::uint64_t bytesOnDisk() const;

    QString directory_;
    std::uint64_t reserved_bytes_{};
    std::vector<std::string> played_;
};

} // namespace music_player::m0
