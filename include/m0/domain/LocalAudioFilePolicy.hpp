#pragma once

#include <array>
#include <filesystem>
#include <string_view>

namespace music_player::m0 {

class LocalAudioFilePolicy final {
  public:
    using CandidateExtensions = std::array<std::string_view, 7>;

    [[nodiscard]] static const CandidateExtensions& candidateExtensions() noexcept;
    [[nodiscard]] static bool isCandidate(const std::filesystem::path& source_path) noexcept;
};

} // namespace music_player::m0
