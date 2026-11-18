#include "adapters/QtRoomFileInbox.hpp"
#include "adapters/RoomFileTransfer.hpp"

#include "../support/FakeRoomTransport.hpp"
#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
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
using music_player::m0::testing::FakeRoomGuestConnector;
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
// ListeningRoomGuest over the real QtRoomFileInbox; the test plays the host on the other end of the link
// and, like ListeningRoomHost, answers each request with a new offer streamed from the request's offset.
struct GuestOverRealInbox {
    explicit GuestOverRealInbox(const QString& config) : inbox(config) {
        guest = std::make_unique<ListeningRoomGuest>(ListeningRoomGuestDependencies{
            .connector = connector,
            .profile = ListenerProfile{"Ana", AvatarChoice{AvatarKind::Bundled, 3, {}}},
            .stored_custom_avatar_sha256 = [] { return std::optional<std::string>{}; },
            .capture_session = [] { return SessionSnapshot{}; },
            .restore_session = [](const SessionSnapshot&) {},
            .schedule = [](std::int64_t, std::function<void()>) {},
            .files = &inbox,
        });
        RoomInviteLink invite{};
        invite.room_id[0] = 7;
        invite.host_fingerprint[0] = 0xab;
        guest->join(invite);
        auto links = FakeRoomLink::pair(host_hex, "fedc0304" + std::string(56, '0'));
        host = links.first;
        host->onControl = [this](const RoomControlMessage& message) {
            if (const auto* request = std::get_if<RoomFileRequest>(&message))
                requests.push_back(*request);
        };
        connector.complete(links.second);
        send(RoomJoinResult{JoinDecision::Admitted});
    }
    void send(const RoomControlMessage& message) const { QVERIFY(host->sendControl(message)); }
    // The room queue: the file as item item_id (current), or empty.
    void queue(const std::uint64_t version, const std::string& sha, const std::uint64_t size,
               const std::optional<std::uint64_t> item_id) const {
        RoomStateMessage message;
        message.snapshot.version = version;
        message.snapshot.members = {RoomMember{host_hex, "Joao", AvatarChoice{}, MemberState::Admitted}};
        if (item_id) {
            RoomItem item;
            item.id = *item_id;
            item.origin = RoomItemOrigin::HostTrack;
            item.owner_fingerprint = host_hex;
            item.content_sha256 = sha;
            item.size_bytes = size;
            item.added_by = "Joao";
            message.snapshot.queue.push_back(item);
            message.snapshot.playback.current_item = *item_id;
        }
        send(message);
    }
    // Offers the file under a new transfer id and streams it from offset up to (not including) until.
    void serve(const QByteArray& bytes, const std::string& sha, const std::uint64_t offset, const std::uint64_t until) {
        const auto transfer_id = next_transfer_id++;
        send(RoomFileOffer{sha, static_cast<std::uint64_t>(bytes.size()), transfer_id});
        for (auto at = offset; at < until; at += room_file_chunk_bytes)
            QVERIFY(host->sendMedia(chunkOf(bytes, transfer_id, at)));
    }
    bool waitForRequests(const std::size_t count) {
        return QTest::qWaitFor([&] { return requests.size() >= count; }, 5000);
    }

    const std::string host_hex = "abcd0102" + std::string(56, '0');
    FakeRoomGuestConnector connector;
    QtRoomFileInbox inbox;
    std::unique_ptr<ListeningRoomGuest> guest;
    std::shared_ptr<FakeRoomLink> host;
    std::vector<RoomFileRequest> requests;
    std::uint32_t next_transfer_id{30};
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
        const auto wav = readAll(fixture("wav-pcm-mono.wav"));
        const auto original = wav + wav + wav;
        const auto sha = sha256(original);

        QVERIFY(room.receiver.accept({sha, static_cast<std::uint64_t>(original.size()), 4}));
        QVERIFY(room.receiver.consume(chunkOf(original, 4, 0)));
        QVERIFY(!room.receiver.consume(chunkOf(original, 4, 2 * room_file_chunk_bytes)));
        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::OutOfOrder));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
        // The transfer is over: a later chunk is just ignored.
        QVERIFY(!room.receiver.consume(chunkOf(original, 4, room_file_chunk_bytes)));
        QCOMPARE(room.failed.size(), std::size_t{1});
    }

    // A first chunk beyond the resume point (the host answering a request made before the .part was cut
    // back) is a Gap: the transfer ends but the .part stays, and asking again resumes from it.
    void aFirstChunkBeyondTheResumePointIsAGapThatKeepsThePart() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto wav = readAll(fixture("wav-pcm-mono.wav"));
        const auto original = wav + wav + wav;
        const auto sha = sha256(original);
        const auto size = static_cast<std::uint64_t>(original.size());

        QVERIFY(room.receiver.accept({sha, size, 1}));
        QVERIFY(room.receiver.consume(chunkOf(original, 1, 0)));
        QVERIFY(room.receiver.consume(chunkOf(original, 1, room_file_chunk_bytes)));
        room.receiver.abandon(1);
        QVERIFY(room.receiver.accept({sha, size, 2})); // resumes at 2 chunks
        QVERIFY(!room.receiver.consume(chunkOf(original, 2, 3 * room_file_chunk_bytes)));
        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::Gap));
        QCOMPARE(QFileInfo(*room.cache.partialPathFor(sha)).size(), qint64{2 * room_file_chunk_bytes});
        QCOMPARE(room.receiver.resumeRequest(sha),
                 (std::optional<RoomFileRequest>{RoomFileRequest{sha, 2 * room_file_chunk_bytes}}));

        QVERIFY(room.receiver.accept({sha, size, 3}));
        for (auto at = std::uint64_t{2 * room_file_chunk_bytes}; at < size; at += room_file_chunk_bytes)
            QVERIFY(room.receiver.consume(chunkOf(original, 3, at)));
        QCOMPARE(room.completed.size(), std::size_t{1});
        QCOMPARE(readAll(room.completed.front().second), original);
        QVERIFY(room.cache.reserve(room_cache_quota_bytes - size)); // nothing else reserved
    }

    // A replacement transfer (the host answering an older request) may start below where the .part
    // stands: on its first chunk the receiver goes back to the sender's offset, keeps the prefix and
    // completes, with nothing left reserved.
    void aTransferStartingBelowThePartGoesBackToTheSendersOffset() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto wav = readAll(fixture("wav-pcm-mono.wav"));
        const auto original = wav + wav + wav;
        const auto sha = sha256(original);
        const auto size = static_cast<std::uint64_t>(original.size());

        QVERIFY(room.receiver.accept({sha, size, 1}));
        QVERIFY(room.receiver.consume(chunkOf(original, 1, 0)));
        QVERIFY(room.receiver.consume(chunkOf(original, 1, room_file_chunk_bytes)));
        room.receiver.abandon(1);
        QVERIFY(room.receiver.accept({sha, size, 2})); // resumes at 2 chunks
        for (std::uint64_t at = 0; at < size; at += room_file_chunk_bytes)
            QVERIFY(room.receiver.consume(chunkOf(original, 2, at)));
        QVERIFY(room.failed.empty());
        QCOMPARE(room.completed.size(), std::size_t{1});
        QCOMPARE(readAll(room.completed.front().second), original);
        QVERIFY(room.cache.reserve(room_cache_quota_bytes - size)); // nothing else reserved
    }

    // Only the first chunk may go back: once a transfer has started, any other offset is OutOfOrder.
    void aStartedTransferGoingBackFailsOutOfOrder() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        Room room(config.path());
        const auto original = readAll(fixture("wav-pcm-mono.wav"));
        const auto sha = sha256(original);

        QVERIFY(room.receiver.accept({sha, static_cast<std::uint64_t>(original.size()), 4}));
        QVERIFY(room.receiver.consume(chunkOf(original, 4, 0)));
        QVERIFY(!room.receiver.consume(chunkOf(original, 4, 0)));
        QCOMPARE(room.failed.size(), std::size_t{1});
        QCOMPARE(room.failed.front(), std::make_pair(sha, RoomTransferError::OutOfOrder));
        QVERIFY(!QFileInfo::exists(*room.cache.partialPathFor(sha)));
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

    // S8.2 review: the host's window drops the file and brings it back before it serves the first
    // request, so the guest asks twice. The first offer is accepted and writes some chunks; the second
    // replaces it but streams from the offset of a request made before those chunks. Whatever happens,
    // the file must complete: the receiver may only start where the host streams from.
    void aReplacedTransferStillCompletesFromWhereTheCacheStands_data() {
        QTest::addColumn<int>("flaps");
        QTest::newRow("dropped and back once") << 1;
        QTest::newRow("dropped and back twice") << 2;
    }
    void aReplacedTransferStillCompletesFromWhereTheCacheStands() {
        QFETCH(int, flaps);
        QTemporaryDir config;
        QVERIFY(config.isValid());
        QByteArray bytes(static_cast<qsizetype>(3 * room_file_chunk_bytes + 1234), Qt::Uninitialized);
        for (qsizetype index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<char>((index * 31 + 7) % 251);
        const auto sha = sha256(bytes);
        const auto size = static_cast<std::uint64_t>(bytes.size());
        {
            GuestOverRealInbox room(config.path());
            QCOMPARE(room.guest->state(), GuestRoomState::Joined);
            std::uint64_t version = 1;
            room.queue(version++, sha, size, 11);
            QVERIFY(room.waitForRequests(1));
            for (int flap = 0; flap < flaps; ++flap) {
                room.queue(version++, sha, size, std::nullopt);
                room.queue(version++, sha, size, 12 + static_cast<std::uint64_t>(flap));
                QVERIFY(room.waitForRequests(static_cast<std::size_t>(flap) + 2));
            }
            const auto asked = room.requests.size();
            QCOMPARE(asked, static_cast<std::size_t>(flaps) + 1);
            for (const auto& request : room.requests)
                QCOMPARE(request.offset, std::uint64_t{0});

            // Each earlier request's transfer writes some chunks before the host replaces it with the
            // answer to the next request (cancelling it), streamed from that request's offset.
            for (std::size_t index = 0; index + 1 < asked; ++index) {
                const auto chunks = index == 0 ? 2 : 1;
                room.serve(bytes, sha, room.requests[index].offset,
                           room.requests[index].offset + chunks * room_file_chunk_bytes);
                room.inbox.waitForWorker();
                QCoreApplication::processEvents();
            }
            room.serve(bytes, sha, room.requests.back().offset, size);
            // Like the host, every later request gets a new offer streamed from its offset.
            std::size_t answered = asked;
            QDeadlineTimer deadline(5000);
            while (!room.guest->localFile(sha) && !deadline.hasExpired()) {
                room.inbox.waitForWorker();
                QCoreApplication::processEvents();
                if (room.requests.size() > answered) {
                    answered = room.requests.size();
                    room.serve(bytes, sha, room.requests.back().offset, size);
                }
            }
            const auto file = room.guest->localFile(sha);
            QVERIFY2(file.has_value(), "the replaced transfer never completed");
            QCOMPARE(readAll(QString::fromStdU16String(file->u16string())), bytes);
        }
    }

    // S8.2 review, round 3: two quick drop-and-return cycles make the host answer a request made before
    // the .part was cut back. The transfer answering the newest request then starts beyond the .part
    // (a Gap); the guest asks again from what is kept and the file completes.
    void aTransferStartingBeyondTheCutBackPartStillCompletes() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        QByteArray bytes(static_cast<qsizetype>(5 * room_file_chunk_bytes + 777), Qt::Uninitialized);
        for (qsizetype index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<char>((index * 17 + 3) % 253);
        const auto sha = sha256(bytes);
        const auto size = static_cast<std::uint64_t>(bytes.size());
        const auto pump = [](GuestOverRealInbox& room) {
            room.inbox.waitForWorker();
            QCoreApplication::processEvents();
        };
        {
            GuestOverRealInbox room(config.path());
            QCOMPARE(room.guest->state(), GuestRoomState::Joined);
            // 1. R0 at 0.
            room.queue(1, sha, size, 11);
            QVERIFY(room.waitForRequests(1));
            // 2. Out and back before R0 is served: R_Y at 0.
            room.queue(2, sha, size, std::nullopt);
            room.queue(3, sha, size, 12);
            QVERIFY(room.waitForRequests(2));
            // 3. The host serves R0: three chunks are written.
            room.serve(bytes, sha, room.requests[0].offset, room.requests[0].offset + 3 * room_file_chunk_bytes);
            pump(room);
            // 4. Out (the guest abandons T0, the .part stays at three chunks) and back: R_X at three chunks.
            room.queue(4, sha, size, std::nullopt);
            room.queue(5, sha, size, 13);
            QVERIFY(room.waitForRequests(3));
            QCOMPARE(room.requests[2].offset, std::uint64_t{3 * room_file_chunk_bytes});
            // 5. The host serves R_Y from 0: the guest goes back to 0 and one chunk is written.
            room.serve(bytes, sha, room.requests[1].offset, room.requests[1].offset + room_file_chunk_bytes);
            pump(room);
            // 6. The host replaces T_Y with the answer to R_X, from three chunks: beyond the .part.
            room.serve(bytes, sha, room.requests[2].offset, size);
            // 7. Like the host, every later request gets a new offer streamed from its offset.
            std::size_t answered = 3;
            QDeadlineTimer deadline(5000);
            while (!room.guest->localFile(sha) && !deadline.hasExpired()) {
                pump(room);
                if (room.requests.size() > answered) {
                    answered = room.requests.size();
                    room.serve(bytes, sha, room.requests.back().offset, size);
                }
            }
            const auto file = room.guest->localFile(sha);
            QVERIFY2(file.has_value(), "the transfer that started beyond the .part never completed");
            QCOMPARE(readAll(QString::fromStdU16String(file->u16string())), bytes);
            // It resumed from what the .part kept (one chunk), not from scratch.
            QCOMPARE(room.requests.back().offset, std::uint64_t{room_file_chunk_bytes});
        }
    }
};

QTEST_GUILESS_MAIN(RoomFileTransferTests)
#include "RoomFileTransferTests.moc"
