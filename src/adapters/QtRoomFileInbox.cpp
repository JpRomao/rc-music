#include "adapters/QtRoomFileInbox.hpp"

#include "adapters/RoomCache.hpp"
#include "adapters/RoomFileTransfer.hpp"

#include <QMetaObject>

#include <map>
#include <utility>

namespace music_player::m0 {

// Touched only on the worker thread.
struct QtRoomFileInbox::Worker final {
    explicit Worker(QString config_directory) : cache(std::move(config_directory)), receiver(cache) {}

    RoomCache cache;
    RoomFileReceiver receiver;
    // Transfers the receiver accepted and has not finished (transfer_id -> sha).
    std::map<std::uint32_t, std::string> transfers;
};

QtRoomFileInbox::QtRoomFileInbox(QString config_directory, QObject* parent)
    : QObject(parent), worker_(std::make_unique<Worker>(std::move(config_directory))) {
    cache_directory_ = worker_->cache.directory();
    worker_->receiver.onComplete = [this](std::string sha256, QString file) {
        std::erase_if(worker_->transfers, [&](const auto& transfer) { return transfer.second == sha256; });
        QMetaObject::invokeMethod(
            this,
            [this, sha256 = std::move(sha256), file = std::move(file)] {
                if (onComplete)
                    onComplete(sha256, std::filesystem::path{file.toStdU16String()});
            },
            Qt::QueuedConnection);
    };
    worker_->receiver.onFailed = [this](std::string sha256, RoomTransferError) {
        std::erase_if(worker_->transfers, [&](const auto& transfer) { return transfer.second == sha256; });
        QMetaObject::invokeMethod(
            this,
            [this, sha256 = std::move(sha256)] {
                if (onFailed)
                    onFailed(sha256);
            },
            Qt::QueuedConnection);
    };
    context_ = new QObject;
    context_->moveToThread(&thread_);
    QObject::connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
    thread_.setObjectName(QStringLiteral("rc-music-room-cache"));
    thread_.start(QThread::LowPriority);
    // App startup: whatever a previous run left behind goes.
    post([](Worker& worker) { worker.cache.purgeAll(); });
}

QtRoomFileInbox::~QtRoomFileInbox() {
    // Everything queued runs first (a purge from leaving the room at exit must not be lost).
    waitForWorker();
    thread_.quit();
    thread_.wait();
}

void QtRoomFileInbox::status(const std::string& sha256, std::function<void(RoomFileStatus)> done) {
    post([this, sha256, done = std::move(done)](Worker& worker) {
        RoomFileStatus status;
        if (const auto file = worker.cache.pathFor(sha256)) {
            status.complete = true;
            status.file = std::filesystem::path{file->toStdU16String()};
        } else if (const auto resume = worker.receiver.resumeRequest(sha256)) {
            status.resume_offset = resume->offset;
        }
        QMetaObject::invokeMethod(this, [done, status] { done(status); }, Qt::QueuedConnection);
    });
}

void QtRoomFileInbox::accept(const RoomFileOffer& offer) {
    post([offer](Worker& worker) {
        // Recorded first: a resumed file already whole completes inside accept().
        worker.transfers[offer.transfer_id] = offer.content_sha256;
        if (!worker.receiver.accept(offer))
            worker.transfers.erase(offer.transfer_id);
    });
}

void QtRoomFileInbox::consume(RoomFileChunk chunk) {
    post([chunk = std::make_shared<RoomFileChunk>(std::move(chunk))](Worker& worker) {
        static_cast<void>(worker.receiver.consume(*chunk));
    });
}

void QtRoomFileInbox::abandon(const std::uint32_t transfer_id) {
    post([transfer_id](Worker& worker) {
        worker.receiver.abandon(transfer_id);
        worker.transfers.erase(transfer_id);
    });
}

void QtRoomFileInbox::markPlayed(const std::string& sha256) {
    post([sha256](Worker& worker) { worker.cache.markPlayed(sha256); });
}

void QtRoomFileInbox::purge() {
    post([](Worker& worker) {
        // Abandoned first, so their reservations are released before the files go.
        for (const auto& transfer : std::exchange(worker.transfers, {}))
            worker.receiver.abandon(transfer.first);
        worker.cache.purgeAll();
    });
}

QString QtRoomFileInbox::cacheDirectory() const { return cache_directory_; }

void QtRoomFileInbox::waitForWorker() {
    if (thread_.isRunning())
        QMetaObject::invokeMethod(context_, [] {}, Qt::BlockingQueuedConnection);
}

void QtRoomFileInbox::post(std::function<void(Worker&)> task) {
    QMetaObject::invokeMethod(
        context_, [worker = worker_.get(), task = std::move(task)] { task(*worker); }, Qt::QueuedConnection);
}

} // namespace music_player::m0
