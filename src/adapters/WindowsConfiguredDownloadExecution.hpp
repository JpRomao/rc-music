#pragma once

#include "WindowsDownloadExecution.hpp"
#include "WindowsYoutubeExecution.hpp"

#include <QProcessEnvironment>

#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
// ADR-0020: compiled trust anchor for the signed runtime manifest.
struct SignedYoutubeRuntimeTrust final {
    QByteArray public_key{}; // P-256 X||Y, 64 raw bytes
    quint64 minimum_sequence{1};
};
struct WindowsYoutubeDownloadProfile final {
    WindowsYoutubeRuntimeConfig runtime{};
    // When set, the packaged composition takes runtime.artifacts from the signed
    // manifest next to the player; the compiled artifact list must then be empty.
    std::optional<SignedYoutubeRuntimeTrust> signed_runtime{};
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
    // Explicit trusted opt-in for a per-job AppContainer. Conflicts with a
    // borrowed media.app_container and is off for the current package profile.
    bool provision_media_profile{false};
    // Trusted composition provides a synchronized snapshot when a new local
    // job is created. An empty selection keeps the existing publish parent.
    std::function<QString()> publication_directory{};
    // L8: jobs that run at the same time, each on its own executor lane.
    std::size_t parallel_jobs{1};
};

struct WindowsDownloadExecutionPreparers final {
    WindowsDownloadRuntimePreparer network{};
    WindowsMediaFoundationRuntimePreparer media{};
    WindowsYoutubeRuntimePreparer youtube{};
    // Consulted only for the per-job opt-in. Empty selects the native backend.
    WindowsMediaProfilePlatformFactory media_profile{};
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
