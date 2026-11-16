#include "WindowsConfiguredDownloadExecution.hpp"
#include "WindowsDownloadArtifactPins.hpp"
#include "WindowsDownloadOwnershipRecovery.hpp"
#include "WindowsDownloadOwnershipStore.hpp"
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
    result.workspace.custom_publish_parent = owned(source.workspace.custom_publish_parent);
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
    const auto job = copiedJob(profile.job);
    const auto journal = owned(profile.journal_directory);
    WindowsDownloadExecution::Preflight preflight = [workspace = job.workspace, journal](std::string_view job_id,
                                                                                         const auto& evidence) {
        QString directory;
        if (!download_artifacts::validPath(journal, directory))
            return DownloadFailure::InvalidResult;
        WindowsDownloadOwnershipStore store{std::move(directory)};
        switch (WindowsDownloadOwnershipRecovery::ensureCapacity(workspace, store, evidence, job_id)) {
        case DownloadOwnershipCapacity::Available:
            return DownloadFailure::None;
        case DownloadOwnershipCapacity::Full:
            return DownloadFailure::LimitExceeded;
        case DownloadOwnershipCapacity::HistoryUnavailable:
            return DownloadFailure::InvalidResult;
        case DownloadOwnershipCapacity::ReclaimUnconfirmed:
        case DownloadOwnershipCapacity::PriorAttemptUnresolved:
            return DownloadFailure::Publication;
        }
        return DownloadFailure::InvalidResult;
    };
    WindowsDownloadExecution::TaskFactory destination =
        [network = copiedNetwork(profile.network), media = copiedMedia(profile.media), job, journal,
         publication_directory = std::move(profile.publication_directory),
         network_prepare = std::move(preparers.network), media_prepare = std::move(preparers.media),
         media_profile = std::move(media_profile)] {
            // This factory is invoked only on the existing execution worker.
            // Construction stays inert; start owns journal validation/preparation
            // and the first uncancelled poll owns the session launch.
            auto selected_job = job;
            if (publication_directory)
                selected_job.workspace.custom_publish_parent = publication_directory();
            return makeWindowsOwnedPreparedDownloadTask(network, media, std::move(selected_job), journal,
                                                        network_prepare, media_prepare, media_profile);
        };
    if (!profile.youtube)
        return std::make_unique<WindowsDownloadExecution>(std::move(destination), std::move(preflight),
                                                          profile.parallel_jobs);

    auto& youtube = *profile.youtube;
    // The broker factory already snapshots every runtime/environment value.
    // No prepared capability is shared or cached across jobs.
    auto resolver = makeWindowsYoutubeBrokerResolverFactory(std::move(youtube.runtime), std::move(youtube.environment),
                                                            std::move(preparers.youtube));
    return makeWindowsMediaAndYoutubeExecution(std::move(resolver), std::move(destination),
                                               std::move(youtube.epoch_clock), youtube.limits, std::move(preflight),
                                               profile.parallel_jobs);
}
} // namespace music_player::m0
