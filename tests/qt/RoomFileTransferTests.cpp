#include "adapters/RoomFileTransfer.hpp"

#include "../support/FakeRoomTransport.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace music_player::m0;
using music_player::m0::testing::FakeRoomLink;

namespace {
QString fixture(const char* name) { return QDir(QStringLiteral(RC_MUSIC_TEST_AUDIO_FIXTURES)).filePath(name); }

QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool writeAll(const QString& path, const QByteArray& bytes, const QIODevice::OpenMode mode = QIODevice::Truncate) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | mode) && file.write(bytes) == bytes.size();
}

std::string sha256(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

RoomFileChunk chunkOf(const QByteArray& bytes, const std::uint32_t transfer_id, const std::uint64_t offset) {
    const auto length = std::min<qsizetype>(static_cast<qsizetype>(room_file_chunk_bytes),
                                            bytes.size() - static_cast<qsizetype>(offset));
    const auto* begin = reinterpret_cast<const std::uint8_t*>(bytes.constData()) + offset;
    return {transfer_id, offset, std::vector<std::uint8_t>(begin, begin + length)};
}

QStringList filesUnder(const QString& directory) {
    QStringList files;
    QDirIterator it(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    return files;
}

// The host's link sends; the guest's link feeds the receiver like ListeningRoomGuest will.
struct Room {
    explicit Room(const QString& config) : cache(config), receiver(cache) {
        auto links = FakeRoomLink::pair("host-fp", "guest-fp");
        host = links.first;
        guest = links.second;
        guest->onControl = [this](const RoomControlMessage& message) {
            if (const auto* offer = std::get_if<RoomFileOffer>(&message))
                accepted.push_back(receiver.accept(*offer));
        };
        guest->onMedia = [this](RoomFileChunk chunk) {
            if (tamper)
                tamper(chunk);
            consumed.push_back(receiver.consume(chunk));
        };
        receiver.onComplete = [this](std::string sha, QString path) { completed.emplace_back(std::move(sha), path); };
        receiver.onFailed = [this](std::string sha, const RoomTransferError error) {
            failed.emplace_back(std::move(sha), error);
        };
    }
    void pumpUntilIdle() {
        for (int round = 0; round < 1000 && !sender.idle(); ++round)
            sender.pump(*host);
    }

    RoomCache cache;
    RoomFileReceiver receiver;
    RoomFileSender sender;
    std::shared_ptr<FakeRoomLink> host;
    std::shared_ptr<FakeRoomLink> guest;
    std::function<void(RoomFileChunk&)> tamper;
    std::vector<bool> accepted;
    std::vector<bool> consumed;
    std::vector<std::pair<std::string, QString>> completed;
    std::vector<std::pair<std::string, RoomTransferError>> failed;
};
} // namespace

class RoomFileTransferTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QVERIFY2(QFileInfo(fixture("wav-pcm-mono.wav")).size() > qint64{room_file_chunk_bytes}, "fixture");
    }

    void completeTransferDeliversTheVerifiedFile() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);

        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha, 7));
        QCOMPARE(room.host->sent_control.size(), std::size_t{1});
        QCOMPARE(std::get<RoomFileOffer>(room.host->sent_control.front()),
                 (RoomFileOffer{sha, static_cast<std::uint64_t>(original.size()), 7}));
        room.pumpUntilIdle();

        QCOMPARE(room.accepted, std::vector<bool>{true});
        QCOMPARE(room.host->sent_media.size(), std::size_t{2});
        QCOMPARE(room.host->sent_media[1].offset, std::uint64_t{room_file_chunk_bytes});
        QVERIFY(room.failed.empty());
        QCOMPARE(room.completed.size(), std::size_t{1});
        QCOMPARE(room.completed.front().first, sha);
        const auto received = room.completed.front().second;
        QCOMPARE(sha256(readAll(received)), sha);
        QCOMPARE(room.cache.pathFor(sha), std::optional<QString>{received});
        QCOMPARE(QFileInfo(received).fileName(), QString::fromStdString(sha));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
    }

    void declaredSizeSmallerThanRealFailsTooLargeAndDeletesThePart() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);

        QVERIFY(room.receiver.accept({sha, static_cast<std::uint64_t>(original.size()) - 1, 3}));
        QVERIFY(room.receiver.consume(chunkOf(original, 3, 0)));
        QVERIFY(QFileInfo::exists(*room.cache.partialPathFor(sha)));
        QVERIFY(!room.receiver.consume(chunkOf(original, 3, room_file_chunk_bytes)));

        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::TooLarge));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
        QVERIFY(!room.cache.pathFor(sha).has_value());
        QVERIFY(room.completed.empty());
    }

    void tamperedChunkFailsHashMismatch() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);
        room.tamper = [](RoomFileChunk& chunk) {
            if (chunk.offset == 0)
                chunk.payload[100] ^= 0x01;
        };

        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha, 1));
        room.pumpUntilIdle();

        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::HashMismatch));
        QCOMPARE(room.consumed.back(), false);
        QVERIFY(room.completed.empty());
        QVERIFY(!room.cache.pathFor(sha).has_value());
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
    }

    void chunkOutOfOrderFails() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);

        QVERIFY(room.receiver.accept({sha, static_cast<std::uint64_t>(original.size()), 4}));
        QVERIFY(!room.receiver.consume(chunkOf(original, 4, room_file_chunk_bytes)));
        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::OutOfOrder));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
        // The transfer is over: a later chunk is just ignored.
        QVERIFY(!room.receiver.consume(chunkOf(original, 4, 0)));
        QCOMPARE(room.failed.size(), std::size_t{1});
    }

    void fullQuotaFailsQuotaExceeded() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha256(original), 1));
        room.pumpUntilIdle();
        QCOMPARE(room.completed.size(), std::size_t{1});

        const std::string other(64, 'b');
        QVERIFY(!room.receiver.accept({other, room_cache_quota_bytes - 10, 2}));
        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(other, RoomTransferError::QuotaExceeded));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(other)));

        // Once played, the first file makes room.
        room.cache.markPlayed(sha256(original));
        QVERIFY(room.receiver.accept({other, room_cache_quota_bytes - 10, 2}));
        QVERIFY(!room.cache.pathFor(sha256(original)).has_value());
    }

    void offerAboveTheReceiverLimitFailsTooLarge() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        RoomFileReceiver uploads(cache, maximum_guest_upload_bytes);
        RoomFileReceiver guest(cache);
        std::vector<RoomTransferError> errors;
        uploads.onFailed = [&errors](std::string, const RoomTransferError error) { errors.push_back(error); };
        guest.onFailed = uploads.onFailed;
        const std::string sha(64, 'c');

        QVERIFY(!uploads.accept({sha, maximum_guest_upload_bytes + 1, 1}));
        QVERIFY(!guest.accept({sha, room_cache_quota_bytes + 1, 1}));
        QCOMPARE(errors, (std::vector{RoomTransferError::TooLarge, RoomTransferError::TooLarge}));
        QVERIFY(uploads.accept({sha, maximum_guest_upload_bytes, 1}));
    }

    void malformedOrBusyOffersAndUnknownChunksAreOnlyRefused() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const std::string sha(64, 'd');
        QVERIFY(!room.receiver.accept({sha, 0, 1}));
        QVERIFY(room.receiver.accept({sha, 10, 1}));
        QVERIFY(!room.receiver.accept({std::string(64, 'e'), 10, 1})); // transfer_id busy
        QVERIFY(!room.receiver.accept({sha, 10, 2}));                  // hash busy
        QVERIFY(!room.receiver.consume({99, 0, std::vector<std::uint8_t>(10, 1)}));
        QVERIFY(room.failed.empty());
        QVERIFY(room.completed.empty());
    }

    // Review Focus 1: a .part of 3 chunks continues from there and ends with the right hash.
    void resumeContinuesFromThePartAndChecksTheWholeFile() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto wav = readAll(fixture("wav-pcm-mono.wav"));
        const auto original = wav + wav + wav; // 4 whole chunks and a tail
        const auto source = QDir(config.path()).filePath("source.wav");
        QVERIFY(writeAll(source, original));
        const auto sha = sha256(original);
        QVERIFY(!room.receiver.resumeRequest(sha).has_value());

        QVERIFY(room.receiver.accept({sha, static_cast<std::uint64_t>(original.size()), 1}));
        for (std::uint64_t index = 0; index < 3; ++index)
            QVERIFY(room.receiver.consume(chunkOf(original, 1, index * room_file_chunk_bytes)));
        room.receiver.abandon(1); // the link dropped
        const auto part = *room.cache.partialPathFor(sha);
        QCOMPARE(QFileInfo(part).size(), qint64{3 * room_file_chunk_bytes});
        QVERIFY(writeAll(part, original.mid(3 * room_file_chunk_bytes, 100), QIODevice::Append)); // partial tail

        const auto request = room.receiver.resumeRequest(sha);
        QCOMPARE(request, (std::optional<RoomFileRequest>{RoomFileRequest{sha, 3 * room_file_chunk_bytes}}));
        QVERIFY(room.sender.offer(*room.host, source, request->content_sha256, 2, request->offset));
        room.pumpUntilIdle();

        QCOMPARE(room.host->sent_media.size(), std::size_t{2});
        QCOMPARE(room.host->sent_media.front().offset, std::uint64_t{3 * room_file_chunk_bytes});
        QVERIFY(room.failed.empty());
        QCOMPARE(room.completed.size(), std::size_t{1});
        QCOMPARE(room.completed.front().first, sha);
        QCOMPARE(readAll(room.completed.front().second), original);
    }

    void resumeWithACorruptedPrefixFailsHashMismatch() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);
        QVERIFY(QDir().mkpath(room.cache.directory()));
        auto prefix = original.left(room_file_chunk_bytes);
        prefix[5] = static_cast<char>(prefix[5] ^ 0x01);
        QVERIFY(writeAll(*room.cache.partialPathFor(sha), prefix));

        const auto request = room.receiver.resumeRequest(sha);
        QVERIFY(request.has_value());
        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha, 1, request->offset));
        room.pumpUntilIdle();

        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::HashMismatch));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
    }

    void purgeAllRemovesPartialAndReceivedFiles() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha256(original), 1));
        room.pumpUntilIdle();
        const auto other = readAll(fixture("flac-16-stereo.flac")) + original;
        QVERIFY(room.receiver.accept({sha256(other), static_cast<std::uint64_t>(other.size()), 2}));
        QVERIFY(room.receiver.consume(chunkOf(other, 2, 0)));
        room.receiver.abandon(2);
        QCOMPARE(filesUnder(room.cache.directory()).size(), 2);

        room.cache.purgeAll();

        QVERIFY(filesUnder(room.cache.directory()).isEmpty());
        QVERIFY(!room.cache.pathFor(sha256(original)).has_value());
        QVERIFY(!room.receiver.resumeRequest(sha256(other)).has_value());
    }

    // The file name comes only from a validated hash.
    void hashWithDotsOrSeparatorsIsRefused() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const std::string traversal = "../../" + std::string(58, 'a');
        const std::string separator = std::string(30, 'a') + "\\" + std::string(33, 'a');
        for (const auto& bad : {traversal, separator}) {
            QVERIFY(!room.receiver.accept({bad, 10, 1}));
            QVERIFY(!room.receiver.resumeRequest(bad).has_value());
            QVERIFY(!room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), bad, 1));
        }
        QVERIFY(room.host->sent_control.empty());
        QVERIFY(room.failed.empty());
        QVERIFY(filesUnder(config.path()).isEmpty());
    }

    void senderWaitsWhileTheMediaChannelIsFull() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        room.host->buffered_bytes = room_media_buffer_high_water_bytes;
        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha256(original), 1));
        room.sender.pump(*room.host);
        QVERIFY(room.host->sent_media.empty());
        QVERIFY(!room.sender.idle());

        room.host->buffered_bytes = room_media_buffer_high_water_bytes - 1;
        room.pumpUntilIdle();
        QCOMPARE(room.completed.size(), std::size_t{1});
    }

    void senderRefusesBadOffersAndDropsTransfersOfAClosedLink() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);
        QVERIFY(!room.sender.offer(*room.host, QDir(config.path()).filePath("missing.wav"), sha, 1));
        QVERIFY(!room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha, 1,
                                   static_cast<std::uint64_t>(original.size()) + 1));
        QVERIFY(room.host->sent_control.empty());

        QVERIFY(room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha, 1));
        QVERIFY(!room.sender.offer(*room.host, fixture("wav-pcm-mono.wav"), sha, 1)); // id queued
        room.host->closed = true;
        room.sender.pump(*room.host);
        QVERIFY(room.sender.idle());
    }
};

QTEST_GUILESS_MAIN(RoomFileTransferTests)
#include "RoomFileTransferTests.moc"
