#include "WindowsYoutubeExecution.hpp"

#include "WindowsYoutubeBrokerProcess.hpp"

#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
WindowsYoutubeRuntimeConfig copiedConfiguration(const WindowsYoutubeRuntimeConfig& source) {
    WindowsYoutubeRuntimeConfig result;
    result.runtime_parent = owned(source.runtime_parent);
    result.artifacts.reserve(source.artifacts.size());
    for (const auto& artifact : source.artifacts)
        result.artifacts.push_back({artifact.role, owned(artifact.relative_path), owned(artifact.source_path),
                                    QByteArray(artifact.sha256.constData(), artifact.sha256.size())});
    return result;
}
QProcessEnvironment copiedEnvironment(const QProcessEnvironment& source) {
    QProcessEnvironment result;
    for (const auto& key : source.keys())
        result.insert(owned(key), owned(source.value(key)));
    return result;
}
} // namespace

YoutubeResolverProcessFactory makeWindowsYoutubeBrokerResolverFactory(WindowsYoutubeRuntimeConfig config,
                                                                      QProcessEnvironment environment,
                                                                      WindowsYoutubeRuntimePreparer prepare) {
    // QString/QByteArray copies can still borrow fromRawData storage. Capture
    // owned bytes now, before the caller can release or mutate that storage.
    auto snapshot = copiedConfiguration(config);
    auto explicit_environment = copiedEnvironment(environment);
    return [config = std::move(snapshot), environment = std::move(explicit_environment),
            prepare = std::move(prepare)]() -> std::unique_ptr<YoutubeResolverProcess> {
        // Constructing a broker remains passive. Neither a prepared lease nor
        // native resources are retained in this reusable outer factory.
        return std::make_unique<WindowsYoutubeBrokerProcess>(
            [config, environment, prepare]() -> std::optional<WindowsYoutubeBrokerPreparedRuntime> {
                // Invoked only by broker start on the executor worker. Every
                // job prepares separately and owns its lease through stop ACK.
                auto files = prepare ? prepare(config) : WindowsYoutubeRuntime::prepare(config);
                if (!files)
                    return {};
                return WindowsYoutubeBrokerPreparedRuntime{std::move(files), environment};
            });
    };
}
} // namespace music_player::m0
