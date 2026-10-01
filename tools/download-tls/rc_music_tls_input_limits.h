// RC Music's explicit limit for the copied Qt Schannel input accumulator.
// This bounds the Qt-owned buffer, not allocations inside the Windows TLS engine.
#pragma once

#include <cstdint>

inline constexpr std::int64_t rcMusicMaximumTlsInput = 256 * 1024;

[[nodiscard]] constexpr bool rcMusicTlsInputFits(std::int64_t current, std::int64_t additional) noexcept {
    return current >= 0 && additional >= 0 && current <= rcMusicMaximumTlsInput &&
           additional <= rcMusicMaximumTlsInput - current;
}
