#pragma once

#include "adapters/NativeDownloadSession.hpp"
#include "adapters/WindowsNativeMessagingIo.hpp"

#include <optional>

namespace music_player::m0 {
// Input may synchronously stall. Its completion clock, never a timestamp
// captured before the Win32 call, must govern assembly/admission deadlines.
template <typename Read, typename Clock>
[[nodiscard]] std::optional<NativeSessionResult> readNativeHostInput(NativeDownloadSession& session, Read read,
                                                                     Clock clock) {
    const auto input = read();
    const auto now = clock();
    switch (input.state) {
    case NativeStdioInputState::Data:
        return session.receive(input.bytes, now);
    case NativeStdioInputState::Waiting:
        return session.poll(now);
    case NativeStdioInputState::Closed:
        return session.finishInput(now);
    case NativeStdioInputState::Failed:
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace music_player::m0
