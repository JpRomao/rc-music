#pragma once

#include "WindowsDownloadExecution.hpp"
#include "WindowsYoutubeExecution.hpp"

#include <QProcessEnvironment>

#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
struct WindowsYoutubeDownloadProfile final {
    WindowsYoutubeRuntimeConfig runtime{};
    QProcessEnvironment environment{};
    std::function<qint64()> epoch_clock{};
    YoutubeResolverLimits limits{};
};

struct WindowsDownloadExecutionProfile final {
    WindowsDownloadNetworkLaunchConfig network{};
    WindowsMediaFoundationLaunchConfig media{};
    WindowsDownloadJobSessionConfig job{};
    QString journal_directory{};
    std::optional<WindowsYoutubeDownloadProfile> youtube{};
};

struct WindowsDownloadExecutionPreparers final {
    WindowsDownloadRuntimePreparer network{};
    WindowsMediaFoundationRuntimePreparer media{};
    WindowsYoutubeRuntimePreparer youtube{};
};

// Trusted C++ composition only, never a page, environment or manifest loader.
// Snapshots every string/hash before capture. Construction calls no preparer,
// filesystem inspection or native child; the existing executor starts its Qt
// worker thread. Each job owns its journal and fresh prepared runtime leases.
// Borrowed job.ownership_store and a YouTube profile without an explicit clock
// return null before constructing an executor. Runtime/path/hash/environment
// qualification stays on the workers; this is not production enablement.
[[nodiscard]] std::unique_ptr<WindowsDownloadExecution>
makeWindowsConfiguredDownloadExecution(WindowsDownloadExecutionProfile profile,
                                       WindowsDownloadExecutionPreparers preparers = {});
} // namespace music_player::m0
