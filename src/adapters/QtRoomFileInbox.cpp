#include "adapters/QtRoomFileInbox.hpp"

#include "adapters/RoomCache.hpp"
#include "adapters/RoomFileTransfer.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>

#include <map>
#include <utility>

namespace music_player::m0 {

// Touched only on the worker thread.
struct QtRoomFileInbox::Worker final {
    Worker(QString config_directory, const QString& directory_name, const std::uint64_t maximum_file_bytes)
        : cache(std::move(config_directory), directory_name), receiver(cache, maximum_file_bytes) {}

    RoomCache cache;
    RoomFileReceiver receiver;
    // Transfers the receiver accepted and has not finished (transfer_id -> sha).
    std::map<std::uint32_t, std::string> transfers;
    // Files a purge could not delete yet (still open), tried again until purge_retries_left runs out.
    std::vector<QString> purge_leftovers;
    int purge_retries_left{};
    bool purge_retry_scheduled{};

    // The hash is wanted by a room again: its leftover file (or .part) is no longer the old room's.
    void claim(const std::string& sha256) {
        const auto name = QString::fromStdString(sha256);
        std::erase_if(purge_leftovers,
                      [&](const QString& left) { return left == name || left == name + QStringLiteral(".part"); });
    }
};

namespace {
constexpr int purge_retry_interval_ms = 250;
constexpr int purge_retry_attempts = 40; // about ten seconds
} // namespace

QtRoomFileInbox::QtRoomFileInbox(QString config_directory, QObject* parent)
    : QtRoomFileInbox(std::move(config_directory), QStringLiteral("room-cache"), room_cache_quota_bytes, parent) {}

QtRoomFileInbox::QtRoomFileInbox(QString config_directory, const QString& directory_name,
                                 const std::uint64_t maximum_file_bytes, QObject* parent)
    : QObject(parent),
      worker_(std::make_unique<Worker>(std::move(config_directory), directory_name, maximum_file_bytes)) {
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
    worker_->receiver.onFailed = [this](std::string sha256, const RoomTransferError error) {
        // The receiver has one transfer per hash: the one recorded for it is the one that ended.
        std::uint32_t transfer_id = 0;
        std::erase_if(worker_->transfers, [&](const auto& transfer) {
            if (transfer.second != sha256)
                return false;
            transfer_id = transfer.first;
            return true;
        });
        const auto failure = error == RoomTransferError::Gap ? RoomFileFailure::Gap : RoomFileFailure::Failed;
        QMetaObject::invokeMethod(
            this,
            [this, sha256 = std::move(sha256), transfer_id, failure] {
                if (onFailed)
                    onFailed(sha256, transfer_id, failure);
            },
            Qt::QueuedConnection);
    };
    context_ = new QObject;
    context_->moveToThread(&thread_);
    QObject::connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
    thread_.setObjectName(QStringLiteral("rc-music-room-cache"));
    thread_.start(QThread::LowPriority);
    // App startup: whatever a previous run left behind goes.
    post([this](Worker& worker) { purgeLater(worker, worker.cache.purgeAll()); });
}

QtRoomFileInbox::~QtRoomFileInbox() {
    // Everything queued runs first (a purge from leaving the room at exit must not be lost).
    waitForWorker();
    thread_.quit();
    thread_.wait();
}

void QtRoomFileInbox::status(const std::string& sha256, std::function<void(RoomFileStatus)> done) {
    post([this, sha256, done = std::move(done)](Worker& worker) {
        worker.claim(sha256);
        RoomFileStatus status;
        if (const auto file = worker.cache.pathFor(sha256)) {
            status.complete = true;
            status.file = std::filesystem::path{file->toStdU16String()};
            status.size_bytes = static_cast<std::uint64_t>(QFileInfo(*file).size());
            // Asked for again: wanted, so no longer evictable (a played mark, or a discard that could not
            // delete it while it was open).
            worker.cache.keep(sha256);
        } else if (const auto resume = worker.receiver.resumeRequest(sha256)) {
            status.resume_offset = resume->offset;
        }
        QMetaObject::invokeMethod(this, [done, status] { done(status); }, Qt::QueuedConnection);
    });
}

void QtRoomFileInbox::accept(const RoomFileOffer& offer) {
    post([offer](Worker& worker) {
        worker.claim(offer.content_sha256);
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
    post([this](Worker& worker) {
        // Abandoned first, so their reservations are released before the files go.
        for (const auto& transfer : std::exchange(worker.transfers, {}))
            worker.receiver.abandon(transfer.first);
        purgeLater(worker, worker.cache.purgeAll());
    });
}

void QtRoomFileInbox::purgeLater(Worker& worker, std::vector<QString> left) {
    worker.purge_leftovers = std::move(left);
    worker.purge_retries_left = purge_retry_attempts;
    if (worker.purge_leftovers.empty() || worker.purge_retry_scheduled)
        return;
    worker.purge_retry_scheduled = true;
    QTimer::singleShot(purge_retry_interval_ms, context_, [this, &worker] { retryPurge(worker); });
}

void QtRoomFileInbox::retryPurge(Worker& worker) {
    worker.purge_retry_scheduled = false;
    const QDir directory(worker.cache.directory());
    std::erase_if(worker.purge_leftovers, [&](const QString& name) {
        const auto file = directory.filePath(name);
        return !QFileInfo::exists(file) || QFile::remove(file);
    });
    if (worker.purge_leftovers.empty()) {
        // Only an empty directory goes (rmdir): a new room may already be receiving into it.
        QDir().rmdir(directory.path());
        return;
    }
    if (--worker.purge_retries_left <= 0) {
        worker.purge_leftovers.clear(); // the next startup purge removes it
        return;
    }
    worker.purge_retry_scheduled = true;
    QTimer::singleShot(purge_retry_interval_ms, context_, [this, &worker] { retryPurge(worker); });
}

void QtRoomFileInbox::discard(const std::string& sha256) {
    post([sha256](Worker& worker) {
        worker.claim(sha256);
        // Its transfer stops first (releasing its reservation), so no later chunk writes the file again.
        std::erase_if(worker.transfers, [&](const auto& transfer) {
            if (transfer.second != sha256)
                return false;
            worker.receiver.abandon(transfer.first);
            return true;
        });
        worker.cache.remove(sha256);
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
