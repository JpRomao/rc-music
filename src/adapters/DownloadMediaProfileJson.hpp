#pragma once

#include "adapters/DownloadMediaProfile.hpp"

#include <QByteArray>
#include <QByteArrayView>

namespace music_player::m0 {
struct DownloadMediaProfileJsonResult final {
    DownloadCheckpointLoadStatus status{DownloadCheckpointLoadStatus::Invalid};
    std::optional<DownloadMediaProfileRecord> record{};
};

class DownloadMediaProfileJson final {
  public:
    static constexpr qsizetype maximum_file_size = 8 * 1024;
    [[nodiscard]] static QByteArray encode(const DownloadMediaProfileRecord&);
    [[nodiscard]] static DownloadMediaProfileJsonResult decode(QByteArrayView);
};
} // namespace music_player::m0
