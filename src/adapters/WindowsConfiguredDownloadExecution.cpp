#include "WindowsConfiguredDownloadExecution.hpp"
#include "WindowsNativeMediaProfile.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
QByteArray owned(const QByteArray& value) { return QByteArray(value.constData(), value.size()); }

WindowsDownloadNetworkLaunchConfig copiedNetwork(const WindowsDownloadNetworkLaunchConfig& source) {
    WindowsDownloadNetworkLaunchConfig result;
    result.runtime_parent = owned(source.runtime_parent);
    for (std::size_t index = 0; index < source.artifacts.size(); ++index) {
        result.artifacts[index].path = owned(source.artifacts[index].path);
        result.artifacts[index].sha256 = owned(source.artifacts[index].sha256);
    }
    return result;
}
WindowsMediaFoundationLaunchConfig copiedMedia(const WindowsMediaFoundationLaunchConfig& source) {
    auto result = source;
    result.runtime_parent = owned(source.runtime_parent);
    result.source_executable = owned(source.source_executable);
    result.source_sha256 = owned(source.source_sha256);
    return result;
}
WindowsDownloadJobSessionConfig copiedJob(const WindowsDownloadJobSessionConfig& source) {
    auto result = source;
    result.workspace.staging_parent = owned(source.workspace.staging_parent);
    result.workspace.publish_parent = owned(source.workspace.publish_parent);
    result.converter.ffmpeg_executable = owned(source.converter.ffmpeg_executable);
    result.converter.ffprobe_executable = owned(source.converter.ffprobe_executable);
    result.converter.media_foundation_executable = owned(source.converter.media_foundation_executable);
    return result;
}
} // namespace

std::unique_ptr<WindowsDownloadExecution>
makeWindowsConfiguredDownloadExecution(WindowsDownloadExecutionProfile profile,
                                       WindowsDownloadExecutionPreparers preparers) {
    if (profile.job.ownership_store || (profile.youtube && !profile.youtube->epoch_clock) ||
        (profile.provision_media_profile && profile.media.app_container))
        return {};

    WindowsMediaProfilePlatformFactory media_profile;
    if (profile.provision_media_profile) {
        media_profile = std::move(preparers.media_profile);
        if (!media_profile)
            media_profile = [] { return WindowsNativeMediaProfile::system(); };
    }
    // Ordinary Qt value copies may still borrow fromRawData storage. Detach
    // all configuration bytes before returning or starting the worker thread.
    WindowsDownloadExecution::TaskFactory destination =
        [network = copiedNetwork(profile.network), media = copiedMedia(profile.media), job = copiedJob(profile.job),
         journal = owned(profile.journal_directory), network_prepare = std::move(preparers.network),
         media_prepare = std::move(preparers.media), media_profile = std::move(media_profile)] {
            // This factory is invoked only on the existing execution worker.
            // Construction stays inert; start owns journal validation/preparation
            // and the first uncancelled poll owns the session launch.
            return makeWindowsOwnedPreparedDownloadTask(network, media, job, journal, network_prepare, media_prepare,
                                                        media_profile);
        };
    if (!profile.youtube)
        return std::make_unique<WindowsDownloadExecution>(std::move(destination));

    auto& youtube = *profile.youtube;
    // The broker factory already snapshots every runtime/environment value.
    // No prepared capability is shared or cached across jobs.
    auto resolver = makeWindowsYoutubeBrokerResolverFactory(std::move(youtube.runtime), std::move(youtube.environment),
                                                            std::move(preparers.youtube));
    return makeWindowsMediaAndYoutubeExecution(std::move(resolver), std::move(destination),
                                               std::move(youtube.epoch_clock), youtube.limits);
}
} // namespace music_player::m0
