#pragma once

#include "adapters/DownloadOwnership.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QStringView>

namespace music_player::m0 {

inline constexpr qsizetype maximum_download_publication_leaf_units = 180;
[[nodiscard]] bool validDownloadPublicationLeaf(QStringView leaf) noexcept;

struct DownloadOwnershipJsonResult final {
    DownloadCheckpointLoadStatus status{DownloadCheckpointLoadStatus::Invalid};
    std::optional<DownloadOwnershipJournal> journal{};
};

class DownloadOwnershipJson final {
  public:
    static constexpr qsizetype maximum_file_size = 256 * 1024;
    [[nodiscard]] static QByteArray encode(const DownloadOwnershipJournal&);
    [[nodiscard]] static DownloadOwnershipJsonResult decode(QByteArrayView);
};

} // namespace music_player::m0
