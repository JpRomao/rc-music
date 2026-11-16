#pragma once

#include <QByteArray>
#include <QCryptographicHash>

#include <windows.h>

#include <bcrypt.h>

namespace music_player::m0::test {
// A fresh P-256 key per test run: no private key is ever stored in the repository.
class SigningKey final {
  public:
    SigningKey() {
        BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
        BCryptGenerateKeyPair(algorithm_, &key_, 256, 0);
        BCryptFinalizeKeyPair(key_, 0);
        ULONG size = 0;
        BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &size, 0);
        QByteArray blob(static_cast<qsizetype>(size), '\0');
        BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(blob.data()), size, &size, 0);
        public_key_ = blob.mid(sizeof(BCRYPT_ECCKEY_BLOB)); // X||Y
    }
    ~SigningKey() {
        if (key_)
            BCryptDestroyKey(key_);
        if (algorithm_)
            BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;
    [[nodiscard]] QByteArray publicKey() const { return public_key_; }
    [[nodiscard]] QByteArray sign(const QByteArray& message) const {
        auto hash = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
        QByteArray signature(64, '\0');
        ULONG size = 0;
        BCryptSignHash(key_, nullptr, reinterpret_cast<PUCHAR>(hash.data()), static_cast<ULONG>(hash.size()),
                       reinterpret_cast<PUCHAR>(signature.data()), 64, &size, 0);
        return signature;
    }

  private:
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_KEY_HANDLE key_{};
    QByteArray public_key_{};
};
} // namespace music_player::m0::test
