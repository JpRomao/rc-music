#pragma once

#include "m0/ports/RoomFiles.hpp"

#include <QObject>
#include <QString>
#include <QThread>

#include <cstdint>
#include <memory>
#include <vector>

namespace music_player::m0 {

// RoomFileInbox over RoomCache + RoomFileReceiver (ADR-0022, S8.2). Every file operation (stat,
// writing chunks, hashing, including the resume-prefix hash in accept(), and purging) runs on the
// inbox's own worker thread, so the GUI thread never waits on the disk; calls are queued to the worker
// in order and answers come back to the thread that created the inbox, in the same order.
//
// The cache is disposable: construction purges it (app startup), and purge() abandons every transfer
// in flight before RoomCache::purgeAll(), so its quota accounting never drifts. A file still open when the
// cache is purged (Windows: the player closing the room's current item) is deleted again a little later,
// for a few seconds, unless the next room asks for that hash first; the next startup purge catches anything
// left after that. Destruction waits for the worker to finish what is queued; queued answers are then dropped.
class QtRoomFileInbox final : public QObject, public RoomFileInbox {
  public:
    explicit QtRoomFileInbox(QString config_directory, QObject* parent = nullptr);
    // S12.1: the host's upload cache, <config>/<directory_name> ("room-uploads"), refusing files above
    // maximum_file_bytes (maximum_guest_upload_bytes).
    QtRoomFileInbox(QString config_directory, const QString& directory_name, std::uint64_t maximum_file_bytes,
                    QObject* parent = nullptr);
    ~QtRoomFileInbox() override;
    QtRoomFileInbox(const QtRoomFileInbox&) = delete;
    QtRoomFileInbox& operator=(const QtRoomFileInbox&) = delete;

    void status(const std::string& sha256, std::function<void(RoomFileStatus)> done) override;
    void accept(const RoomFileOffer& offer) override;
    void consume(RoomFileChunk chunk) override;
    void abandon(std::uint32_t transfer_id) override;
    void markPlayed(const std::string& sha256) override;
    void purge() override;
    void discard(const std::string& sha256) override;

    // <config>/room-cache (or the directory given).
    [[nodiscard]] QString cacheDirectory() const;
    // Blocks until the worker has done everything queued so far (tests).
    void waitForWorker();

  private:
    struct Worker;
    void post(std::function<void(Worker&)> task);
    // Worker thread: what purgeAll() could not delete is tried again shortly.
    void purgeLater(Worker& worker, std::vector<QString> left);
    void retryPurge(Worker& worker);

    QString cache_directory_;
    QThread thread_;
    QObject* context_{nullptr}; // lives on thread_
    std::unique_ptr<Worker> worker_;
};

} // namespace music_player::m0
