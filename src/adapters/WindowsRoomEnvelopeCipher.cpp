#include "adapters/WindowsRoomEnvelopeCipher.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <limits>

namespace music_player::m0 {
namespace {
constexpr qsizetype hash_size = 32;
constexpr qsizetype key_size = 32;
constexpr char signaling_info[] = "rc-music room signaling v1";

PUCHAR bytes(const void* data) { return static_cast<PUCHAR>(const_cast<void*>(data)); }
// BCrypt wants a valid pointer even for zero-length input.
PUCHAR bytes(QByteArrayView view) {
    static std::uint8_t none = 0;
    return view.empty() ? &none : bytes(static_cast<const void*>(view.data()));
}

bool hmacSha256(QByteArrayView key, QByteArrayView message, std::uint8_t* out) {
    if (key.size() > std::numeric_limits<ULONG>::max() || message.size() > std::numeric_limits<ULONG>::max())
        return false;
    return BCRYPT_SUCCESS(BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, bytes(key), static_cast<ULONG>(key.size()),
                                     bytes(message), static_cast<ULONG>(message.size()), out,
                                     static_cast<ULONG>(hash_size)));
}

// GCM with a fresh BCrypt key object per call: the class stays copyable and thread-agnostic.
class GcmKey final {
  public:
    explicit GcmKey(const std::array<std::uint8_t, 32>& key) {
        if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(BCRYPT_AES_GCM_ALG_HANDLE, &handle_, nullptr, 0,
                                                       bytes(key.data()), static_cast<ULONG>(key.size()), 0)))
            handle_ = nullptr;
    }
    ~GcmKey() {
        if (handle_)
            BCryptDestroyKey(handle_);
    }
    GcmKey(const GcmKey&) = delete;
    GcmKey& operator=(const GcmKey&) = delete;
    [[nodiscard]] BCRYPT_KEY_HANDLE get() const { return handle_; }

  private:
    BCRYPT_KEY_HANDLE handle_{};
};

BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO modeInfo(const void* nonce, const std::array<std::uint8_t, 16>& aad, void* tag) {
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = bytes(nonce);
    info.cbNonce = static_cast<ULONG>(WindowsRoomEnvelopeCipher::nonce_size);
    info.pbAuthData = bytes(aad.data());
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = static_cast<PUCHAR>(tag);
    info.cbTag = static_cast<ULONG>(WindowsRoomEnvelopeCipher::tag_size);
    return info;
}
} // namespace

QByteArray hkdfSha256(QByteArrayView input_key, QByteArrayView salt, QByteArrayView info, const qsizetype length) {
    if (length <= 0 || length > 255 * hash_size)
        return {};
    std::array<std::uint8_t, hash_size> prk{};
    const QByteArray zero_salt(hash_size, '\0');
    if (!hmacSha256(salt.empty() ? QByteArrayView(zero_salt) : salt, input_key, prk.data()))
        return {};
    QByteArray okm;
    QByteArray block;
    std::array<std::uint8_t, hash_size> t{};
    for (int counter = 1; okm.size() < length; ++counter) {
        block = block + info.toByteArray() + static_cast<char>(counter);
        if (!hmacSha256(QByteArrayView(reinterpret_cast<const char*>(prk.data()), hash_size), block, t.data())) {
            SecureZeroMemory(prk.data(), prk.size());
            return {};
        }
        block = QByteArray(reinterpret_cast<const char*>(t.data()), hash_size);
        okm += block;
    }
    SecureZeroMemory(prk.data(), prk.size());
    SecureZeroMemory(t.data(), t.size());
    SecureZeroMemory(block.data(), block.size());
    okm.truncate(length);
    return okm;
}

WindowsRoomEnvelopeCipher::WindowsRoomEnvelopeCipher(std::span<const std::uint8_t, 16> secret,
                                                     std::span<const std::uint8_t, 16> room_id)
    : WindowsRoomEnvelopeCipher(secret, room_id, &hkdfSha256) {}

WindowsRoomEnvelopeCipher::WindowsRoomEnvelopeCipher(std::span<const std::uint8_t, 16> secret,
                                                     std::span<const std::uint8_t, 16> room_id,
                                                     const KeyDerivation derive) {
    const QByteArrayView ikm(reinterpret_cast<const char*>(secret.data()), static_cast<qsizetype>(secret.size()));
    auto key = derive(ikm, {}, signaling_info, key_size);
    // Fail closed: without a derived key there is no cipher, never the all-zero key_.
    valid_ = key.size() == key_size;
    if (valid_)
        std::copy(key.cbegin(), key.cend(), reinterpret_cast<char*>(key_.data()));
    if (!key.isEmpty())
        SecureZeroMemory(key.data(), static_cast<SIZE_T>(key.size()));
    std::copy(room_id.begin(), room_id.end(), room_id_.begin());
}

bool WindowsRoomEnvelopeCipher::isValid() const { return valid_; }

WindowsRoomEnvelopeCipher::~WindowsRoomEnvelopeCipher() { SecureZeroMemory(key_.data(), key_.size()); }

QByteArray WindowsRoomEnvelopeCipher::seal(QByteArrayView plain) const {
    if (!valid_ || plain.size() > std::numeric_limits<ULONG>::max() - nonce_size - tag_size)
        return {};
    const GcmKey key(key_);
    if (!key.get())
        return {};
    QByteArray sealed(nonce_size + plain.size() + tag_size, Qt::Uninitialized);
    auto* nonce = reinterpret_cast<std::uint8_t*>(sealed.data());
    auto* body = nonce + nonce_size;
    auto* tag = body + plain.size();
    if (!BCRYPT_SUCCESS(
            BCryptGenRandom(nullptr, nonce, static_cast<ULONG>(nonce_size), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        return {};
    auto info = modeInfo(nonce, room_id_, tag);
    ULONG written = 0;
    if (!BCRYPT_SUCCESS(BCryptEncrypt(key.get(), bytes(plain), static_cast<ULONG>(plain.size()), &info, nullptr, 0,
                                      body, static_cast<ULONG>(plain.size()), &written, 0)) ||
        written != static_cast<ULONG>(plain.size()))
        return {};
    return sealed;
}

std::optional<QByteArray> WindowsRoomEnvelopeCipher::open(QByteArrayView sealed) const {
    if (!valid_ || sealed.size() < nonce_size + tag_size || sealed.size() > std::numeric_limits<ULONG>::max())
        return std::nullopt;
    const GcmKey key(key_);
    if (!key.get())
        return std::nullopt;
    const auto body_size = sealed.size() - nonce_size - tag_size;
    const auto* nonce = sealed.data();
    const auto* body = nonce + nonce_size;
    // BCrypt wants a writable tag buffer even when decrypting.
    std::array<std::uint8_t, tag_size> tag{};
    std::copy(body + body_size, body + body_size + tag_size, reinterpret_cast<char*>(tag.data()));
    auto info = modeInfo(nonce, room_id_, tag.data());
    QByteArray plain(body_size, Qt::Uninitialized);
    ULONG written = 0;
    if (!BCRYPT_SUCCESS(BCryptDecrypt(key.get(), bytes(QByteArrayView(body, body_size)), static_cast<ULONG>(body_size),
                                      &info, nullptr, 0, reinterpret_cast<PUCHAR>(plain.data()),
                                      static_cast<ULONG>(body_size), &written, 0)) ||
        written != static_cast<ULONG>(body_size))
        return std::nullopt;
    return plain;
}

} // namespace music_player::m0
