#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace music_player::m0 {

// HKDF-SHA256 (RFC 5869) with BCrypt HMAC-SHA256. An empty salt means HashLen zero bytes, as the
// RFC specifies. Returns an empty array when length is 0 or above 255 * 32. Exposed for the RFC
// test vectors.
[[nodiscard]] QByteArray hkdfSha256(QByteArrayView input_key, QByteArrayView salt, QByteArrayView info,
                                    qsizetype length);

// End-to-end cipher of the signaling envelopes (the "data" of S5's {to, data}). Both keys come
// from the invite secret with HKDF-SHA256 and an EMPTY salt:
//   signaling key = HKDF(secret, info "rc-music room signaling v1", 32 bytes), AES-256-GCM;
//   host token    = HKDF(secret, info "rc-music room host v1", 32 bytes).
// seal output: 12-byte random nonce | ciphertext | 16-byte tag, AAD = room_id. The relay sees only
// opaque bytes. The signaling key is wiped on destruction; nothing here is logged.
class WindowsRoomEnvelopeCipher final {
  public:
    static constexpr qsizetype nonce_size = 12;
    static constexpr qsizetype tag_size = 16;

    explicit WindowsRoomEnvelopeCipher(std::span<const std::uint8_t, 16> secret,
                                       std::span<const std::uint8_t, 16> room_id);
    ~WindowsRoomEnvelopeCipher();
    WindowsRoomEnvelopeCipher(const WindowsRoomEnvelopeCipher&) = default;
    WindowsRoomEnvelopeCipher& operator=(const WindowsRoomEnvelopeCipher&) = default;

    // Empty only if the system RNG or BCrypt fails.
    [[nodiscard]] QByteArray seal(QByteArrayView plain) const;
    // nullopt for input shorter than nonce + tag, a wrong secret or room_id, or any altered byte.
    [[nodiscard]] std::optional<QByteArray> open(QByteArrayView sealed) const;
    // The 32-byte host token as the 43-char unpadded base64url text that the S5 relay
    // (services/room-signaling) requires in X-Room-Host-Token.
    [[nodiscard]] QByteArray hostToken() const;

  private:
    std::array<std::uint8_t, 32> key_{};
    std::array<std::uint8_t, 16> room_id_{};
    QByteArray host_token_;
};

} // namespace music_player::m0
