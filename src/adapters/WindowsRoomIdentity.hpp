#pragma once

#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace music_player::m0 {

// The device's DTLS identity for the listening room (ADR-0022): a self-signed ECDSA P-256
// certificate whose SHA-256 fingerprint is the listener ID. Three files in config_directory, each
// written with QSaveFile:
//   room-identity-cert.pem  certificate (-----BEGIN CERTIFICATE-----);
//   room-identity-key.pem   private key as encrypted PKCS#8 (-----BEGIN ENCRYPTED PRIVATE KEY-----):
//                           PBES2, PBKDF2-HMAC-SHA256 (16-byte salt, 10000 iterations), AES-256-CBC.
//                           Mbed TLS 3.6.8 (pkcs5.c + oid.c) parses exactly this PBES2 variant.
//   room-identity-pass.bin  the 32 random bytes of the key passphrase, protected with
//                           CryptProtectData (current user, no UI, purpose entropy).
// The private key never reaches the disk in clear. libdatachannel 0.24.6 reads the identity through
// rtc::Configuration::certificatePemFile / keyPemFile (file paths) and keyPemPass.
struct RoomIdentity final {
    std::string certificate_pem;
    std::string key_pem;                        // the encrypted PKCS#8 PEM, as stored
    std::array<std::uint8_t, 32> fingerprint{}; // SHA-256 of the certificate DER, as a DTLS peer sees it
    QString certificate_file;                   // absolute paths of the files above
    QString key_file;
    // keyPemPass: base64url (no padding, 43 ASCII chars) of the 32 passphrase bytes. A secret:
    // keep it in memory only and never log it.
    std::string key_passphrase;
};

// Loads the identity, or creates it when room-identity-cert.pem is absent (a key or passphrase
// left without a certificate is an interrupted creation and is replaced). Any unreadable, corrupt
// or mismatched file (bad PEM, certificate not self-signed by the stored key, wrong passphrase,
// DPAPI failure) returns nullopt and leaves every file untouched; so does a certificate whose key
// or passphrase file is missing. Never logs secrets.
[[nodiscard]] std::optional<RoomIdentity> loadOrCreateRoomIdentity(const QString& config_directory);

} // namespace music_player::m0
