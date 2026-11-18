#pragma once

// In-memory implementation of m0/ports/RoomTransport.hpp for application-layer tests.
// Links come in connected pairs and deliver synchronously; the endpoint and the connector only
// record their calls and let the test decide when a link arrives or a failure happens. Like the
// real adapter, the connector calls exactly one of its callbacks once and nothing after cancel(),
// and the endpoint delivers nothing after closeAll() or its own failure.

#include "m0/ports/RoomTransport.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace music_player::m0::testing {

class FakeRoomLink final : public RoomLink {
  public:
    explicit FakeRoomLink(std::string peer_fingerprint) : peer_fingerprint_(std::move(peer_fingerprint)) {}

    // a.peerFingerprint() == b_fingerprint and the other way round.
    static std::pair<std::shared_ptr<FakeRoomLink>, std::shared_ptr<FakeRoomLink>> pair(std::string a_fingerprint,
                                                                                        std::string b_fingerprint) {
        auto a = std::make_shared<FakeRoomLink>(std::move(b_fingerprint));
        auto b = std::make_shared<FakeRoomLink>(std::move(a_fingerprint));
        a->peer_ = b;
        b->peer_ = a;
        return {a, b};
    }

    [[nodiscard]] std::string peerFingerprint() const override { return peer_fingerprint_; }
    bool sendControl(const RoomControlMessage& message) override {
        if (closed || (accepts_control && !accepts_control(message)))
            return false;
        sent_control.push_back(message);
        if (const auto peer = peer_.lock(); peer && !peer->closed && peer->onControl)
            peer->onControl(message);
        return true;
    }
    bool sendMedia(const RoomFileChunk& chunk) override {
        if (closed)
            return false;
        sent_media.push_back(chunk);
        if (const auto peer = peer_.lock(); peer && !peer->closed && peer->onMedia)
            peer->onMedia(chunk);
        return true;
    }
    [[nodiscard]] std::size_t mediaBufferedBytes() const override { return buffered_bytes; }
    void close() override {
        if (closed)
            return;
        closed = true;
        if (const auto peer = peer_.lock())
            peer->drop(RoomLinkFailure::Closed);
    }
    // The network side ends the link: this side sees onClosed(failure).
    void drop(const RoomLinkFailure failure) {
        if (closed)
            return;
        closed = true;
        if (onClosed)
            onClosed(failure);
    }

    std::vector<RoomControlMessage> sent_control;
    // When set and false, sendControl drops the message like the adapter drops what the codec rejects.
    std::function<bool(const RoomControlMessage&)> accepts_control;
    std::vector<RoomFileChunk> sent_media;
    std::size_t buffered_bytes{};
    bool closed{};

  private:
    std::string peer_fingerprint_;
    std::weak_ptr<FakeRoomLink> peer_;
};

class FakeRoomHostEndpoint final : public RoomHostEndpoint {
  public:
    void open(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_incoming,
              std::function<void(RoomLinkFailure)> on_failure) override {
        opened = invite;
        ++open_count;
        on_incoming_ = std::move(on_incoming);
        on_failure_ = std::move(on_failure);
    }
    void closeAll() override {
        closed_all = true;
        disarm();
    }
    void admit(std::shared_ptr<RoomLink> link) {
        if (const auto deliver = on_incoming_)
            deliver(std::move(link));
    }
    void fail(const RoomLinkFailure failure) {
        const auto deliver = on_failure_;
        disarm();
        if (deliver)
            deliver(failure);
    }

    std::optional<RoomInviteLink> opened;
    int open_count{};
    bool closed_all{};

  private:
    void disarm() {
        on_incoming_ = nullptr;
        on_failure_ = nullptr;
    }
    std::function<void(std::shared_ptr<RoomLink>)> on_incoming_;
    std::function<void(RoomLinkFailure)> on_failure_;
};

class FakeRoomGuestConnector final : public RoomGuestConnector {
  public:
    void connect(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_connected,
                 std::function<void(RoomLinkFailure)> on_failure) override {
        requested = invite;
        cancelled = false;
        on_connected_ = std::move(on_connected);
        on_failure_ = std::move(on_failure);
    }
    void cancel() override {
        cancelled = true;
        disarm();
    }
    void complete(std::shared_ptr<RoomLink> link) {
        const auto deliver = on_connected_;
        disarm();
        if (deliver)
            deliver(std::move(link));
    }
    void fail(const RoomLinkFailure failure) {
        const auto deliver = on_failure_;
        disarm();
        if (deliver)
            deliver(failure);
    }

    std::optional<RoomInviteLink> requested;
    bool cancelled{};

  private:
    void disarm() {
        on_connected_ = nullptr;
        on_failure_ = nullptr;
    }
    std::function<void(std::shared_ptr<RoomLink>)> on_connected_;
    std::function<void(RoomLinkFailure)> on_failure_;
};

} // namespace music_player::m0::testing
