#include "m0/domain/LocalAudioFilePolicy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string_view>

namespace music_player::m0::test {
namespace {

TEST_CASE("local audio candidates use the validated extension matrix case-insensitively",
          "[m0][qt][core][local-source][FR-003][FR-006]") {
    constexpr std::array<std::string_view, 7> expected{
        ".mp3", ".m4a", ".mp4", ".flac", ".ogg", ".oga", ".wav",
    };

    CHECK(LocalAudioFilePolicy::candidateExtensions() == expected);
    CHECK(LocalAudioFilePolicy::isCandidate(std::filesystem::path{LR"(C:\Música\Faixa.MP3)"}));
    CHECK(LocalAudioFilePolicy::isCandidate(std::filesystem::path{LR"(C:\Música\Faixa.FlAc)"}));
    CHECK(LocalAudioFilePolicy::isCandidate(std::filesystem::path{LR"(C:\Música\Faixa.OGA)"}));
}

TEST_CASE("local audio candidates reject directories by name and unsupported or missing extensions",
          "[m0][qt][core][local-source][FR-003][FR-006]") {
    CHECK_FALSE(LocalAudioFilePolicy::isCandidate(std::filesystem::path{LR"(C:\Música\Pasta.mp3\)"}));
    CHECK_FALSE(LocalAudioFilePolicy::isCandidate(std::filesystem::path{LR"(C:\Música\Capa.jpg)"}));
    CHECK_FALSE(LocalAudioFilePolicy::isCandidate(std::filesystem::path{LR"(C:\Música\Sem extensão)"}));
    CHECK_FALSE(LocalAudioFilePolicy::isCandidate({}));
}

} // namespace
} // namespace music_player::m0::test
