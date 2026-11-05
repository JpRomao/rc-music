#pragma once

#include "WindowsDownloadExecution.hpp"
#include "YoutubeResolverSession.hpp"

namespace music_player::m0 {
using YoutubeResolverProcessFactory = std::function<std::unique_ptr<YoutubeResolverProcess>()>;
using YoutubeResolverEpochClock = std::function<qint64()>;
// Trusted optional composition. Factories are inert and run only on the
// existing private executor worker. Canonical watch URLs stay in the job;
// signed URLs exist only in a resolver capability passed to the next task.
// No Native Messaging registration, runtime provisioning or production enable.
[[nodiscard]] std::unique_ptr<WindowsDownloadExecution>
makeWindowsYoutubeAudioExecution(YoutubeResolverProcessFactory resolver,
                                 WindowsDownloadExecution::TaskFactory destination,
                                 YoutubeResolverEpochClock epoch_clock, YoutubeResolverLimits limits = {});
} // namespace music_player::m0
