#pragma once

#include "m0/application/DownloadCheckpoint.hpp"
#include "m0/ports/DownloadRequestIdentity.hpp"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace music_player::m0 {

enum class DownloadCheckpointJsonStatus { Valid, Invalid, FutureVersion };

struct DownloadCheckpointJsonResult final {
    DownloadCheckpointJsonStatus status{DownloadCheckpointJsonStatus::Invalid};
    std::optional<DownloadCheckpoint> checkpoint{};
};

// A strict bounded private document, never authority to execute/import/delete media.
class DownloadCheckpointJson final {
  public:
    static constexpr int schema_version = 1;
    static constexpr qsizetype max_document_bytes = 2 * 1024 * 1024;
    [[nodiscard]] static QByteArray encode(const DownloadCheckpoint& checkpoint);
    [[nodiscard]] static DownloadCheckpointJsonResult decode(QByteArrayView bytes);
};

// Hashes only the ordered typed items. UUIDs and all filesystem state are excluded.
// Invalid typed items return an empty identity; no lossy UTF-8 replacement is allowed.
class QtDownloadRequestIdentity final : public DownloadRequestIdentity {
  public:
    [[nodiscard]] std::string fingerprint(const DownloadRequest& request) override;
};

} // namespace music_player::m0
