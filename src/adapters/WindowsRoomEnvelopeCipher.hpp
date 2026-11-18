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

// End-to-end cipher of the signaling envelopes (the "data" of S5's {to, data}). The key comes
// from the invite secret with HKDF-SHA256 and an EMPTY salt:
//   signaling key = HKDF(secret, info "rc-music room signaling v1", 32 bytes), AES-256-GCM.
// seal output: 12-byte random nonce | ciphertext | 16-byte tag, AAD = room_id. The relay sees only
// opaque bytes. Fails closed: if the key derivation fails, isValid() is false, seal() returns an
// empty array and open() returns nullopt (never a fallback key). The relay's host token is NOT
// derived here: every guest holds the invite secret (see roomHostToken in WindowsRoomIdentity.hpp).
// The signaling key is wiped on destruction; nothing here is logged.
class WindowsRoomEnvelopeCipher final {
  public:
    static constexpr qsizetype nonce_size = 12;
    static constexpr qsizetype tag_size = 16;

    using KeyDerivation = QByteArray (*)(QByteArrayView input_key, QByteArrayView salt, QByteArrayView info,
                                         qsizetype length);

    explicit WindowsRoomEnvelopeCipher(std::span<const std::uint8_t, 16> secret,
                                       std::span<const std::uint8_t, 16> room_id);
    // Test seam: the same cipher with another HKDF (production passes hkdfSha256).
    WindowsRoomEnvelopeCipher(std::span<const std::uint8_t, 16> secret, std::span<const std::uint8_t, 16> room_id,
                              KeyDerivation derive);
    ~WindowsRoomEnvelopeCipher();
    WindowsRoomEnvelopeCipher(const WindowsRoomEnvelopeCipher&) = default;
    WindowsRoomEnvelopeCipher& operator=(const WindowsRoomEnvelopeCipher&) = default;

    // false when the key derivation failed; the cipher then refuses to seal or open.
    [[nodiscard]] bool isValid() const;
    // Empty when the cipher is not valid or the system RNG or BCrypt fails.
    [[nodiscard]] QByteArray seal(QByteArrayView plain) const;
    // nullopt for an invalid cipher, input shorter than nonce + tag, a wrong secret or room_id, or
    // any altered byte.
    [[nodiscard]] std::optional<QByteArray> open(QByteArrayView sealed) const;

  private:
    std::array<std::uint8_t, 32> key_{};
    std::array<std::uint8_t, 16> room_id_{};
    bool valid_{};
};

} // namespace music_player::m0
