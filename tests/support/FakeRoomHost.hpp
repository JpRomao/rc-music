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

// Starts Playing at the first item (the DeterministicPlaybackBackend default: 84 s of 248 s). Its backend
// confirms each command at once, or, once `post` is set, on a later turn through it, as the presenter's
// queued connection delivers backend events in the app. Like PlayerPresenter, state() reports where the
// player is heading (an accepted command, a held start), a track loaded to play starts on its Ready
// confirmation, so a hold can still stop it, and a resume is offered to the room (held, then notified)
// before the backend hears it.
class FakeRoomHostPlayer final : public RoomHostPlayer {
  public:
    explicit FakeRoomHostPlayer(std::vector<std::filesystem::path> sources, const bool select_first = true)
        : controller_(backend_, platform_, backend_.snapshot()) {
        backend_.setEventCallback([this](const PlaybackEvent& event) {
            if (post) {
                post([this, snapshot = event.snapshot] { confirm(snapshot); });
                return;
            }
            confirm(event.snapshot);
        });
        static_cast<void>(queue_.replace(std::move(sources)));
        if (!select_first)
            static_cast<void>(queue_.clearSelection());
        else if (!queue_.empty())
            static_cast<void>(queue_.select(0));
    }

    // The host picks an item and plays it (a double click in its queue), also when nothing was current.
    bool playAt(const std::size_t index) { return selectAndLoad(index, true); }

    [[nodiscard]] RoomHostPlayerState state() const override {
        const auto status = held() || autoplay_ ? PlaybackStatus::Playing : controller_.effectivePlaybackStatus();
        const auto position =
            controller_.seekPending() ? controller_.displayedPositionMs() : controller_.confirmedSnapshot().position_ms;
        const bool starting = status == PlaybackStatus::Playing &&
                              (held() || controller_.confirmedSnapshot().status != PlaybackStatus::Playing);
        return {queue_.snapshot(), status, position, seeks_, starting};
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
        ++seeks_;
        static_cast<void>(backend_.tick());
        changed();
        return true;
    }
    bool pause() override {
        if (!held())
            return controller_.pause();
        hold_.reset();
        changed();
        return true;
    }
    bool play() override {
        if (held())
            return true;
        // Offered to the room before the backend hears it: a holdStart from this notification keeps it.
        const auto status = controller_.effectivePlaybackStatus();
        if (onChanged && batch_depth_ == 0 && queue_.currentOccurrenceId() &&
            (status == PlaybackStatus::Ready || status == PlaybackStatus::Paused)) {
            hold_ = backend_.snapshot().generation;
            offered_ = true;
            changed();
            if (held() && !offered_)
                return true;
            hold_.reset();
            offered_ = false;
        }
        return controller_.play();
    }
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
    bool holdStart() override {
        if (!queue_.currentOccurrenceId())
            return false;
        if (held()) {
            offered_ = false;
            return true;
        }
        if (autoplay_)
            autoplay_ = false; // still waiting for Ready: it stays there
        else if (controller_.effectivePlaybackStatus() != PlaybackStatus::Playing || !controller_.pause())
            return false;
        // Silent now, even while the controller's pause waits for an earlier command's confirmation.
        if (backend_.snapshot().status == PlaybackStatus::Playing)
            static_cast<void>(backend_.pause());
        hold_ = backend_.snapshot().generation;
        changed();
        return true;
    }
    bool releaseStart() override {
        if (!held())
            return false;
        hold_.reset();
        if (!refuse_start && controller_.play())
            return true;
        changed();
        return false;
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
        hold_.reset();
        autoplay_ = false;
        if (queue_end == QueueEndBehavior::StopClearSelection) {
            static_cast<void>(queue_.clearSelection());
            static_cast<void>(backend_.unload());
        } else {
            static_cast<void>(controller_.pause());
        }
        changed();
    }
    // One step of normal playback (DeterministicPlaybackConfig::tick_interval_ms). Like a presenter
    // that reports positions, the fake notifies onChanged for it.
    void tick() { static_cast<void>(backend_.tick()); }
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
    // What the backend does right now, confirmed or not: Playing is what the host hears.
    [[nodiscard]] PlaybackStatus audible() const { return backend_.snapshot().status; }
    [[nodiscard]] std::int64_t audiblePosition() const { return backend_.snapshot().position_ms; }

    bool repeat{false};
    QueueEndBehavior queue_end{QueueEndBehavior::StopKeepSelection};
    // Hands a backend confirmation to a later turn (see the class comment).
    std::function<void(std::function<void()>)> post;
    // The player refuses to start a held start when released (a backend that lost its output, say).
    bool refuse_start{false};

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

    // Loads the item; it plays when asked or when the player was playing (or about to).
    bool selectAndLoad(const std::size_t index, const bool play = false) {
        Batch batch{*this};
        const bool starts = play || state().status == PlaybackStatus::Playing;
        if (!queue_.select(index))
            return false;
        hold_.reset();
        // Set before the load: a synchronous Ready confirmation starts it at once.
        autoplay_ = starts;
        if (backend_.load(queue_.items()[index].source_path) != PlaybackCommandResult::Accepted) {
            autoplay_ = false;
            return false;
        }
        // Like PlayerPresenter after a load: the new track's state does not wait for its queued event.
        controller_.applyBackendSnapshot(backend_.snapshot());
        changed();
        return true;
    }
    void confirm(const PlaybackSnapshot& snapshot) {
        controller_.applyBackendSnapshot(snapshot);
        if (autoplay_ && snapshot.status == PlaybackStatus::Ready &&
            snapshot.generation == backend_.snapshot().generation) {
            autoplay_ = false;
            static_cast<void>(controller_.play());
        }
        changed();
    }
    [[nodiscard]] bool held() const { return hold_ == std::optional{backend_.snapshot().generation}; }
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
    // The generation whose start is held (holdStart), and a loaded track waiting for Ready to play. offered_:
    // a resume held by play() itself that the room has not taken yet.
    std::optional<std::uint64_t> hold_;
    bool autoplay_{false};
    bool offered_{false};
    std::uint64_t seeks_{0};
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
        description.item.size_bytes = size_override.value_or(1000);
        description.item.summary =
            summary_override.value_or(RoomTrackSummary{source.stem().string(), "Artista", "Album", 248'000});
        if (const auto found = lyrics.find(source); found != lyrics.end())
            description.lyrics_lrc = found->second;
        return description;
    }

    bool deferred{false};
    std::optional<RoomTrackSummary> summary_override;
    std::optional<std::uint64_t> size_override;
    std::set<std::filesystem::path> unreadable;
    std::map<std::filesystem::path, std::string> lyrics;
    std::vector<std::filesystem::path> requests;

  private:
    std::vector<std::pair<std::filesystem::path, std::function<void(std::optional<RoomTrackDescription>)>>> pending_;
};

// Checks uploaded files: every file is valid audio with `summary` unless it is in `invalid`. Answers at
// once, or keeps the answers for completeAll() when `deferred` is set.
class FakeRoomUploadProbe final : public RoomUploadProbe {
  public:
    void probe(std::filesystem::path file, std::function<void(std::optional<RoomTrackSummary>)> done) override {
        probed.push_back(file);
        if (deferred) {
            pending_.emplace_back(std::move(file), std::move(done));
            return;
        }
        done(lookup(file));
    }
    void completeAll() {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& [file, done] : pending)
            done(lookup(file));
    }
    [[nodiscard]] std::optional<RoomTrackSummary> lookup(const std::filesystem::path& file) const {
        if (invalid.contains(file))
            return std::nullopt;
        return summary;
    }

    bool deferred{false};
    RoomTrackSummary summary{"Minha", "Eu", "Demo", 61'000};
    std::set<std::filesystem::path> invalid;
    std::vector<std::filesystem::path> probed;

  private:
    std::vector<std::pair<std::filesystem::path, std::function<void(std::optional<RoomTrackSummary>)>>> pending_;
};

class InMemoryRoomRosterStore final : public RoomRosterStore {
  public:
    [[nodiscard]] RoomRosterLoadResult load() override { return {stored, status}; }
    [[nodiscard]] RoomRosterSaveStatus save(const RoomRosterSnapshot& snapshot) override {
        ++saves;
        if (save_result != RoomRosterSaveStatus::Saved)
            return save_result;
        stored = snapshot;
        status = RoomRosterLoadStatus::Loaded;
        return RoomRosterSaveStatus::Saved;
    }

    RoomRosterSnapshot stored{defaultRoomRoster()};
    RoomRosterLoadStatus status{RoomRosterLoadStatus::Missing};
    // Failed or BlockedByFutureVersion: the file keeps what it had.
    RoomRosterSaveStatus save_result{RoomRosterSaveStatus::Saved};
    int saves{0};
};

} // namespace music_player::m0::testing
