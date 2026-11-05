#include "PlayerDownloadsBuildProfile.hpp"

#include "adapters/WindowsDownloadCheckpointStore.hpp"

#ifdef RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER
#include RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER
#endif

#ifdef RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#include RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#endif

#include <algorithm>
#include <array>
#include <utility>

#include <QDateTime>
#include <qt_windows.h>

namespace music_player::m0 {
namespace {
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
QByteArray owned(const QByteArray& value) { return QByteArray(value.constData(), value.size()); }
constexpr qsizetype maximum_profile_path_units = 30'000;

bool reservedComponent(const QString& component) {
    auto stem = component.section(u'.', 0, 0).toUpper();
    while (stem.endsWith(u' '))
        stem.chop(1);
    if (stem == QLatin1String("CON") || stem == QLatin1String("PRN") || stem == QLatin1String("AUX") ||
        stem == QLatin1String("NUL") || stem == QLatin1String("CONIN$") || stem == QLatin1String("CONOUT$"))
        return true;
    if (stem.size() != 4 || (!stem.startsWith(QLatin1String("COM")) && !stem.startsWith(QLatin1String("LPT"))))
        return false;
    const auto suffix = stem.at(3).unicode();
    return (suffix >= '1' && suffix <= '9') || suffix == 0x00b9 || suffix == 0x00b2 || suffix == 0x00b3;
}
bool validComponent(const QString& component, const bool ascii_only) {
    if (component.isEmpty() || component.size() > 255 || component == QLatin1String(".") ||
        component == QLatin1String("..") || component.endsWith(u'.') || component.endsWith(u' ') ||
        reservedComponent(component))
        return false;
    return std::all_of(component.cbegin(), component.cend(), [ascii_only](const QChar unit) {
        return (!ascii_only || unit.unicode() < 128) && unit.category() != QChar::Other_Control &&
               unit.category() != QChar::Separator_Line && unit.category() != QChar::Separator_Paragraph &&
               !QStringLiteral(":<>\"|?*").contains(unit);
    });
}
std::optional<QString> normalizedRoot(const QString& input) {
    if (input.size() < 3 || input.size() > maximum_profile_path_units || input.at(1) != u':' ||
        !((input.at(0) >= u'a' && input.at(0) <= u'z') || (input.at(0) >= u'A' && input.at(0) <= u'Z')) ||
        (input.at(2) != u'/' && input.at(2) != u'\\') || QString::fromUtf8(input.toUtf8()) != input)
        return {};
    auto result = owned(input);
    result.replace(u'\\', u'/');
    if (result.size() > 3 && result.endsWith(u'/'))
        result.chop(1);
    if (result.size() == 3)
        return result;
    const auto components = result.mid(3).split(u'/');
    if (components.size() > 256 || !std::all_of(components.cbegin(), components.cend(),
                                                [](const QString& part) { return validComponent(part, false); }))
        return {};
    return result;
}
std::optional<QString> sourcePath(const QString& application, const QString& relative) {
    if (relative.isEmpty() || relative.size() > maximum_profile_path_units || relative.startsWith(u'/') ||
        relative.startsWith(u'\\'))
        return {};
    auto local = owned(relative);
    local.replace(u'\\', u'/');
    const auto components = local.split(u'/');
    if (components.size() > 256 || !std::all_of(components.cbegin(), components.cend(),
                                                [](const QString& part) { return validComponent(part, true); }))
        return {};
    const auto separator = application.endsWith(u'/') ? QString{} : QStringLiteral("/");
    if (application.size() > maximum_profile_path_units - separator.size() - local.size())
        return {};
    return application + separator + local;
}
bool validPin(const QByteArray& pin) {
    return pin.size() == 64 && std::all_of(pin.cbegin(), pin.cend(), [](const char byte) {
               return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
           });
}
void ownNativeStrings(NativeHostConfiguration& profile) {
    profile.allowed_origin = owned(profile.allowed_origin);
    profile.endpoint_name = owned(profile.endpoint_name);
    for (auto& image : profile.expected_owner_images)
        image = owned(image);
    for (auto& image : profile.expected_host_images)
        image = owned(image);
}
void ownProfileStrings(WindowsPlayerDownloadProfile& profile) {
    profile.checkpoint_directory = owned(profile.checkpoint_directory);
    auto& execution = profile.execution;
    execution.journal_directory = owned(execution.journal_directory);
    execution.network.runtime_parent = owned(execution.network.runtime_parent);
    for (auto& artifact : execution.network.artifacts) {
        artifact.path = owned(artifact.path);
        artifact.sha256 = owned(artifact.sha256);
    }
    execution.media.runtime_parent = owned(execution.media.runtime_parent);
    execution.media.source_executable = owned(execution.media.source_executable);
    execution.media.source_sha256 = owned(execution.media.source_sha256);
    execution.job.workspace.staging_parent = owned(execution.job.workspace.staging_parent);
    execution.job.workspace.publish_parent = owned(execution.job.workspace.publish_parent);
    execution.job.converter.ffmpeg_executable = owned(execution.job.converter.ffmpeg_executable);
    execution.job.converter.ffprobe_executable = owned(execution.job.converter.ffprobe_executable);
    execution.job.converter.media_foundation_executable = owned(execution.job.converter.media_foundation_executable);
    if (execution.youtube) {
        execution.youtube->runtime.runtime_parent = owned(execution.youtube->runtime.runtime_parent);
        for (auto& artifact : execution.youtube->runtime.artifacts) {
            artifact.source_path = owned(artifact.source_path);
            artifact.relative_path = owned(artifact.relative_path);
            artifact.sha256 = owned(artifact.sha256);
        }
    }
    if (profile.options.native_ingress) {
        ownNativeStrings(profile.options.native_ingress->principal);
        ownNativeStrings(profile.options.native_ingress->youtube);
    }
}
void applyStorageLayout(WindowsPlayerDownloadProfile& profile, const QString& config_root) {
    const auto paths = windowsDownloadStoragePaths(config_root);
    profile.checkpoint_directory = paths.state;
    profile.execution.journal_directory = paths.state;
    profile.execution.network.runtime_parent = paths.network;
    profile.execution.media.runtime_parent = paths.media;
    if (profile.execution.youtube)
        profile.execution.youtube->runtime.runtime_parent = paths.youtube;
    profile.execution.job.workspace.staging_parent = paths.staging;
    profile.execution.job.workspace.publish_parent = paths.published;
}
class StoragePreparedCheckpointStore final : public DownloadCheckpointStore {
  public:
    StoragePreparedCheckpointStore(QString root, QString checkpoint, WindowsPlayerDownloadStoragePreparer prepare)
        : root_(std::move(root)), prepare_(std::move(prepare)), backend_(std::move(checkpoint)) {}
    DownloadCheckpointLoadResult load() override {
        if (!prepared_) {
            try {
                if (!prepare_ || !prepare_(root_))
                    return {.status = DownloadCheckpointLoadStatus::Unreadable};
            } catch (...) {
                return {.status = DownloadCheckpointLoadStatus::Unreadable};
            }
            // Remember preparation independently of load's result. Later reads
            // retain the backend's checks instead of repairing changed storage.
            prepared_ = true;
        }
        return backend_.load();
    }
    DownloadCheckpointSaveStatus save(const DownloadCheckpoint& checkpoint) override {
        return prepared_ ? backend_.save(checkpoint) : DownloadCheckpointSaveStatus::Failed;
    }

  private:
    QString root_;
    WindowsPlayerDownloadStoragePreparer prepare_;
    WindowsDownloadCheckpointStore backend_;
    bool prepared_{};
};
bool active(const NativeHostConfiguration& profile) {
    return !profile.allowed_origin.isEmpty() || !profile.endpoint_name.isEmpty();
}
bool sameImageAllowset(const QStringList& left, const QStringList& right) {
    const auto contains = [](const QStringList& images, const QString& expected) {
        return std::any_of(images.cbegin(), images.cend(),
                           [&](const QString& actual) { return actual.compare(expected, Qt::CaseInsensitive) == 0; });
    };
    return std::all_of(left.cbegin(), left.cend(), [&](const QString& image) { return contains(right, image); }) &&
           std::all_of(right.cbegin(), right.cend(), [&](const QString& image) { return contains(left, image); });
}
bool matches(const NativeHostConfiguration& actual, const NativeHostConfiguration& compiled) {
    // A provider may decline a compiled channel. It cannot enable a different
    // identity or broaden either peer's allowed executable image paths.
    return !active(actual) || (active(compiled) && actual.allowed_origin == compiled.allowed_origin &&
                               actual.endpoint_name.compare(compiled.endpoint_name, Qt::CaseInsensitive) == 0 &&
                               sameImageAllowset(actual.expected_owner_images, compiled.expected_owner_images) &&
                               sameImageAllowset(actual.expected_host_images, compiled.expected_host_images));
}
} // namespace

std::optional<WindowsPlayerDownloadProfile> makePackagedPlayerDownloadProfile(WindowsPlayerDownloadProfile profile,
                                                                              const QString& config_root,
                                                                              const QString& application_directory,
                                                                              const QString& windows_directory) {
    const auto application = normalizedRoot(application_directory);
    const auto windows = normalizedRoot(windows_directory);
    if (!application || !windows)
        return {};
    ownProfileStrings(profile);
    for (auto& artifact : profile.execution.network.artifacts) {
        const auto path = sourcePath(*application, artifact.path);
        if (!path || !validPin(artifact.sha256))
            return {};
        artifact.path = *path;
    }
    const auto media = sourcePath(*application, profile.execution.media.source_executable);
    if (!media || !validPin(profile.execution.media.source_sha256))
        return {};
    profile.execution.media.source_executable = *media;
    if (profile.execution.youtube) {
        auto& youtube = *profile.execution.youtube;
        for (auto& artifact : youtube.runtime.artifacts) {
            const auto path = sourcePath(*application, artifact.source_path);
            if (!path || !validPin(artifact.sha256))
                return {};
            artifact.source_path = *path;
        }
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("PATH"), QString{});
        environment.insert(QStringLiteral("SystemRoot"), *windows);
        youtube.environment = std::move(environment);
        youtube.epoch_clock = [] { return QDateTime::currentSecsSinceEpoch(); };
    }
    profile.prepare_storage = true;
    applyStorageLayout(profile, owned(config_root));
    return profile;
}
std::optional<WindowsPlayerDownloadProfile> makePackagedPlayerDownloadProfile(WindowsPlayerDownloadProfile profile,
                                                                              const QString& config_root) {
    const auto root = owned(config_root);
    ownProfileStrings(profile);
    std::array<wchar_t, 32768> image{};
    const auto image_size = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (image_size == 0 || image_size >= image.size())
        return {};
    const auto image_path = QString::fromWCharArray(image.data(), static_cast<qsizetype>(image_size));
    const auto separator = std::max(image_path.lastIndexOf(u'/'), image_path.lastIndexOf(u'\\'));
    if (separator < 2)
        return {};
    const auto application = image_path.first(separator == 2 ? 3 : separator);
    std::array<wchar_t, 32768> windows{};
    const auto windows_size = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (windows_size == 0 || windows_size >= windows.size())
        return {};
    return makePackagedPlayerDownloadProfile(
        std::move(profile), root, application,
        QString::fromWCharArray(windows.data(), static_cast<qsizetype>(windows_size)));
}

SingleWorkerDownloadCheckpointIo::Factory
makePlayerDownloadCheckpointFactory(const WindowsPlayerDownloadProfile& profile, const QString& config_root,
                                    WindowsPlayerDownloadStoragePreparer prepare) {
    const auto root = owned(config_root);
    const auto checkpoint =
        owned(profile.prepare_storage ? windowsDownloadStoragePaths(root).state : profile.checkpoint_directory);
    return [root, checkpoint, initialize = profile.prepare_storage,
            prepare = std::move(prepare)]() -> std::unique_ptr<DownloadCheckpointStore> {
        if (initialize)
            return std::make_unique<StoragePreparedCheckpointStore>(root, checkpoint, prepare);
        return std::make_unique<WindowsDownloadCheckpointStore>(checkpoint);
    };
}

std::optional<WindowsPlayerNativeIngressOptions> compiledPlayerNativeIngressProfiles() {
#ifdef RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER
    auto profiles = nativeHostBuildProfiles();
    if (active(profiles.principal) || active(profiles.youtube))
        return WindowsPlayerNativeIngressOptions{.principal = std::move(profiles.principal),
                                                 .youtube = std::move(profiles.youtube)};
#endif
    return std::nullopt;
}
std::optional<WindowsPlayerDownloadProfile> playerDownloadsBuildProfile(const QString& config_root) {
#ifdef RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
    auto profile = configuredPlayerDownloadsBuildProfile(config_root);
    if (profile && profile->options.native_ingress) {
        const auto& actual = *profile->options.native_ingress;
        if (active(actual.youtube) && !profile->execution.youtube)
            return std::nullopt;
        if (active(actual.principal) || active(actual.youtube)) {
            const auto compiled = compiledPlayerNativeIngressProfiles();
            if (!compiled || !matches(actual.principal, compiled->principal) ||
                !matches(actual.youtube, compiled->youtube))
                return std::nullopt;
        }
    }
    if (profile && profile->prepare_storage) {
        applyStorageLayout(*profile, config_root);
    }
    return profile;
#else
    static_cast<void>(config_root);
    return std::nullopt;
#endif
}
} // namespace music_player::m0
