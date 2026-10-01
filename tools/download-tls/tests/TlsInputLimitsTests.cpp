#include "rc_music_tls_input_limits.h"

#include <array>
#include <cstdint>
#include <limits>

int main() {
    struct Example {
        std::int64_t current;
        std::int64_t additional;
        bool allowed;
    };
    constexpr std::array examples{
        Example{0, 0, true},
        Example{0, 262144, true},
        Example{262143, 1, true},
        Example{262144, 0, true},
        Example{262144, 1, false},
        Example{262145, 0, false},
        Example{-1, 0, false},
        Example{0, -1, false},
        Example{0, std::numeric_limits<std::int64_t>::max(), false},
        Example{std::numeric_limits<std::int64_t>::max(), 1, false},
        Example{245760, 16384, true},
        Example{245761, 16384, false},
    };
    for (const auto& example : examples) {
        if (rcMusicTlsInputFits(example.current, example.additional) != example.allowed)
            return 1;
    }
    return 0;
}
