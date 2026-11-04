#pragma once

#include "m0/ports/DownloadSubmission.hpp"

#include <QByteArray>
#include <QString>

#include <optional>
#include <vector>

namespace music_player::m0 {
struct BrowserMediaSnapshot final {
    QString page_url{};
    std::vector<DownloadItem> candidates{};
    bool truncated{false};
    bool operator==(const BrowserMediaSnapshot&) const = default;
};

// Navigation URLs may carry a fragment. It remains part of the host comparison,
// while the same direct-media hostname policy governs eligibility.
[[nodiscard]] bool browserMediaPageAllowed(const QString& page_url);

// ExecuteScript returns data only. Host-owned document/operation identities are
// deliberately absent: a page cannot authenticate its own browsing context.
[[nodiscard]] std::optional<BrowserMediaSnapshot> decodeBrowserMediaSnapshot(const QByteArray& payload);
} // namespace music_player::m0
