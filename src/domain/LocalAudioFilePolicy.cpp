#include "m0/domain/LocalAudioFilePolicy.hpp"

#include <algorithm>

namespace music_player::m0 {
namespace {

constexpr LocalAudioFilePolicy::CandidateExtensions candidate_extensions{
    ".mp3", ".m4a", ".mp4", ".flac", ".ogg", ".oga", ".wav",
};

template <typename Character> [[nodiscard]] constexpr Character lowerAscii(const Character value) noexcept {
    constexpr Character uppercase_a = static_cast<Character>('A');
    constexpr Character uppercase_z = static_cast<Character>('Z');
    constexpr Character case_offset = static_cast<Character>('a' - 'A');
    return value >= uppercase_a && value <= uppercase_z ? static_cast<Character>(value + case_offset) : value;
}

[[nodiscard]] bool equalsCandidateExtension(const std::filesystem::path::string_type& extension,
                                            const std::string_view candidate) noexcept {
    if (extension.size() != candidate.size())
        return false;
    return std::ranges::equal(extension, candidate, [](const auto left, const char right) {
        return lowerAscii(left) == static_cast<decltype(left)>(right);
    });
}

} // namespace

const LocalAudioFilePolicy::CandidateExtensions& LocalAudioFilePolicy::candidateExtensions() noexcept {
    return candidate_extensions;
}

bool LocalAudioFilePolicy::isCandidate(const std::filesystem::path& source_path) noexcept {
    if (source_path.empty() || source_path.filename().empty())
        return false;
    const auto extension = source_path.extension().native();
    return std::ranges::any_of(candidate_extensions, [&extension](const std::string_view candidate) {
        return equalsCandidateExtension(extension, candidate);
    });
}

} // namespace music_player::m0
