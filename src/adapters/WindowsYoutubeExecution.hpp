#pragma once

#include "WindowsYoutubeRuntime.hpp"
#include "YoutubeDownloadExecution.hpp"

#include <QProcessEnvironment>

#include <functional>
#include <memory>

namespace music_player::m0 {
// Trusted worker-only seam. Each call must return a fresh, independent private
// runtime lease, or null with no escaped resources. The default preparer is
// WindowsYoutubeRuntime::prepare. A callback is never a page/plugin setting.
using WindowsYoutubeRuntimePreparer =
    std::function<std::shared_ptr<const WindowsYoutubeRuntime>(const WindowsYoutubeRuntimeConfig&)>;

// Pure composition: snapshots all configuration strings, hashes and explicit
// environment values before capture, without filesystem/native I/O or calling
// the preparer. The returned factory constructs only a passive broker. Runtime
// preparation occurs in broker start on the existing executor worker; launching
// waits for its uncancelled poll. Each job invokes preparation independently.
// No PATH discovery, stock CLI, receipt loader or production enable is added.
[[nodiscard]] YoutubeResolverProcessFactory
makeWindowsYoutubeBrokerResolverFactory(WindowsYoutubeRuntimeConfig config, QProcessEnvironment environment,
                                        WindowsYoutubeRuntimePreparer prepare = {});
// ADR-0019 search counterpart: the same snapshot and per-search preparation,
// with brokers in search mode (extractor only, `--search <query>`).
[[nodiscard]] YoutubeResolverProcessFactory
makeWindowsYoutubeBrokerSearchFactory(WindowsYoutubeRuntimeConfig config, QProcessEnvironment environment,
                                      WindowsYoutubeRuntimePreparer prepare = {});
} // namespace music_player::m0
