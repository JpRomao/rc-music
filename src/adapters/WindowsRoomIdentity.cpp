#include "adapters/WindowsRoomIdentity.hpp"

#include "adapters/WindowsRoomEnvelopeCipher.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QScopeGuard>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#include <ncrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr auto certificate_name = "room-identity-cert.pem";
constexpr auto key_name = "room-identity-key.pem";
constexpr auto passphrase_name = "room-identity-pass.bin";
constexpr auto lock_name = "room-identity.lock";
constexpr qint64 maximum_file_size = 64 * 1024;
constexpr qsizetype passphrase_size = 32;
constexpr qsizetype coordinate_size = 32; // P-256
constexpr qsizetype salt_size = 16;
constexpr qsizetype iv_size = 16;
constexpr quint32 pbkdf2_iterations = 10000;
// Mbed TLS refuses more (MBEDTLS_PK_DECRYPT_MAX_ITERATIONS in 3.6.8).
constexpr quint32 maximum_iterations = 10000000;
constexpr char dpapi_entropy[] = "rc-music room identity v1";
constexpr char host_token_info[] = "rc-music room host v1";

// DER contents of the object identifiers in use.
const QByteArray oid_ec_public_key = QByteArray::fromHex("2a8648ce3d0201");
const QByteArray oid_prime256v1 = QByteArray::fromHex("2a8648ce3d030107");
const QByteArray oid_pbes2 = QByteArray::fromHex("2a864886f70d01050d");
const QByteArray oid_pbkdf2 = QByteArray::fromHex("2a864886f70d01050c");
const QByteArray oid_hmac_sha256 = QByteArray::fromHex("2a864886f70d0209");
const QByteArray oid_aes256_cbc = QByteArray::fromHex("60864801650304012a");

enum Tag : quint8 {
    Integer = 0x02,
    BitString = 0x03,
    OctetString = 0x04,
    Null = 0x05,
    Oid = 0x06,
    Sequence = 0x30,
    Explicit0 = 0xa0,
    Explicit1 = 0xa1,
};

void wipe(QByteArray& bytes) {
    if (!bytes.isEmpty())
        SecureZeroMemory(bytes.data(), static_cast<SIZE_T>(bytes.size()));
    bytes.clear();
}
PUCHAR raw(const QByteArray& bytes) { return reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.constData())); }
PUCHAR raw(QByteArray& bytes) { return reinterpret_cast<PUCHAR>(bytes.data()); }

// --- DER writing -------------------------------------------------------------------------------
QByteArray tlv(const quint8 tag, const QByteArray& content) {
    QByteArray out(1, static_cast<char>(tag));
    const auto size = static_cast<quint32>(content.size());
    if (size < 0x80) {
        out += static_cast<char>(size);
    } else if (size <= 0xff) {
        out += '\x81';
        out += static_cast<char>(size);
    } else if (size <= 0xffff) {
        out += '\x82';
        out += static_cast<char>(size >> 8);
        out += static_cast<char>(size & 0xff);
    } else {
        out += '\x83';
        out += static_cast<char>(size >> 16);
        out += static_cast<char>((size >> 8) & 0xff);
        out += static_cast<char>(size & 0xff);
    }
    return out + content;
}
QByteArray integer(const quint32 value) {
    QByteArray content;
    for (int shift = 24; shift >= 0; shift -= 8)
        if (const auto byte = static_cast<char>((value >> shift) & 0xff); byte != 0 || !content.isEmpty() || shift == 0)
            content += byte;
    if (static_cast<quint8>(content[0]) & 0x80)
        content.prepend('\0');
    return tlv(Integer, content);
}

// --- strict DER reading --------------------------------------------------------------------------
class DerReader final {
  public:
    explicit DerReader(QByteArrayView bytes) : rest_(bytes) {}
    [[nodiscard]] std::optional<QByteArrayView> take(const quint8 tag) {
        if (rest_.size() < 2 || static_cast<quint8>(rest_[0]) != tag)
            return std::nullopt;
        qsizetype at = 1;
        qsizetype size = static_cast<quint8>(rest_[at++]);
        if (size & 0x80) {
            const auto count = size & 0x7f;
            if (count < 1 || count > 3 || rest_.size() - at < count || rest_[at] == '\0')
                return std::nullopt;
            size = 0;
            for (qsizetype i = 0; i < count; ++i)
                size = (size << 8) | static_cast<quint8>(rest_[at++]);
            if (size < 0x80)
                return std::nullopt; // not the minimal length form
        }
        if (rest_.size() - at < size)
            return std::nullopt;
        const auto content = rest_.sliced(at, size);
        rest_ = rest_.sliced(at + size);
        return content;
    }
    [[nodiscard]] bool expect(const quint8 tag, const QByteArray& content) {
        const auto value = take(tag);
        return value && *value == content;
    }
    [[nodiscard]] bool done() const { return rest_.isEmpty(); }
    [[nodiscard]] bool startsWith(const quint8 tag) const {
        return !rest_.isEmpty() && static_cast<quint8>(rest_[0]) == tag;
    }

  private:
    QByteArrayView rest_;
};
std::optional<quint32> positiveInteger(const QByteArrayView content) {
    if (content.isEmpty() || content.size() > 4 || (static_cast<quint8>(content[0]) & 0x80) ||
        (content.size() > 1 && content[0] == '\0' && !(static_cast<quint8>(content[1]) & 0x80)))
        return std::nullopt;
    quint32 value = 0;
    for (const char c : content)
        value = (value << 8) | static_cast<quint8>(c);
    return value == 0 ? std::nullopt : std::optional<quint32>(value);
}

// --- PEM -------------------------------------------------------------------------------------------
QByteArray pem(const QByteArray& der, const QByteArray& label) {
    QByteArray out = "-----BEGIN " + label + "-----\n";
    const auto base64 = der.toBase64();
    for (qsizetype at = 0; at < base64.size(); at += 64)
        out += base64.mid(at, 64) + '\n';
    return out + "-----END " + label + "-----\n";
}
// Accepts only the exact text pem() writes.
std::optional<QByteArray> fromPem(const QByteArray& text, const QByteArray& label) {
    const QByteArray header = "-----BEGIN " + label + "-----\n";
    const QByteArray footer = "-----END " + label + "-----\n";
    if (!text.startsWith(header) || !text.endsWith(footer) || text.size() <= header.size() + footer.size())
        return std::nullopt;
    auto body = text.mid(header.size(), text.size() - header.size() - footer.size());
    body.replace('\n', QByteArray{});
    auto decoded = QByteArray::fromBase64Encoding(body, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || pem(decoded.decoded, label) != text)
        return std::nullopt;
    return std::move(decoded.decoded);
}

// --- CNG helpers ------------------------------------------------------------------------------------
std::optional<QByteArray> randomBytes(const qsizetype size) {
    QByteArray bytes(size, Qt::Uninitialized);
    if (!BCRYPT_SUCCESS(
            BCryptGenRandom(nullptr, raw(bytes), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        return std::nullopt;
    return bytes;
}
std::optional<std::array<std::uint8_t, 32>> sha256(const QByteArray& bytes) {
    std::array<std::uint8_t, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, raw(bytes), static_cast<ULONG>(bytes.size()),
                                   digest.data(), 32)))
        return std::nullopt;
    return digest;
}
// PBKDF2-HMAC-SHA256 of the passphrase text (Mbed TLS uses the strlen() bytes of keyPemPass).
std::optional<QByteArray> pbkdf2(const QByteArray& passphrase_text, const QByteArray& salt, const quint32 iterations) {
    QByteArray key(32, Qt::Uninitialized);
    if (!BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(BCRYPT_HMAC_SHA256_ALG_HANDLE, raw(passphrase_text),
                                              static_cast<ULONG>(passphrase_text.size()), raw(salt),
                                              static_cast<ULONG>(salt.size()), iterations, raw(key), 32, 0))) {
        wipe(key);
        return std::nullopt;
    }
    return key;
}
// AES-256-CBC with PKCS#7 padding, as PBES2 specifies.
std::optional<QByteArray> aesCbc(const bool encrypt, QByteArray key, const QByteArray& iv, const QByteArray& input) {
    const auto cleanup = qScopeGuard([&] { wipe(key); });
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (input.isEmpty() || !BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(BCRYPT_AES_CBC_ALG_HANDLE, &handle, nullptr, 0,
                                                                      raw(key), static_cast<ULONG>(key.size()), 0)))
        return std::nullopt;
    const auto release = qScopeGuard([&] { BCryptDestroyKey(handle); });
    auto chain = iv; // BCrypt updates the IV buffer in place
    QByteArray output(input.size() + iv_size, Qt::Uninitialized);
    ULONG written = 0;
    const auto status = encrypt ? BCryptEncrypt(handle, raw(input), static_cast<ULONG>(input.size()), nullptr,
                                                raw(chain), static_cast<ULONG>(chain.size()), raw(output),
                                                static_cast<ULONG>(output.size()), &written, BCRYPT_BLOCK_PADDING)
                                : BCryptDecrypt(handle, raw(input), static_cast<ULONG>(input.size()), nullptr,
                                                raw(chain), static_cast<ULONG>(chain.size()), raw(output),
                                                static_cast<ULONG>(output.size()), &written, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) {
        wipe(output);
        return std::nullopt;
    }
    output.truncate(written);
    return output;
}

std::optional<QByteArray> dpapi(const bool protect, const QByteArray& input) {
    DATA_BLOB in{static_cast<DWORD>(input.size()), raw(input)};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof dpapi_entropy - 1),
                      reinterpret_cast<BYTE*>(const_cast<char*>(dpapi_entropy))};
    DATA_BLOB out{};
    const auto ok = protect
                        ? CryptProtectData(&in, L"RC Music room identity", &entropy, nullptr, nullptr,
                                           CRYPTPROTECT_UI_FORBIDDEN, &out)
                        : CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok)
        return std::nullopt;
    QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

QByteArray passphraseText(const QByteArray& passphrase) {
    return passphrase.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

// --- key structures ---------------------------------------------------------------------------------
// PKCS#8 PrivateKeyInfo around a SEC1 ECPrivateKey that carries its public key ([1]), so Mbed TLS
// never has to recompute it.
QByteArray privateKeyInfo(const QByteArray& scalar, const QByteArray& point) {
    const auto ec_private_key = tlv(Sequence, integer(1) + tlv(OctetString, scalar) +
                                                  tlv(Explicit1, tlv(BitString, QByteArray(1, '\0') + point)));
    return tlv(Sequence, integer(0) + tlv(Sequence, tlv(Oid, oid_ec_public_key) + tlv(Oid, oid_prime256v1)) +
                             tlv(OctetString, ec_private_key));
}

struct EcKey final {
    QByteArray scalar; // d, 32 bytes big-endian
    QByteArray point;  // 04 | X | Y
    ~EcKey() { wipe(scalar); }
};
// Strict reader for exactly what privateKeyInfo() writes (the [0] parameters are optional in SEC1).
bool parsePrivateKeyInfo(const QByteArray& der, EcKey& key) {
    DerReader outer(der);
    const auto info = outer.take(Sequence);
    if (!info || !outer.done())
        return false;
    DerReader fields(*info);
    if (!fields.expect(Integer, QByteArray(1, '\0')))
        return false;
    const auto algorithm = fields.take(Sequence);
    const auto wrapped = fields.take(OctetString);
    if (!algorithm || !wrapped || !fields.done())
        return false;
    DerReader identifier(*algorithm);
    if (!identifier.expect(Oid, oid_ec_public_key) || !identifier.expect(Oid, oid_prime256v1) || !identifier.done())
        return false;
    DerReader sec1_outer(*wrapped);
    const auto sec1 = sec1_outer.take(Sequence);
    if (!sec1 || !sec1_outer.done())
        return false;
    DerReader sec1_fields(*sec1);
    if (!sec1_fields.expect(Integer, QByteArray(1, '\x01')))
        return false;
    const auto scalar = sec1_fields.take(OctetString);
    if (!scalar || scalar->size() != coordinate_size)
        return false;
    if (sec1_fields.startsWith(Explicit0)) {
        const auto parameters = sec1_fields.take(Explicit0);
        if (!parameters)
            return false;
        DerReader curve(*parameters);
        if (!curve.expect(Oid, oid_prime256v1) || !curve.done())
            return false;
    }
    const auto public_key = sec1_fields.take(Explicit1);
    if (!public_key || !sec1_fields.done())
        return false;
    DerReader bits_outer(*public_key);
    const auto bits = bits_outer.take(BitString);
    if (!bits || !bits_outer.done() || bits->size() != 2 + 2 * coordinate_size || (*bits)[0] != '\0' ||
        (*bits)[1] != '\x04')
        return false;
    key.scalar = scalar->toByteArray();
    key.point = bits->sliced(1).toByteArray();
    return true;
}

// EncryptedPrivateKeyInfo with PBES2 = PBKDF2-HMAC-SHA256 + AES-256-CBC (RFC 8018), the strongest
// PBES2 combination Mbed TLS 3.6.8 parses (pkcs5.c accepts the aes*-CBC OIDs of oid.c).
std::optional<QByteArray> encryptPrivateKey(const QByteArray& key_info, const QByteArray& passphrase_text) {
    const auto salt = randomBytes(salt_size);
    const auto iv = randomBytes(iv_size);
    if (!salt || !iv)
        return std::nullopt;
    auto key = pbkdf2(passphrase_text, *salt, pbkdf2_iterations);
    if (!key)
        return std::nullopt;
    const auto encrypted = aesCbc(true, std::move(*key), *iv, key_info);
    if (!encrypted)
        return std::nullopt;
    const auto kdf = tlv(Sequence, tlv(Oid, oid_pbkdf2) +
                                       tlv(Sequence, tlv(OctetString, *salt) + integer(pbkdf2_iterations) +
                                                         tlv(Sequence, tlv(Oid, oid_hmac_sha256) + tlv(Null, {}))));
    const auto scheme = tlv(Sequence, tlv(Oid, oid_aes256_cbc) + tlv(OctetString, *iv));
    const auto algorithm = tlv(Sequence, tlv(Oid, oid_pbes2) + tlv(Sequence, kdf + scheme));
    return tlv(Sequence, algorithm + tlv(OctetString, *encrypted));
}
std::optional<QByteArray> decryptPrivateKey(const QByteArray& der, const QByteArray& passphrase_text) {
    DerReader outer(der);
    const auto document = outer.take(Sequence);
    if (!document || !outer.done())
        return std::nullopt;
    DerReader fields(*document);
    const auto algorithm = fields.take(Sequence);
    const auto encrypted = fields.take(OctetString);
    if (!algorithm || !encrypted || !fields.done() || encrypted->isEmpty() || encrypted->size() % 16 != 0)
        return std::nullopt;
    DerReader pbes2(*algorithm);
    if (!pbes2.expect(Oid, oid_pbes2))
        return std::nullopt;
    const auto parameters = pbes2.take(Sequence);
    if (!parameters || !pbes2.done())
        return std::nullopt;
    DerReader schemes(*parameters);
    const auto kdf = schemes.take(Sequence);
    const auto cipher = schemes.take(Sequence);
    if (!kdf || !cipher || !schemes.done())
        return std::nullopt;
    DerReader kdf_fields(*kdf);
    if (!kdf_fields.expect(Oid, oid_pbkdf2))
        return std::nullopt;
    const auto kdf_parameters = kdf_fields.take(Sequence);
    if (!kdf_parameters || !kdf_fields.done())
        return std::nullopt;
    DerReader pbkdf2_fields(*kdf_parameters);
    const auto salt = pbkdf2_fields.take(OctetString);
    const auto count = pbkdf2_fields.take(Integer);
    const auto prf = pbkdf2_fields.take(Sequence);
    if (!salt || salt->size() < 8 || salt->size() > 64 || !count || !prf || !pbkdf2_fields.done())
        return std::nullopt;
    const auto iterations = positiveInteger(*count);
    DerReader prf_fields(*prf);
    if (!iterations || *iterations > maximum_iterations || !prf_fields.expect(Oid, oid_hmac_sha256) ||
        !prf_fields.expect(Null, {}) || !prf_fields.done())
        return std::nullopt;
    DerReader cipher_fields(*cipher);
    if (!cipher_fields.expect(Oid, oid_aes256_cbc))
        return std::nullopt;
    const auto iv = cipher_fields.take(OctetString);
    if (!iv || iv->size() != iv_size || !cipher_fields.done())
        return std::nullopt;
    auto key = pbkdf2(passphrase_text, salt->toByteArray(), *iterations);
    if (!key)
        return std::nullopt;
    return aesCbc(false, std::move(*key), iv->toByteArray(), encrypted->toByteArray());
}

// --- certificate --------------------------------------------------------------------------------------
using CertificatePtr = std::unique_ptr<const CERT_CONTEXT, decltype(&CertFreeCertificateContext)>;

CertificatePtr parseCertificate(const QByteArray& der) {
    return {CertCreateCertificateContext(X509_ASN_ENCODING, raw(der), static_cast<DWORD>(der.size())),
            &CertFreeCertificateContext};
}
// An ECDSA P-256 certificate whose signature verifies with its own public key.
bool isSelfSignedP256(const CERT_CONTEXT* certificate) {
    const auto& spki = certificate->pCertInfo->SubjectPublicKeyInfo;
    const auto& parameters = spki.Algorithm.Parameters;
    if (!spki.Algorithm.pszObjId || std::strcmp(spki.Algorithm.pszObjId, szOID_ECC_PUBLIC_KEY) != 0 ||
        QByteArrayView(parameters.pbData, parameters.cbData) != tlv(Oid, oid_prime256v1) ||
        spki.PublicKey.cbData != 1 + 2 * coordinate_size || spki.PublicKey.cUnusedBits != 0 ||
        spki.PublicKey.pbData[0] != 0x04)
        return false;
    return CryptVerifyCertificateSignatureEx(0, X509_ASN_ENCODING, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,
                                             const_cast<CERT_CONTEXT*>(certificate), CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,
                                             const_cast<CERT_CONTEXT*>(certificate), 0, nullptr);
}
// The stored key is the certificate's: same public point, and a signature by the scalar verifies
// with the certificate's key (BCrypt does not check that d and Q belong together on import).
bool keyBelongsToCertificate(const EcKey& key, const CERT_CONTEXT* certificate) {
    const auto& spki = certificate->pCertInfo->SubjectPublicKeyInfo;
    if (QByteArrayView(spki.PublicKey.pbData, spki.PublicKey.cbData) != key.point)
        return false;
    BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PRIVATE_P256_MAGIC, static_cast<ULONG>(coordinate_size)};
    QByteArray blob(reinterpret_cast<const char*>(&header), sizeof header);
    blob += key.point.sliced(1) + key.scalar;
    const auto cleanup = qScopeGuard([&] { wipe(blob); });
    BCRYPT_KEY_HANDLE private_key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, nullptr, BCRYPT_ECCPRIVATE_BLOB, &private_key,
                                            raw(blob), static_cast<ULONG>(blob.size()), 0)))
        return false;
    const auto release_private = qScopeGuard([&] { BCryptDestroyKey(private_key); });
    BCRYPT_KEY_HANDLE public_key = nullptr;
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, const_cast<CERT_PUBLIC_KEY_INFO*>(&spki), 0, nullptr,
                                     &public_key))
        return false;
    const auto release_public = qScopeGuard([&] { BCryptDestroyKey(public_key); });
    const auto digest = randomBytes(32);
    QByteArray signature(2 * coordinate_size, Qt::Uninitialized);
    ULONG written = 0;
    return digest &&
           BCRYPT_SUCCESS(BCryptSignHash(private_key, nullptr, raw(*digest), static_cast<ULONG>(digest->size()),
                                         raw(signature), static_cast<ULONG>(signature.size()), &written, 0)) &&
           BCRYPT_SUCCESS(BCryptVerifySignature(public_key, nullptr, raw(*digest), static_cast<ULONG>(digest->size()),
                                                raw(signature), written, 0));
}

struct GeneratedKey final {
    QByteArray certificate_der;
    EcKey key;
};
SYSTEMTIME shifted(const FILETIME base, const LONGLONG days) {
    ULARGE_INTEGER value{};
    value.LowPart = base.dwLowDateTime;
    value.HighPart = base.dwHighDateTime;
    value.QuadPart += static_cast<ULONGLONG>(days * 24LL * 3600LL * 10000000LL);
    const FILETIME moved{value.LowPart, value.HighPart};
    SYSTEMTIME time{};
    FileTimeToSystemTime(&moved, &time);
    return time;
}
// Ephemeral (unnamed) NCrypt key, exportable once in plaintext so it can be stored encrypted;
// CertCreateSelfSignCertificate signs the certificate with ECDSA-SHA256.
std::optional<GeneratedKey> generateKey() {
    NCRYPT_PROV_HANDLE provider = 0;
    if (NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS)
        return std::nullopt;
    const auto close_provider = qScopeGuard([&] { NCryptFreeObject(provider); });
    NCRYPT_KEY_HANDLE key = 0;
    if (NCryptCreatePersistedKey(provider, &key, NCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0, 0) != ERROR_SUCCESS)
        return std::nullopt;
    const auto close_key = qScopeGuard([&] { NCryptFreeObject(key); });
    DWORD policy = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    if (NCryptSetProperty(key, NCRYPT_EXPORT_POLICY_PROPERTY, reinterpret_cast<PBYTE>(&policy), sizeof policy, 0) !=
            ERROR_SUCCESS ||
        NCryptFinalizeKey(key, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
        return std::nullopt;

    BYTE name[128];
    DWORD name_size = sizeof name;
    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=RC Music room", CERT_X500_NAME_STR, nullptr, name, &name_size, nullptr))
        return std::nullopt;
    CERT_NAME_BLOB subject{name_size, name};
    CRYPT_ALGORITHM_IDENTIFIER signature{const_cast<LPSTR>(szOID_ECDSA_SHA256), {}};
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    // Peers pin the fingerprint and ignore validity, so a long life keeps the listener ID stable.
    auto start = shifted(now, -1);
    auto end = shifted(now, 20 * 365);
    const CertificatePtr certificate(CertCreateSelfSignCertificate(static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(key),
                                                                   &subject, CERT_CREATE_SELFSIGN_NO_KEY_INFO, nullptr,
                                                                   &signature, &start, &end, nullptr),
                                     &CertFreeCertificateContext);
    if (!certificate)
        return std::nullopt;

    DWORD blob_size = 0;
    if (NCryptExportKey(key, 0, BCRYPT_ECCPRIVATE_BLOB, nullptr, nullptr, 0, &blob_size, 0) != ERROR_SUCCESS ||
        blob_size != sizeof(BCRYPT_ECCKEY_BLOB) + 3 * coordinate_size)
        return std::nullopt;
    QByteArray blob(static_cast<qsizetype>(blob_size), Qt::Uninitialized);
    const auto cleanup = qScopeGuard([&] { wipe(blob); });
    if (NCryptExportKey(key, 0, BCRYPT_ECCPRIVATE_BLOB, nullptr, raw(blob), blob_size, &blob_size, 0) != ERROR_SUCCESS)
        return std::nullopt;
    BCRYPT_ECCKEY_BLOB header{};
    std::memcpy(&header, blob.constData(), sizeof header);
    if (header.dwMagic != BCRYPT_ECDSA_PRIVATE_P256_MAGIC || header.cbKey != coordinate_size)
        return std::nullopt;
    GeneratedKey generated;
    generated.certificate_der = QByteArray(reinterpret_cast<const char*>(certificate->pbCertEncoded),
                                           static_cast<qsizetype>(certificate->cbCertEncoded));
    const auto body = QByteArrayView(blob).sliced(sizeof header);
    generated.key.point = QByteArray(1, '\x04') + body.first(2 * coordinate_size).toByteArray();
    generated.key.scalar = body.sliced(2 * coordinate_size, coordinate_size).toByteArray();
    return generated;
}

// --- files ------------------------------------------------------------------------------------------------
std::optional<QByteArray> readSmallFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximum_file_size)
        return std::nullopt;
    auto bytes = file.read(maximum_file_size + 1);
    if (bytes.size() > maximum_file_size)
        return std::nullopt;
    return bytes;
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

struct Paths final {
    QString certificate;
    QString key;
    QString passphrase;
};

std::optional<RoomIdentity> load(const Paths& paths) {
    const auto certificate_pem = readSmallFile(paths.certificate);
    const auto key_pem = readSmallFile(paths.key);
    const auto protected_passphrase = readSmallFile(paths.passphrase);
    if (!certificate_pem || !key_pem || !protected_passphrase)
        return std::nullopt;

    auto passphrase = dpapi(false, *protected_passphrase);
    const auto wipe_passphrase = qScopeGuard([&] {
        if (passphrase)
            wipe(*passphrase);
    });
    if (!passphrase || passphrase->size() != passphrase_size)
        return std::nullopt;
    auto text = passphraseText(*passphrase);
    const auto wipe_text = qScopeGuard([&] { wipe(text); });

    const auto certificate_der = fromPem(*certificate_pem, "CERTIFICATE");
    const auto encrypted_der = fromPem(*key_pem, "ENCRYPTED PRIVATE KEY");
    if (!certificate_der || !encrypted_der)
        return std::nullopt;
    const auto certificate = parseCertificate(*certificate_der);
    if (!certificate || !isSelfSignedP256(certificate.get()))
        return std::nullopt;
    auto key_info = decryptPrivateKey(*encrypted_der, text);
    const auto wipe_key_info = qScopeGuard([&] {
        if (key_info)
            wipe(*key_info);
    });
    EcKey key;
    if (!key_info || !parsePrivateKeyInfo(*key_info, key) || !keyBelongsToCertificate(key, certificate.get()))
        return std::nullopt;
    const auto fingerprint = sha256(*certificate_der);
    if (!fingerprint)
        return std::nullopt;

    RoomIdentity identity;
    identity.certificate_pem = certificate_pem->toStdString();
    identity.key_pem = key_pem->toStdString();
    identity.fingerprint = *fingerprint;
    identity.certificate_file = QDir::toNativeSeparators(QFileInfo(paths.certificate).absoluteFilePath());
    identity.key_file = QDir::toNativeSeparators(QFileInfo(paths.key).absoluteFilePath());
    identity.key_passphrase = text.toStdString();
    std::copy(passphrase->cbegin(), passphrase->cend(), reinterpret_cast<char*>(identity.passphrase.data()));
    return identity;
}

// Passphrase, then key, then the certificate: the certificate's presence marks a complete set.
bool create(const Paths& paths) {
    auto passphrase = randomBytes(passphrase_size);
    if (!passphrase)
        return false;
    auto text = passphraseText(*passphrase);
    const auto cleanup = qScopeGuard([&] {
        wipe(*passphrase);
        wipe(text);
    });
    const auto generated = generateKey();
    if (!generated)
        return false;
    auto key_info = privateKeyInfo(generated->key.scalar, generated->key.point);
    const auto wipe_key_info = qScopeGuard([&] { wipe(key_info); });
    const auto encrypted = encryptPrivateKey(key_info, text);
    const auto protected_passphrase = dpapi(true, *passphrase);
    return encrypted && protected_passphrase && writeFile(paths.passphrase, *protected_passphrase) &&
           writeFile(paths.key, pem(*encrypted, "ENCRYPTED PRIVATE KEY")) &&
           writeFile(paths.certificate, pem(generated->certificate_der, "CERTIFICATE"));
}

} // namespace

std::optional<RoomIdentity> loadOrCreateRoomIdentity(const QString& config_directory) {
    const auto directory = QDir::cleanPath(config_directory);
    const QFileInfo info(directory);
    if (directory.isEmpty() || (info.exists() && !info.isDir()) || (!info.exists() && !QDir{}.mkpath(directory)))
        return std::nullopt;
    const QDir dir(directory);
    QLockFile lock(dir.filePath(lock_name));
    if (!lock.tryLock(10000))
        return std::nullopt;
    const Paths paths{dir.filePath(certificate_name), dir.filePath(key_name), dir.filePath(passphrase_name)};
    if (!QFileInfo::exists(paths.certificate) && !create(paths))
        return std::nullopt;
    return load(paths);
}

QByteArray roomHostToken(const RoomIdentity& identity, std::span<const std::uint8_t, 16> room_id) {
    if (std::all_of(identity.passphrase.cbegin(), identity.passphrase.cend(), [](auto byte) { return byte == 0; }))
        return {};
    auto token = hkdfSha256(
        QByteArrayView(reinterpret_cast<const char*>(identity.passphrase.data()), passphrase_size),
        QByteArrayView(reinterpret_cast<const char*>(room_id.data()), static_cast<qsizetype>(room_id.size())),
        host_token_info, 32);
    const auto cleanup = qScopeGuard([&] { wipe(token); });
    if (token.size() != 32)
        return {};
    return token.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace music_player::m0
