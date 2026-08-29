#pragma once

#include "m0/ports/MetadataExtractor.hpp"

namespace music_player::m0 {

class TagLibMetadataExtractor final : public MetadataExtractor {
  public:
    [[nodiscard]] MetadataReadResult read(const std::filesystem::path& source_path) const override;
};

} // namespace music_player::m0
