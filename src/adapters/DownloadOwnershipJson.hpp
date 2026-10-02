#pragma once

#include "adapters/DownloadOwnership.hpp"

#include <QByteArray>
#include <QByteArrayView>

namespace music_player::m0 {

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
