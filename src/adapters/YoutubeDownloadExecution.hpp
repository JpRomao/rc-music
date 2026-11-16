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
// One executor/queue owner for the two independently validated ingress ports.
// Only a consistent typed YouTube context selects resolution; legacy items
// keep the direct-source policy and cannot acquire YouTube indirectly.
[[nodiscard]] std::unique_ptr<WindowsDownloadExecution>
makeWindowsMediaAndYoutubeExecution(YoutubeResolverProcessFactory resolver,
                                    WindowsDownloadExecution::TaskFactory destination,
                                    YoutubeResolverEpochClock epoch_clock, YoutubeResolverLimits limits = {},
                                    WindowsDownloadExecution::Preflight preflight = {}, std::size_t lanes = 1);
} // namespace music_player::m0
