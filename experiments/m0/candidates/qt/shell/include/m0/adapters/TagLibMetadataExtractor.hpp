#pragma once

#include "m0/ports/MetadataExtractor.hpp"

#include <functional>
#include <system_error>

namespace music_player::m0 {

struct TagLibMetadataExtractorOptions final {
    std::function<std::filesystem::file_time_type(const std::filesystem::path&, std::error_code&)> last_write_time{};
};

class TagLibMetadataExtractor final : public MetadataExtractor {
  public:
    explicit TagLibMetadataExtractor(TagLibMetadataExtractorOptions options = {});

    [[nodiscard]] MetadataReadResult read(const std::filesystem::path& source_path) const override;

  private:
    TagLibMetadataExtractorOptions options_;
};

} // namespace music_player::m0
