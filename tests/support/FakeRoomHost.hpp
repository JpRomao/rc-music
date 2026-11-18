#pragma once

// In-memory collaborators of ListeningRoomHost for application-layer tests: the host's player
// (LocalFileQueue + PlayerController + DeterministicPlaybackBackend, silent), a track catalog and a
// roster store. Every callback is synchronous unless FakeRoomTrackCatalog::deferred is set.

#include "m0/adapters/DeterministicPlaybackBackend.hpp"
#include "m0/application/PlayerController.hpp"
#include "m0/domain/LocalFileQueue.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/PlatformIntegration.hpp"
#include "m0/ports/RoomHostPlayer.hpp"
#include "m0/ports/RoomRosterStore.hpp"
#include "m0/ports/RoomTrackCatalog.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace music_player::m0::testing {

class SilentPlatformIntegration final : public PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return false; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}
};

// Starts Playing at the first item (the DeterministicPlaybackBackend default: 84 s of 248 s).
class FakeRoomHostPlayer final : public RoomHostPlayer {
  public:
    explicit FakeRoomHostPlayer(std::vector<std::filesystem::path> sources)
        : controller_(backend_, platform_, backend_.snapshot()) {
        backend_.setEventCallback([this](const PlaybackEvent& event) {
            controller_.applyBackendSnapshot(event.snapshot);
            changed();
        });
        static_cast<void>(queue_.replace(std::move(sources)));
        if (!queue_.empty())
            static_cast<void>(queue_.select(0));
    }

    [[nodiscard]] RoomHostPlayerState state() const override {
        const auto& playback = controller_.confirmedSnapshot();
        return {queue_.snapshot(), playback.status, playback.position_ms};
    }
    bool next() override {
        const auto current = queue_.currentIndex();
        if (!current)
            return false;
        if (*current + 1 < queue_.size())
            return selectAndLoad(*current + 1);
        return repeat && selectAndLoad(0);
    }
    bool previous() override {
        const auto current = queue_.currentIndex();
        if (!current)
            return false;
        if (*current > 0)
            return selectAndLoad(*current - 1);
        return repeat && selectAndLoad(queue_.size() - 1);
    }
    bool seek(const std::int64_t position_ms) override {
        Batch batch{*this};
        controller_.beginSeekPreview(position_ms);
        if (!controller_.commitSeekPreview())
            return false;
        static_cast<void>(backend_.tick());
        return true;
    }
    bool pause() override { return controller_.pause(); }
    bool play() override { return controller_.play(); }
    std::optional<LocalQueueOccurrenceId> enqueue(const std::filesystem::path& source) override {
        if (!queue_.append({source}))
            return std::nullopt;
        const auto id = queue_.items().back().occurrence_id;
        changed();
        return id;
    }
    bool remove(const LocalQueueOccurrenceId occurrence) override {
        if (!queue_.remove(occurrence))
            return false;
        changed();
        return true;
    }
    bool move(const LocalQueueOccurrenceId occurrence, const std::size_t destination_index) override {
        if (!queue_.move(occurrence, destination_index))
            return false;
        changed();
        return true;
    }

    // Host-only controls, used by the host's own UI and never by guests.
    // Stand-in for PlayerPresenter::shuffleQueue: reorders the items after the current one (reversed,
    // to stay deterministic).
    void shuffleUpcoming() {
        const auto current = queue_.currentIndex();
        const std::size_t first = current ? *current + 1 : 0;
        std::vector<LocalQueueOccurrenceId> upcoming;
        for (std::size_t index = first; index < queue_.size(); ++index)
            upcoming.push_back(queue_.items()[index].occurrence_id);
        for (std::size_t offset = 0; offset < upcoming.size(); ++offset)
            static_cast<void>(queue_.move(upcoming[upcoming.size() - 1 - offset], first + offset));
        changed();
    }
    // The current track reaches its end: advance, or apply the end-of-queue behaviour.
    void finishTrack() {
        if (next())
            return;
        Batch batch{*this};
        if (queue_end == QueueEndBehavior::StopClearSelection) {
            static_cast<void>(queue_.clearSelection());
            static_cast<void>(backend_.unload());
        } else {
            static_cast<void>(controller_.pause());
        }
        changed();
    }
    [[nodiscard]] SessionSnapshot sessionSnapshot() const {
        const auto playback = state();
        return {queue_.snapshot(), queue_.currentOccurrenceId() ? playback.position_ms : 0, {}, {}};
    }
    [[nodiscard]] std::vector<std::filesystem::path> paths() const {
        std::vector<std::filesystem::path> result;
        for (const auto& item : queue_.items())
            result.push_back(item.source_path);
        return result;
    }
    [[nodiscard]] std::optional<std::size_t> currentIndex() const { return queue_.currentIndex(); }
    [[nodiscard]] std::uint64_t occurrenceAt(const std::size_t index) const {
        return queue_.items()[index].occurrence_id.value;
    }

    bool repeat{false};
    QueueEndBehavior queue_end{QueueEndBehavior::StopKeepSelection};

  private:
    // Groups the notifications of one compound command into a single onChanged.
    struct Batch final {
        explicit Batch(FakeRoomHostPlayer& player) : owner(player) { ++owner.batch_depth_; }
        ~Batch() {
            if (--owner.batch_depth_ == 0 && owner.batch_changed_) {
                owner.batch_changed_ = false;
                owner.changed();
            }
        }
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        FakeRoomHostPlayer& owner;
    };

    bool selectAndLoad(const std::size_t index) {
        Batch batch{*this};
        const bool was_playing = controller_.confirmedSnapshot().status == PlaybackStatus::Playing;
        if (!queue_.select(index) ||
            backend_.load(queue_.items()[index].source_path) != PlaybackCommandResult::Accepted)
            return false;
        if (was_playing)
            static_cast<void>(controller_.play());
        changed();
        return true;
    }
    void changed() {
        if (batch_depth_ > 0) {
            batch_changed_ = true;
            return;
        }
        if (onChanged)
            onChanged();
    }

    DeterministicPlaybackBackend backend_{};
    SilentPlatformIntegration platform_{};
    PlayerController controller_;
    LocalFileQueue queue_{};
    int batch_depth_{0};
    bool batch_changed_{false};
};

// Describes every path with a fake hash derived from it; paths in `unreadable` fail.
class FakeRoomTrackCatalog final : public RoomTrackCatalog {
  public:
    void describe(std::filesystem::path source,
                  std::function<void(std::optional<RoomTrackDescription>)> done) override {
        requests.push_back(source);
        if (deferred) {
            pending_.emplace_back(std::move(source), std::move(done));
            return;
        }
        done(lookup(source));
    }
    void completeAll() {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& [source, done] : pending)
            done(lookup(source));
    }
    [[nodiscard]] static std::string fakeSha256(const std::filesystem::path& source) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string hash(64, '0');
        std::uint64_t value = 1469598103934665603ULL;
        for (const char c : source.generic_string())
            value = (value ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
        for (std::size_t index = 0; index < hash.size(); ++index)
            hash[index] = digits[(value >> ((index % 16) * 4)) & 0xF];
        return hash;
    }
    [[nodiscard]] std::optional<RoomTrackDescription> lookup(const std::filesystem::path& source) const {
        if (unreadable.contains(source))
            return std::nullopt;
        RoomTrackDescription description;
        description.item.origin = RoomItemOrigin::HostTrack;
        description.item.content_sha256 = fakeSha256(source);
        description.item.size_bytes = 1000;
        description.item.summary = {source.stem().string(), "Artista", "Album", 248'000};
        if (const auto found = lyrics.find(source); found != lyrics.end())
            description.lyrics_lrc = found->second;
        return description;
    }

    bool deferred{false};
    std::set<std::filesystem::path> unreadable;
    std::map<std::filesystem::path, std::string> lyrics;
    std::vector<std::filesystem::path> requests;

  private:
    std::vector<std::pair<std::filesystem::path, std::function<void(std::optional<RoomTrackDescription>)>>> pending_;
};

class InMemoryRoomRosterStore final : public RoomRosterStore {
  public:
    [[nodiscard]] RoomRosterLoadResult load() override { return {stored, status}; }
    [[nodiscard]] RoomRosterSaveStatus save(const RoomRosterSnapshot& snapshot) override {
        stored = snapshot;
        status = RoomRosterLoadStatus::Loaded;
        ++saves;
        return RoomRosterSaveStatus::Saved;
    }

    RoomRosterSnapshot stored{defaultRoomRoster()};
    RoomRosterLoadStatus status{RoomRosterLoadStatus::Missing};
    int saves{0};
};

} // namespace music_player::m0::testing
