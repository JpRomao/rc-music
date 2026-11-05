#pragma once

#include "adapters/WindowsConfiguredDownloadExecution.hpp"
#include "adapters/WindowsPrivateDownloadDocument.hpp"
#include "app/PlayerDownloadComponentProbe.hpp"
#include "presentation/WindowsPlayerDownloads.hpp"

#include <QString>

#include <functional>
#include <optional>

namespace music_player::m0 {
// Reviewed C++ build composition only. Default paths must already exist; the
// explicit storage opt-in prepares only the fixed layout on its worker.
// The provider never creates directories, repairs ACLs or discovers tools.
// Borrowed option dependencies must outlive the player download owner.
struct WindowsPlayerDownloadProfile final {
    WindowsDownloadExecutionProfile execution{};
    QString checkpoint_directory{};
    WindowsPlayerDownloadsOptions options{};
    bool prepare_storage{false};
    // Packaged optional tools require the fixed installed-component marker
    // before the main thread creates the download owner or any listener.
    // Legacy trusted profiles keep their existing eager composition by default.
    bool requires_installed_component{false};
    PlayerDownloadComponentReader component_reader{};
};

using WindowsPlayerDownloadStoragePreparer = std::function<bool(const QString&)>;

// Passive snapshot of config_root/checkpoint strings, including raw Qt data.
// The existing checkpoint worker owns the returned store and its destruction.
// Opt-in always uses the fixed layout's state directory. Only load may attempt
// preparation until its first success; failure/throw is Unreadable and an
// explicit later load can retry. Save before success fails without preparation.
// Success never reprovisions storage; the existing backend revalidates files.
[[nodiscard]] SingleWorkerDownloadCheckpointIo::Factory
makePlayerDownloadCheckpointFactory(const WindowsPlayerDownloadProfile& profile, const QString& config_root,
                                    WindowsPlayerDownloadStoragePreparer prepare = prepareWindowsDownloadStorage);

// Trusted compiled prototype with package-relative source paths. Pure overload
// rebases sources without filesystem inspection/hashing, snapshots Qt paths and
// pins, enables fixed private storage and supplies an explicit YouTube
// environment/UTC clock when the resolver is already present. Other roles,
// limits, options and Native identities retain their existing contracts.
[[nodiscard]] std::optional<WindowsPlayerDownloadProfile>
makePackagedPlayerDownloadProfile(WindowsPlayerDownloadProfile profile, const QString& config_root,
                                  const QString& application_directory, const QString& windows_directory);
// Reads only bounded process-image/Windows-directory Win32 path values; never
// derives either location from CWD, argv or inherited environment.
[[nodiscard]] std::optional<WindowsPlayerDownloadProfile>
makePackagedPlayerDownloadProfile(WindowsPlayerDownloadProfile profile, const QString& config_root);

// Trusted compiled Native identities only. Missing support or two disabled
// channels returns nullopt. Providers may use this directly without repeating
// origins/endpoints/image allowlists; peer-check callbacks remain unconfigured.
[[nodiscard]] std::optional<WindowsPlayerNativeIngressOptions> compiledPlayerNativeIngressProfiles();

// No configured header means no composition. Building optional modules alone
// does not enable admission, native listeners, tools or remote acquisition.
// Active provider channels must match the compiled identities. Origins compare
// exactly; pipe names and image allowsets ignore case (and image ordering).
// A compiled active channel may be omitted by the provider as an opt-out.
[[nodiscard]] std::optional<WindowsPlayerDownloadProfile> playerDownloadsBuildProfile(const QString& config_root);
} // namespace music_player::m0
