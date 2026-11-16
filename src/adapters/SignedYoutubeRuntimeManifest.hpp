#pragma once

#include "WindowsYoutubeRuntime.hpp"

#include <QByteArrayView>
#include <QString>

#include <optional>

namespace music_player::m0 {

inline constexpr qsizetype youtube_runtime_manifest_max_bytes = 1024 * 1024;
inline constexpr qsizetype youtube_runtime_public_key_bytes = 64; // P-256 X||Y
inline constexpr qsizetype youtube_runtime_signature_bytes = 64;  // IEEE P1363 r||s

struct SignedYoutubeRuntimeManifest final {
    quint64 sequence{0};
    QString yt_dlp_version{};
    // source_path is source_prefix + path, still relative to the player image.
    std::vector<WindowsYoutubeRuntimeSource> artifacts{};
};

enum class SignedYoutubeRuntimeManifestError { None, InvalidKey, BadSignature, Malformed, Superseded };

struct SignedYoutubeRuntimeManifestResult final {
    SignedYoutubeRuntimeManifestError error{SignedYoutubeRuntimeManifestError::Malformed};
    std::optional<SignedYoutubeRuntimeManifest> manifest{};
};

// ADR-0020: the runtime inventory is trusted only through the public key compiled
// into the player. The ECDSA P-256/SHA-256 signature over the exact bytes is checked
// before the JSON is read; the sequence must reach the compiled minimum.
// The result only feeds WindowsYoutubeRuntimeConfig; each job still hashes, copies
// and re-checks the files before any launch.
[[nodiscard]] SignedYoutubeRuntimeManifestResult
verifySignedYoutubeRuntimeManifest(QByteArrayView manifest, QByteArrayView signature, QByteArrayView public_key,
                                   quint64 minimum_sequence, const QString& source_prefix);

} // namespace music_player::m0
