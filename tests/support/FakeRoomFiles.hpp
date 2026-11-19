#pragma once

// In-memory implementations of m0/ports/RoomFiles.hpp for application-layer tests: they only record
// the calls. The inbox answers status() at once from `complete`/`partial`, or keeps the answers for
// answerAll() when `deferred` is set, like the worker of the real adapter.

#include "m0/ports/RoomFiles.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace music_player::m0::testing {

class FakeRoomFileOutbox final : public RoomFileOutbox {
  public:
    struct Sent final {
        std::string peer;
        std::filesystem::path file;
        std::string sha256;
        std::uint32_t transfer_id{};
        std::uint64_t offset{};
    };
    bool send(const std::shared_ptr<RoomLink>& link, const std::filesystem::path& file, const std::string& sha256,
              const std::uint32_t transfer_id, const std::uint64_t offset) override {
        if (!accepts)
            return false;
        sent.push_back({link->peerFingerprint(), file, sha256, transfer_id, offset});
        return true;
    }
    void cancel(const RoomLink& link, const std::uint32_t transfer_id) override {
        cancelled.emplace_back(link.peerFingerprint(), transfer_id);
    }
    void forget(const RoomLink& link) override { forgotten.push_back(link.peerFingerprint()); }

    [[nodiscard]] std::vector<std::string> sentHashes() const {
        std::vector<std::string> result;
        for (const auto& item : sent)
            result.push_back(item.sha256);
        return result;
    }
    [[nodiscard]] std::uint32_t transferOf(const std::string& sha256) const {
        for (auto item = sent.rbegin(); item != sent.rend(); ++item)
            if (item->sha256 == sha256)
                return item->transfer_id;
        return 0;
    }

    bool accepts{true};
    std::vector<Sent> sent;
    std::vector<std::pair<std::string, std::uint32_t>> cancelled;
    std::vector<std::string> forgotten;
};

class FakeRoomFileInbox final : public RoomFileInbox {
  public:
    void status(const std::string& sha256, std::function<void(RoomFileStatus)> done) override {
        log.push_back("status:" + sha256.substr(0, 4));
        if (deferred) {
            pending_.emplace_back(sha256, std::move(done));
            return;
        }
        done(lookup(sha256));
    }
    void accept(const RoomFileOffer& offer) override {
        log.push_back("accept:" + std::to_string(offer.transfer_id));
        accepted.push_back(offer);
    }
    void consume(RoomFileChunk chunk) override { consumed.push_back(std::move(chunk)); }
    void abandon(const std::uint32_t transfer_id) override {
        log.push_back("abandon:" + std::to_string(transfer_id));
        abandoned.push_back(transfer_id);
    }
    void markPlayed(const std::string& sha256) override { played.push_back(sha256); }
    void purge() override {
        log.push_back("purge");
        ++purges;
    }
    void discard(const std::string& sha256) override {
        log.push_back("discard:" + sha256.substr(0, 4));
        discarded.push_back(sha256);
        files.erase(sha256);
        partial.erase(sha256);
    }

    void answerAll() {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& [sha, done] : pending)
            done(lookup(sha));
    }
    // The receiver finished: the file is in the cache.
    void complete(const std::string& sha256) {
        files[sha256] = std::filesystem::path{"cache"} / sha256;
        if (onComplete)
            onComplete(sha256, files[sha256]);
    }
    // The receiver ended transfer_id without the file.
    void fail(const std::string& sha256, const std::uint32_t transfer_id,
              const RoomFileFailure failure = RoomFileFailure::Failed) {
        if (onFailed)
            onFailed(sha256, transfer_id, failure);
    }
    [[nodiscard]] RoomFileStatus lookup(const std::string& sha256) const {
        if (const auto found = files.find(sha256); found != files.end())
            return {true, 0, found->second};
        if (const auto found = partial.find(sha256); found != partial.end())
            return {false, found->second, {}};
        return {};
    }

    bool deferred{false};
    std::map<std::string, std::filesystem::path> files;
    std::map<std::string, std::uint64_t> partial;
    std::vector<std::string> log;
    std::vector<RoomFileOffer> accepted;
    std::vector<RoomFileChunk> consumed;
    std::vector<std::uint32_t> abandoned;
    std::vector<std::string> played;
    std::vector<std::string> discarded;
    int purges{0};

  private:
    std::vector<std::pair<std::string, std::function<void(RoomFileStatus)>>> pending_;
};

} // namespace music_player::m0::testing
