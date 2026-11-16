#include "SignedYoutubeRuntimeManifest.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace music_player::m0 {
namespace {
using Error = SignedYoutubeRuntimeManifestError;
using Role = WindowsYoutubeRuntimeRole;

constexpr quint64 max_file_bytes = 128ULL * 1024 * 1024;
constexpr double max_json_integer = 9007199254740991.0; // 2^53 - 1
constexpr std::array<const char*, 4> unique_role_names{"Extractor", "Node", "EjsCore", "EjsLibrary"};
constexpr std::array<Role, 4> unique_roles{Role::Extractor, Role::Node, Role::EjsCore, Role::EjsLibrary};
constexpr std::array<const char*, 4> unique_role_paths{"extractor/yt-dlp.exe", "javascript/node.exe",
                                                       "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js",
                                                       "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"};

class KeyHandles final {
  public:
    ~KeyHandles() {
        if (key)
            BCryptDestroyKey(key);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_KEY_HANDLE key{};
};

// nullopt: the key itself is unusable; false: the signature does not match.
std::optional<bool> verifyEcdsaP256(QByteArrayView public_key, QByteArrayView message, QByteArrayView signature) {
    if (public_key.size() != youtube_runtime_public_key_bytes)
        return std::nullopt;
    KeyHandles handles;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)))
        return std::nullopt;
    QByteArray blob(sizeof(BCRYPT_ECCKEY_BLOB), '\0');
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    blob.append(public_key.data(), public_key.size());
    // CNG refuses points that are not on the curve, such as all zeros.
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(handles.algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &handles.key,
                                            reinterpret_cast<PUCHAR>(blob.data()), static_cast<ULONG>(blob.size()), 0)))
        return std::nullopt;
    if (signature.size() != youtube_runtime_signature_bytes)
        return false;
    auto hash = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
    auto copy = signature.toByteArray();
    return BCRYPT_SUCCESS(BCryptVerifySignature(handles.key, nullptr, reinterpret_cast<PUCHAR>(hash.data()),
                                                static_cast<ULONG>(hash.size()), reinterpret_cast<PUCHAR>(copy.data()),
                                                static_cast<ULONG>(copy.size()), 0));
}

bool exactKeys(const QJsonObject& object, const QStringList& keys) {
    if (object.size() != keys.size())
        return false;
    return std::all_of(keys.begin(), keys.end(), [&](const QString& key) { return object.contains(key); });
}

std::optional<quint64> integer(const QJsonValue& value, quint64 maximum) {
    if (!value.isDouble())
        return std::nullopt;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > max_json_integer || std::floor(number) != number ||
        number > static_cast<double>(maximum))
        return std::nullopt;
    return static_cast<quint64>(number);
}

bool lowerHexPin(const QString& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](QChar c) {
               return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
           });
}

// Relative, '/'-separated and confined to the runtime tree; prepare() re-checks it.
bool relativePath(const QString& value) {
    if (value.isEmpty() || value.size() > youtube_runtime_max_relative_units || value.startsWith(u'/') ||
        value.contains(u'\\') || value.contains(u':'))
        return false;
    const auto segments = value.split(u'/');
    if (segments.size() > youtube_runtime_max_segments)
        return false;
    return std::none_of(segments.begin(), segments.end(), [](const QString& segment) {
        return segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String("..") ||
               std::any_of(segment.begin(), segment.end(), [](QChar c) { return c.unicode() < 0x20; });
    });
}

bool printableVersion(const QString& value) {
    return !value.isEmpty() && value.size() <= 32 &&
           std::all_of(value.begin(), value.end(), [](QChar c) { return c.unicode() > 0x20 && c.unicode() < 0x7f; });
}

std::optional<SignedYoutubeRuntimeManifest> decode(QByteArrayView bytes, const QString& prefix) {
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(bytes.toByteArray(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const auto root = document.object();
    if (!exactKeys(root, {"schemaVersion", "purpose", "sequence", "ytDlpVersion", "artifacts"}) ||
        integer(root.value("schemaVersion"), 1) != quint64{1} ||
        root.value("purpose").toString() != QLatin1String("rcmusic-youtube-runtime-v1") ||
        !root.value("ytDlpVersion").isString() || !printableVersion(root.value("ytDlpVersion").toString()) ||
        !root.value("artifacts").isArray())
        return std::nullopt;
    const auto sequence = integer(root.value("sequence"), static_cast<quint64>(max_json_integer));
    const auto list = root.value("artifacts").toArray();
    if (!sequence || *sequence == 0 || list.isEmpty() ||
        static_cast<std::size_t>(list.size()) > youtube_runtime_max_files)
        return std::nullopt;
    SignedYoutubeRuntimeManifest result;
    result.sequence = *sequence;
    result.yt_dlp_version = root.value("ytDlpVersion").toString();
    std::array<bool, 4> seen{};
    QSet<QString> paths;
    quint64 total = 0;
    for (const auto& entry : list) {
        if (!entry.isObject())
            return std::nullopt;
        const auto object = entry.toObject();
        if (!exactKeys(object, {"role", "path", "bytes", "sha256"}) || !object.value("role").isString() ||
            !object.value("path").isString() || !object.value("sha256").isString())
            return std::nullopt;
        const auto path = object.value("path").toString();
        const auto sha = object.value("sha256").toString();
        const auto size = integer(object.value("bytes"), max_file_bytes);
        if (!relativePath(path) || !lowerHexPin(sha) || !size || *size > youtube_runtime_max_bytes - total)
            return std::nullopt;
        total += *size;
        // Windows paths are case-insensitive: two spellings would name one file.
        if (paths.contains(path.toLower()))
            return std::nullopt;
        paths.insert(path.toLower());
        const auto name = object.value("role").toString();
        auto role = Role::Support;
        if (name != QLatin1String("Support")) {
            const auto found = std::find_if(unique_role_names.begin(), unique_role_names.end(),
                                            [&](const char* candidate) { return name == QLatin1String(candidate); });
            if (found == unique_role_names.end())
                return std::nullopt;
            const auto index = static_cast<std::size_t>(found - unique_role_names.begin());
            if (seen[index] || path != QLatin1String(unique_role_paths[index]))
                return std::nullopt;
            seen[index] = true;
            role = unique_roles[index];
        }
        result.artifacts.push_back({role, path, prefix + path, sha.toLatin1()});
    }
    if (std::any_of(seen.begin(), seen.end(), [](bool present) { return !present; }))
        return std::nullopt;
    return result;
}
} // namespace

SignedYoutubeRuntimeManifestResult verifySignedYoutubeRuntimeManifest(QByteArrayView manifest, QByteArrayView signature,
                                                                      QByteArrayView public_key,
                                                                      const quint64 minimum_sequence,
                                                                      const QString& source_prefix) {
    if (manifest.size() > youtube_runtime_manifest_max_bytes)
        return {.error = Error::Malformed};
    const auto verified = verifyEcdsaP256(public_key, manifest, signature);
    if (!verified)
        return {.error = Error::InvalidKey};
    if (!*verified)
        return {.error = Error::BadSignature};
    auto decoded = decode(manifest, source_prefix);
    if (!decoded)
        return {.error = Error::Malformed};
    if (decoded->sequence < minimum_sequence)
        return {.error = Error::Superseded};
    return {.error = Error::None, .manifest = std::move(decoded)};
}
} // namespace music_player::m0
