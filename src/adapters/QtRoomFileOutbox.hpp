#pragma once

#include "adapters/RoomFileTransfer.hpp"
#include "m0/ports/RoomFiles.hpp"

#include <QObject>
#include <QTimer>

#include <map>
#include <memory>

namespace music_player::m0 {

// RoomFileOutbox over one RoomFileSender per link (ADR-0022, S8.2). Runs on the thread that owns the
// links (the GUI thread in the app): RoomLink has no "buffer low" callback, so a timer pumps every
// sender while any has chunks left, and a send pumps at once. Each pump reads at most
// room_media_buffer_high_water_bytes (in room_file_chunk_bytes reads), so the thread never blocks on a
// whole file. Links are held weakly: a link destroyed elsewhere is dropped at the next pump.
class QtRoomFileOutbox final : public RoomFileOutbox {
  public:
    QtRoomFileOutbox();
    ~QtRoomFileOutbox() override;
    QtRoomFileOutbox(const QtRoomFileOutbox&) = delete;
    QtRoomFileOutbox& operator=(const QtRoomFileOutbox&) = delete;

    bool send(const std::shared_ptr<RoomLink>& link, const std::filesystem::path& file, const std::string& sha256,
              std::uint32_t transfer_id, std::uint64_t offset) override;
    void cancel(const RoomLink& link, std::uint32_t transfer_id) override;
    void forget(const RoomLink& link) override;

  private:
    struct Peer final {
        std::weak_ptr<RoomLink> link;
        std::shared_ptr<RoomFileSender> sender;
    };
    void pumpAll();

    std::map<const RoomLink*, Peer> peers_;
    QTimer timer_;
};

} // namespace music_player::m0
