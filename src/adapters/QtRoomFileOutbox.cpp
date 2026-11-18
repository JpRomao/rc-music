#include "adapters/QtRoomFileOutbox.hpp"

#include <QString>

#include <chrono>
#include <vector>

namespace music_player::m0 {
namespace {
// Often enough to keep a 1 MiB media buffer topped up on a LAN, rare enough to cost nothing when idle
// (the timer only runs while chunks are left).
constexpr std::chrono::milliseconds pump_interval{10};
} // namespace

QtRoomFileOutbox::QtRoomFileOutbox() {
    timer_.setInterval(pump_interval);
    QObject::connect(&timer_, &QTimer::timeout, [this] { pumpAll(); });
}

QtRoomFileOutbox::~QtRoomFileOutbox() { timer_.stop(); }

bool QtRoomFileOutbox::send(const std::shared_ptr<RoomLink>& link, const std::filesystem::path& file,
                            const std::string& sha256, const std::uint32_t transfer_id, const std::uint64_t offset) {
    if (!link)
        return false;
    auto& peer = peers_[link.get()];
    if (peer.link.lock() != link)
        peer = Peer{link, std::make_shared<RoomFileSender>()};
    // Held here: an in-process link may deliver during the pump and lead to forget().
    const auto sender = peer.sender;
    if (!sender->offer(*link, QString::fromStdU16String(file.u16string()), sha256, transfer_id, offset))
        return false;
    sender->pump(*link);
    if (!sender->idle() && !timer_.isActive())
        timer_.start();
    return true;
}

void QtRoomFileOutbox::cancel(const RoomLink& link, const std::uint32_t transfer_id) {
    if (const auto found = peers_.find(&link); found != peers_.end())
        found->second.sender->cancel(transfer_id);
}

void QtRoomFileOutbox::forget(const RoomLink& link) { peers_.erase(&link); }

void QtRoomFileOutbox::pumpAll() {
    // A pump may deliver to the peer at once (an in-process link): look each peer up again.
    std::vector<const RoomLink*> keys;
    for (const auto& peer : peers_)
        keys.push_back(peer.first);
    bool busy = false;
    for (const auto* key : keys) {
        auto found = peers_.find(key);
        if (found == peers_.end())
            continue;
        const auto link = found->second.link.lock();
        if (!link) {
            peers_.erase(found);
            continue;
        }
        const auto sender = found->second.sender;
        sender->pump(*link);
        busy = busy || (peers_.contains(key) && !sender->idle());
    }
    if (!busy)
        timer_.stop();
}

} // namespace music_player::m0
