#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/ports/AudioFileLocator.hpp"
#include "m0/ports/AudioFilesPicker.hpp"
#include "m0/ports/AudioOutputSelection.hpp"
#include "m0/ports/LocalSourceDiscovery.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "m0/ports/PlaylistStore.hpp"
#include "presentation/ArtworkImageProvider.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/PlaylistsBinding.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QAccessible>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QStringList>
#include <QTimer>
#include <QTranslator>
#include <QVariantMap>
#include <QWindow>
#include <QtQuickTest/quicktest.h>
#include <QtTest/qtestkeyboard.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
// Stands in for the product's `image://queueart/`: every cover is a transparent 1x1 pixel.
class TransparentQueueArtworkProvider final : public QQuickImageProvider {
  public:
    TransparentQueueArtworkProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        if (size != nullptr)
            *size = image.size();
        return image;
    }
};

class QuickTestPlatform final : public music_player::m0::PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return tray_available; }
    [[nodiscard]] bool defaultAppsSettingsSupported() const noexcept override {
        return default_apps_settings_supported;
    }
    [[nodiscard]] bool defaultAppsSettingsAvailable() const noexcept override {
        return default_apps_settings_available;
    }
    void openDefaultAppsSettings(music_player::m0::DefaultAppsSettingsCompletion completion) override {
        ++default_apps_settings_calls;
        completion(default_apps_settings_result);
    }
    void hideWindow() override { ++hide_calls; }
    void minimizeWindow() override { ++minimize_calls; }
    void showAndActivateWindow() override { ++restore_calls; }
    void requestExit() override { ++exit_calls; }
    bool tray_available{false};
    bool default_apps_settings_supported{true};
    bool default_apps_settings_available{true};
    music_player::m0::DefaultAppsSettingsLaunchResult default_apps_settings_result{
        music_player::m0::DefaultAppsSettingsLaunchResult::Dispatched};
    int default_apps_settings_calls{0};
    int hide_calls{0};
    int minimize_calls{0};
    int restore_calls{0};
    int exit_calls{0};
};

class PendingPreferencesWriter final : public music_player::m0::PreferencesWriter {
  public:
    music_player::m0::PreferencesWriterCommand start(music_player::m0::PreferencesWriteRequest request) override {
        if (active_.has_value())
            return music_player::m0::PreferencesWriterCommand::Busy;
        active_ = std::move(request);
        return music_player::m0::PreferencesWriterCommand::Accepted;
    }
    [[nodiscard]] std::optional<music_player::m0::PreferencesWriteCompletion> poll() override { return std::nullopt; }
    [[nodiscard]] std::optional<music_player::m0::PreferencesWriteCompletion>
    flush(music_player::m0::PreferencesWriteRequest) override {
        return std::nullopt;
    }
    [[nodiscard]] bool shutdown() override { return true; }

  private:
    std::optional<music_player::m0::PreferencesWriteRequest> active_{};
};

class QuickTestPlaybackBackend : public music_player::m0::PlaybackBackend {
  public:
    [[nodiscard]] const music_player::m0::PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(music_player::m0::PlaybackEventCallback callback) override {
        event_callback_ = std::move(callback);
    }
    music_player::m0::PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loaded_paths.push_back(source_path);
        snapshot_ = {.status = music_player::m0::PlaybackStatus::Loading, .generation = ++generation_};
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult play() override {
        ++play_calls;
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult pause() override {
        ++pause_calls;
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult seekTo(std::int64_t target) override {
        ++seek_calls;
        last_seek_target = target;
        return std::exchange(reject_next_seek, false) ? music_player::m0::PlaybackCommandResult::Busy
                                                      : music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult setVolume(double) override {
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult setMuted(bool) override {
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult unload() override {
        ++unload_calls;
        snapshot_ = {.status = music_player::m0::PlaybackStatus::Empty, .generation = ++generation_};
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult shutdown() override {
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    void publish(const music_player::m0::PlaybackStatus status, const bool seekable = true) {
        snapshot_.status = status;
        snapshot_.duration_ms = 180'000;
        snapshot_.seekable = seekable;
        if (event_callback_)
            event_callback_({music_player::m0::PlaybackEventType::SnapshotChanged, snapshot_});
    }
    void resetObservations() {
        loaded_paths.clear();
        play_calls = 0;
        pause_calls = 0;
        unload_calls = 0;
        seek_calls = 0;
        last_seek_target = 0;
        reject_next_seek = false;
    }

    music_player::m0::PlaybackSnapshot snapshot_{};
    music_player::m0::PlaybackEventCallback event_callback_{};
    std::vector<std::filesystem::path> loaded_paths{};
    int play_calls{0};
    int pause_calls{0};
    int unload_calls{0};
    int seek_calls{0};
    std::int64_t last_seek_target{0};
    bool reject_next_seek{false};

  private:
    std::uint64_t generation_{0};
};

class QuickTestSelectablePlaybackBackend final : public QuickTestPlaybackBackend,
                                                 public music_player::m0::AudioOutputSelection {
  public:
    [[nodiscard]] std::vector<music_player::m0::AudioOutputDevice> audioOutputs() const override { return devices_; }
    [[nodiscard]] std::string selectedAudioOutputId() const override { return selected_id_; }
    bool selectAudioOutput(std::string_view id) override {
        ++selection_calls;
        if (std::exchange(reject_next_selection, false) ||
            (!id.empty() && std::ranges::none_of(devices_, [&](const auto& device) { return device.id == id; }))) {
            return false;
        }
        selected_id_ = id;
        if (outputs_changed_)
            outputs_changed_();
        return true;
    }
    void setAudioOutputsChangedCallback(std::function<void()> callback) override {
        outputs_changed_ = std::move(callback);
    }
    void setDevices(std::vector<music_player::m0::AudioOutputDevice> devices) {
        devices_ = std::move(devices);
        if (outputs_changed_)
            outputs_changed_();
    }
    void resetSelection() {
        selection_calls = 0;
        reject_next_selection = false;
        if (!selected_id_.empty() || !devices_.empty()) {
            selected_id_.clear();
            setDevices({});
        }
    }

    int selection_calls{0};
    bool reject_next_selection{false};

  private:
    std::vector<music_player::m0::AudioOutputDevice> devices_{};
    std::string selected_id_{};
    std::function<void()> outputs_changed_{};
};

class QuickTestAudioFileLocator final : public QObject, public music_player::m0::AudioFileLocator {
  public:
    bool locate(const std::filesystem::path& missing_source,
                music_player::m0::AudioFileLocationCompletion completion) override {
        if (completion_)
            return false;
        requests.push_back(missing_source);
        completion_ = std::move(completion);
        return true;
    }
    void cancel() noexcept override {
        ++generation_;
        completion_ = {};
    }
    void complete(music_player::m0::AudioFileLocationResult result) {
        const auto generation = generation_;
        QTimer::singleShot(0, this, [this, generation, result = std::move(result)]() mutable {
            if (generation != generation_)
                return;
            auto completion = std::exchange(completion_, {});
            if (completion)
                completion(std::move(result));
        });
    }
    void reset() {
        cancel();
        requests.clear();
    }

    std::vector<std::filesystem::path> requests;

  private:
    music_player::m0::AudioFileLocationCompletion completion_{};
    std::uint64_t generation_{0};
};

class QuickTestAudioFilesPicker final : public QObject, public music_player::m0::AudioFilesPicker {
  public:
    bool pick(music_player::m0::AudioFilesSelectionCompletion completion) override {
        if (completion_)
            return false;
        ++requests;
        completion_ = std::move(completion);
        if (!deferred) {
            auto paths = selection_reader ? selection_reader() : std::vector<std::filesystem::path>{};
            complete({.status = paths.empty() ? music_player::m0::AudioFilesSelectionStatus::Cancelled
                                              : music_player::m0::AudioFilesSelectionStatus::Selected,
                      .source_paths = std::move(paths)});
        }
        return true;
    }
    void cancel() noexcept override {
        ++generation_;
        completion_ = {};
    }
    void complete(music_player::m0::AudioFilesSelectionResult result) {
        const auto generation = generation_;
        QTimer::singleShot(0, this, [this, generation, result = std::move(result)]() mutable {
            if (generation != generation_)
                return;
            auto completion = std::exchange(completion_, {});
            if (completion)
                completion(std::move(result));
        });
    }
    void reset() {
        cancel();
        requests = 0;
        deferred = false;
    }

    std::function<std::vector<std::filesystem::path>()> selection_reader;
    int requests{0};
    bool deferred{false};

  private:
    music_player::m0::AudioFilesSelectionCompletion completion_{};
    std::uint64_t generation_{0};
};

class QuickTestMetadataReader final : public music_player::m0::MetadataReader {
  public:
    music_player::m0::MetadataCommandResult startRead(music_player::m0::MetadataReadRequest request) override {
        requests.push_back(std::move(request));
        return music_player::m0::MetadataCommandResult::Accepted;
    }
    music_player::m0::MetadataCommandResult cancelRead(music_player::m0::MetadataGeneration) override {
        return music_player::m0::MetadataCommandResult::Accepted;
    }
    [[nodiscard]] music_player::m0::MetadataPollResult poll(music_player::m0::MetadataGeneration) override {
        if (poll_results.empty())
            return {music_player::m0::MetadataPollStatus::Pending, std::nullopt};
        auto result = std::move(poll_results.front());
        poll_results.pop_front();
        return result;
    }
    [[nodiscard]] std::optional<music_player::m0::MetadataReadCompletion> tryTakeCompletion() override {
        return std::nullopt;
    }
    music_player::m0::MetadataCommandResult shutdown() override {
        return music_player::m0::MetadataCommandResult::Accepted;
    }

    void reset() {
        requests.clear();
        poll_results.clear();
    }

    std::vector<music_player::m0::MetadataReadRequest> requests{};
    std::deque<music_player::m0::MetadataPollResult> poll_results{};
};

class FakeLocalSourceDiscovery final : public music_player::m0::LocalSourceDiscovery {
  public:
    music_player::m0::DiscoveryCommandResult start(music_player::m0::LocalSourceDiscoveryRequest request) override {
        requests.push_back(std::move(request));
        next_sequence_ = 0;
        return music_player::m0::DiscoveryCommandResult::Accepted;
    }

    music_player::m0::DiscoveryCommandResult cancel(const music_player::m0::DiscoveryGeneration generation) override {
        cancelled_generations.push_back(generation);
        return music_player::m0::DiscoveryCommandResult::Accepted;
    }

    [[nodiscard]] music_player::m0::DiscoveryPollResult
    poll(const music_player::m0::DiscoveryGeneration generation) override {
        polled_generations.push_back(generation);
        if (poll_results.empty())
            return {.status = music_player::m0::DiscoveryPollStatus::Pending};
        auto result = std::move(poll_results.front());
        poll_results.pop_front();
        return result;
    }

    music_player::m0::DiscoveryCommandResult shutdown() override {
        ++shutdown_calls;
        return music_player::m0::DiscoveryCommandResult::Accepted;
    }

    [[nodiscard]] bool hasRequest() const noexcept { return !requests.empty(); }

    void publishBatch(std::vector<std::filesystem::path> accepted_paths, const std::uint64_t issue_count,
                      const std::uint64_t inspected_count) {
        if (!hasRequest())
            return;
        std::vector<music_player::m0::LocalSourceDiscoveryIssue> issues;
        issues.reserve(static_cast<std::size_t>(issue_count));
        for (std::uint64_t index = 0; index < issue_count; ++index) {
            issues.push_back(
                {music_player::m0::LocalSourceDiscoveryIssueCode::UnsupportedFile,
                 std::filesystem::path{"unsupported"} / std::filesystem::path{std::to_string(index) + ".txt"}});
        }
        poll_results.push_back(
            {.status = music_player::m0::DiscoveryPollStatus::BatchReady,
             .batch = music_player::m0::LocalSourceDiscoveryBatch{.generation = requests.back().generation,
                                                                  .sequence = ++next_sequence_,
                                                                  .inspected_count = inspected_count,
                                                                  .accepted_paths = std::move(accepted_paths),
                                                                  .issues = std::move(issues)}});
    }

    void publishCompleted(const std::uint64_t inspected_count, const std::uint64_t accepted_count,
                          const std::uint64_t issue_count) {
        poll_results.push_back(
            {.status = music_player::m0::DiscoveryPollStatus::Completed,
             .summary = music_player::m0::LocalSourceDiscoverySummary{
                 .inspected_count = inspected_count, .accepted_count = accepted_count, .issue_count = issue_count}});
    }

    void publishCancelled() { poll_results.push_back({.status = music_player::m0::DiscoveryPollStatus::Cancelled}); }

    void reset() {
        requests.clear();
        cancelled_generations.clear();
        polled_generations.clear();
        poll_results.clear();
        next_sequence_ = 0;
        shutdown_calls = 0;
    }

    std::vector<music_player::m0::LocalSourceDiscoveryRequest> requests{};
    std::vector<music_player::m0::DiscoveryGeneration> cancelled_generations{};
    std::vector<music_player::m0::DiscoveryGeneration> polled_generations{};
    std::deque<music_player::m0::DiscoveryPollResult> poll_results{};
    int shutdown_calls{0};

  private:
    std::uint64_t next_sequence_{0};
};

QAccessible::UpdateHandler previous_accessibility_update_handler = nullptr;
QString last_accessible_announcement;
int accessible_announcement_count = 0;
int last_accessible_announcement_politeness = -1;

void recordAccessibilityUpdate(QAccessibleEvent* event) {
    if (event != nullptr && event->type() == QAccessible::Announcement) {
        const auto* announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        last_accessible_announcement = announcement->message();
        last_accessible_announcement_politeness = static_cast<int>(announcement->politeness());
        ++accessible_announcement_count;
    }
    if (previous_accessibility_update_handler != nullptr)
        previous_accessibility_update_handler(event);
}

[[nodiscard]] std::filesystem::path localPath(const QString& source) {
#ifdef _WIN32
    return std::filesystem::path{source.toStdWString()};
#else
    const QByteArray utf8 = source.toUtf8();
    return std::filesystem::path{std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())}};
#endif
}

[[nodiscard]] QString displayPath(const std::filesystem::path& source) {
#ifdef _WIN32
    return QString::fromStdWString(source.native());
#else
    const auto& native = source.native();
    return QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
#endif
}

class ScenarioDriver final : public QObject {
    Q_OBJECT
  public:
    ScenarioDriver(music_player::m0::PlayerPresenter& presenter, QuickTestPlatform& platform,
                   music_player::m0::QtSystemAppearance& appearance,
                   std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state,
                   std::deque<std::vector<std::filesystem::path>>& local_selections,
                   music_player::m0::PlayerPresenter& local_presenter, QuickTestPlaybackBackend& local_backend,
                   QuickTestMetadataReader& local_metadata_reader, FakeLocalSourceDiscovery& local_source_discovery,
                   QuickTestAudioFileLocator& local_file_locator, QuickTestAudioFilesPicker& local_files_picker,
                   QObject* parent = nullptr)
        : QObject(parent), presenter_(presenter), platform_(platform), appearance_(appearance),
          appearance_state_(std::move(appearance_state)), local_selections_(local_selections),
          local_presenter_(local_presenter), local_backend_(local_backend),
          local_output_backend_(dynamic_cast<QuickTestSelectablePlaybackBackend*>(&local_backend)),
          local_metadata_reader_(local_metadata_reader), local_source_discovery_(local_source_discovery),
          local_file_locator_(local_file_locator), local_files_picker_(local_files_picker) {}
    Q_INVOKABLE QObject* createDownloadsScenario();
    Q_INVOKABLE bool releaseDownloadsScenario(QObject* scenario);
    Q_INVOKABLE QObject* createPlaylistsScenario();
    Q_INVOKABLE bool releasePlaylistsScenario(QObject* scenario);
    Q_INVOKABLE void setTrayAvailable(const bool available) {
        platform_.tray_available = available;
        presenter_.refreshPlatformAvailability();
    }
    Q_INVOKABLE void setDefaultAppsSettingsAvailable(const bool available) {
        platform_.default_apps_settings_available = available;
        presenter_.refreshPlatformAvailability();
    }
    Q_INVOKABLE void setDefaultAppsSettingsSupported(const bool supported) {
        platform_.default_apps_settings_supported = supported;
        presenter_.refreshPlatformAvailability();
    }
    Q_INVOKABLE void setDefaultAppsSettingsResult(const int result) {
        if (result < static_cast<int>(music_player::m0::DefaultAppsSettingsLaunchResult::Dispatched) ||
            result > static_cast<int>(music_player::m0::DefaultAppsSettingsLaunchResult::Failed)) {
            return;
        }
        platform_.default_apps_settings_result = static_cast<music_player::m0::DefaultAppsSettingsLaunchResult>(result);
    }
    Q_INVOKABLE void resetDefaultAppsSettings() {
        platform_.default_apps_settings_supported = true;
        platform_.default_apps_settings_available = true;
        platform_.default_apps_settings_result = music_player::m0::DefaultAppsSettingsLaunchResult::Dispatched;
        platform_.default_apps_settings_calls = 0;
        presenter_.refreshPlatformAvailability();
    }
    Q_INVOKABLE int defaultAppsSettingsCalls() const noexcept { return platform_.default_apps_settings_calls; }
    Q_INVOKABLE int platformHideCalls() const noexcept { return platform_.hide_calls; }
    Q_INVOKABLE int platformMinimizeCalls() const noexcept { return platform_.minimize_calls; }
    Q_INVOKABLE int platformRestoreCalls() const noexcept { return platform_.restore_calls; }
    Q_INVOKABLE int platformExitCalls() const noexcept { return platform_.exit_calls; }
    Q_INVOKABLE bool enterUnavailable() {
        return music_player::m0::experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter_);
    }
    Q_INVOKABLE qulonglong seekCommitAttemptCount() const noexcept {
        return static_cast<qulonglong>(
            music_player::m0::test::PlayerPresenterTestAccess::seekCommitAttemptCount(presenter_));
    }
    Q_INVOKABLE void rejectNextFixtureSeek() noexcept {
        music_player::m0::test::PlayerPresenterTestAccess::rejectNextSeek(presenter_);
    }
    Q_INVOKABLE void enqueueLocalSelection(const QStringList& sources) {
        std::vector<std::filesystem::path> selection;
        selection.reserve(static_cast<std::size_t>(sources.size()));
        for (const QString& source : sources)
            selection.push_back(localPath(source));
        local_selections_.push_back(std::move(selection));
    }
    Q_INVOKABLE int localLoadCount() const noexcept { return static_cast<int>(local_backend_.loaded_paths.size()); }
    Q_INVOKABLE QString localLoadedPath(const int index) const {
        if (index < 0 || index >= static_cast<int>(local_backend_.loaded_paths.size()))
            return {};
        return displayPath(local_backend_.loaded_paths[static_cast<std::size_t>(index)]);
    }
    Q_INVOKABLE int localUnloadCount() const noexcept { return local_backend_.unload_calls; }
    Q_INVOKABLE int localPlayCount() const noexcept { return local_backend_.play_calls; }
    Q_INVOKABLE int localPauseCount() const noexcept { return local_backend_.pause_calls; }
    Q_INVOKABLE QString artifactPath(const QString& name) const {
        const QString root = qEnvironmentVariable("RC_MUSIC_UI_ARTIFACT_DIR");
        if (root.isEmpty() || QFileInfo{name}.fileName() != name)
            return {};
        return QDir{root}.filePath(name);
    }
    Q_INVOKABLE void deferLocalAudioSelection(const bool deferred) { local_files_picker_.deferred = deferred; }
    Q_INVOKABLE int localAudioSelectionCount() const noexcept { return local_files_picker_.requests; }
    Q_INVOKABLE void rebindLocalAudioPicker() {
        local_presenter_.setAudioFilesPicker(nullptr);
        local_presenter_.setAudioFilesPicker(&local_files_picker_);
    }
    Q_INVOKABLE void completeLocalAudioSelection(const QStringList& sources) {
        std::vector<std::filesystem::path> paths;
        paths.reserve(static_cast<std::size_t>(sources.size()));
        for (const QString& source : sources)
            paths.push_back(localPath(source));
        local_files_picker_.complete(
            {.status = music_player::m0::AudioFilesSelectionStatus::Selected, .source_paths = std::move(paths)});
    }
    Q_INVOKABLE void cancelLocalAudioSelection() {
        local_files_picker_.complete({.status = music_player::m0::AudioFilesSelectionStatus::Cancelled});
    }
    Q_INVOKABLE void failLocalAudioSelection() {
        local_files_picker_.complete({.status = music_player::m0::AudioFilesSelectionStatus::Failed});
    }
    Q_INVOKABLE int localLocateCount() const noexcept { return static_cast<int>(local_file_locator_.requests.size()); }
    Q_INVOKABLE QString localLocateSource(const int index) const {
        if (index < 0 || index >= static_cast<int>(local_file_locator_.requests.size()))
            return {};
        return displayPath(local_file_locator_.requests[static_cast<std::size_t>(index)]);
    }
    Q_INVOKABLE void completeLocalLocation(const QString& source) {
        local_file_locator_.complete(
            {.status = music_player::m0::AudioFileLocationStatus::Selected, .source_path = localPath(source)});
    }
    Q_INVOKABLE void cancelLocalLocation() {
        local_file_locator_.complete({.status = music_player::m0::AudioFileLocationStatus::Cancelled});
    }
    Q_INVOKABLE void failLocalLocation() {
        local_file_locator_.complete({.status = music_player::m0::AudioFileLocationStatus::Failed});
    }
    Q_INVOKABLE void publishLocalReady() { local_backend_.publish(music_player::m0::PlaybackStatus::Ready); }
    Q_INVOKABLE void publishLocalPlaying() { local_backend_.publish(music_player::m0::PlaybackStatus::Playing); }
    Q_INVOKABLE void publishLocalPaused() { local_backend_.publish(music_player::m0::PlaybackStatus::Paused); }
    Q_INVOKABLE void rejectNextLocalSeek() { local_backend_.reject_next_seek = true; }
    Q_INVOKABLE int localSeekCount() const noexcept { return local_backend_.seek_calls; }
    Q_INVOKABLE qlonglong localSeekTarget() const noexcept { return local_backend_.last_seek_target; }
    Q_INVOKABLE void publishLocalPosition(const qlonglong position, const qlonglong duration = 180000) {
        local_backend_.snapshot_.position_ms = position;
        local_backend_.snapshot_.duration_ms = duration;
        if (local_backend_.event_callback_)
            local_backend_.event_callback_(
                {music_player::m0::PlaybackEventType::SnapshotChanged, local_backend_.snapshot_});
    }
    Q_INVOKABLE void publishLocalUnseekablePlaying() {
        local_backend_.publish(music_player::m0::PlaybackStatus::Playing, false);
    }
    Q_INVOKABLE void publishLocalError() {
        local_backend_.snapshot_.error = music_player::m0::PlaybackErrorCode::InvalidMedia;
        local_backend_.publish(music_player::m0::PlaybackStatus::Error);
    }
    Q_INVOKABLE void publishLocalSourceUnavailable() {
        local_backend_.snapshot_.error = music_player::m0::PlaybackErrorCode::SourceUnavailable;
        local_backend_.publish(music_player::m0::PlaybackStatus::Error);
    }
    Q_INVOKABLE void publishLocalOutputError(const bool changed) {
        local_backend_.snapshot_.error = changed ? music_player::m0::PlaybackErrorCode::AudioOutputChanged
                                                 : music_player::m0::PlaybackErrorCode::AudioOutputUnavailable;
        local_backend_.snapshot_.position_ms = 47'000;
        local_backend_.publish(music_player::m0::PlaybackStatus::Error);
    }
    Q_INVOKABLE bool setLocalAudioOutputs(const QStringList& ids, const QStringList& labels) {
        if (!local_output_backend_ || ids.size() != labels.size())
            return false;
        std::vector<music_player::m0::AudioOutputDevice> devices;
        devices.reserve(static_cast<std::size_t>(ids.size()));
        for (qsizetype index = 0; index < ids.size(); ++index)
            devices.push_back({ids[index].toStdString(), labels[index].toStdString()});
        local_output_backend_->setDevices(std::move(devices));
        return true;
    }
    Q_INVOKABLE int localAudioOutputSelectionCount() const noexcept {
        return local_output_backend_ ? local_output_backend_->selection_calls : 0;
    }
    Q_INVOKABLE void rejectNextLocalAudioOutputSelection() {
        if (local_output_backend_)
            local_output_backend_->reject_next_selection = true;
    }
    Q_INVOKABLE void publishCompleteLocalMetadata() { publishLocalMetadata(false); }
    Q_INVOKABLE void publishArtworkLocalMetadata() { publishLocalMetadata(true); }
    // The queue rows read `<base><occurrence value>`; the test provider answers with a transparent pixel.
    Q_INVOKABLE void setLocalQueueArtworkBase(const QString& base) { local_presenter_.setQueueArtwork({}, base); }
    // The metadata results published but not yet read by the presenter.
    Q_INVOKABLE int localMetadataPollsPending() const noexcept {
        return static_cast<int>(local_metadata_reader_.poll_results.size());
    }
    // Tags with only the given fields: an empty artist or album stays absent.
    // A positive duration also fills the audio properties, which is how the queue rows learn their length.
    Q_INVOKABLE void publishNamedLocalMetadata(const QString& title, const QString& artist, const QString& album,
                                               const qlonglong duration_ms = 0) {
        if (local_metadata_reader_.requests.empty())
            return;
        const auto& request = local_metadata_reader_.requests.back();
        music_player::m0::TrackMetadata track{
            .source_path = request.source_path,
            .tags = {.title = std::string{title.toStdString()}},
            .display_title = title.toStdString(),
        };
        if (duration_ms > 0)
            track.audio.duration_ms = static_cast<std::uint64_t>(duration_ms);
        if (!artist.isEmpty())
            track.tags.artist = artist.toStdString();
        if (!album.isEmpty())
            track.tags.album = album.toStdString();
        local_metadata_reader_.poll_results.push_back(
            {music_player::m0::MetadataPollStatus::Completed,
             music_player::m0::MetadataReadCompletion{request.generation, request.source_path, std::move(track)}});
    }
    Q_INVOKABLE void publishSanitizedLocalMetadata(const bool valid_numeric_fallback) {
        if (local_metadata_reader_.requests.empty())
            return;
        const auto& request = local_metadata_reader_.requests.back();
        music_player::m0::TrackMetadata track{
            .source_path = request.source_path,
            .tags = {.title = std::string{"<b>Título ajustado 日本語</b>"},
                     .artist = std::string{"Artista ☕"},
                     .album = std::string{"Álbum"},
                     .track_number = valid_numeric_fallback ? std::optional<std::uint32_t>{7} : std::nullopt,
                     .year = valid_numeric_fallback ? std::optional<std::uint32_t>{2026} : std::nullopt},
            .display_title = "<b>Título ajustado 日本語</b>",
            .diagnostics = {{.code = music_player::m0::MetadataDiagnosticCode::TextSanitized,
                             .field = music_player::m0::MetadataField::Title},
                            {.code = music_player::m0::MetadataDiagnosticCode::InvalidValue,
                             .field = music_player::m0::MetadataField::TrackNumber},
                            {.code = music_player::m0::MetadataDiagnosticCode::InvalidValue,
                             .field = music_player::m0::MetadataField::Year}},
        };
        local_metadata_reader_.poll_results.push_back(
            {music_player::m0::MetadataPollStatus::Completed,
             music_player::m0::MetadataReadCompletion{request.generation, request.source_path, std::move(track)}});
    }
    Q_INVOKABLE void publishExactLargeLocalMetadata() {
        if (local_metadata_reader_.requests.empty())
            return;
        constexpr std::uint64_t beyond_javascript_safe_integer = 9'007'199'254'740'993ULL;
        const auto diagnostic_bytes = static_cast<std::size_t>(beyond_javascript_safe_integer);
        const auto& request = local_metadata_reader_.requests.back();
        music_player::m0::TrackMetadata track{
            .source_path = request.source_path,
            .file_size_bytes = beyond_javascript_safe_integer,
            .display_title = "Contagens exatas",
            .diagnostics = {{.code = music_player::m0::MetadataDiagnosticCode::FieldTooLarge,
                             .field = music_player::m0::MetadataField::Title,
                             .observed_bytes = diagnostic_bytes,
                             .limit_bytes = diagnostic_bytes - 1}},
            .artwork_diagnostics = {{.code = music_player::m0::ArtworkDiagnosticCode::EncodedBytesExceeded,
                                     .encoded_bytes = diagnostic_bytes - 2}},
        };
        local_metadata_reader_.poll_results.push_back(
            {music_player::m0::MetadataPollStatus::Completed,
             music_player::m0::MetadataReadCompletion{request.generation, request.source_path, std::move(track)}});
    }
    Q_INVOKABLE void publishFailedLocalMetadata() {
        if (local_metadata_reader_.requests.empty())
            return;
        const auto& request = local_metadata_reader_.requests.back();
        local_metadata_reader_.poll_results.push_back(
            {music_player::m0::MetadataPollStatus::Completed,
             music_player::m0::MetadataReadCompletion{
                 request.generation, request.source_path,
                 music_player::m0::MetadataReadError{music_player::m0::MetadataReadErrorCode::InvalidData,
                                                     "DO NOT EXPOSE: C:\\private\\reader-secret.flac"}}});
    }
    Q_INVOKABLE QString clipboardText() const { return QGuiApplication::clipboard()->text(); }
    // S13.1: a guest following a room: its local queue put away, the room's item shown, the follower's playback
    // on the shared (silent) player.
    Q_INVOKABLE bool followRoom(const QString& title, const QString& artist, const QString& album,
                                const qlonglong duration) {
        if (!local_presenter_.restoreRoomSession(music_player::m0::SessionSnapshot{}))
            return false;
        local_presenter_.showRoomPlayback(music_player::m0::RoomTrackSummary{title.toStdString(), artist.toStdString(),
                                                                             album.toStdString(), duration});
        return true;
    }
    Q_INVOKABLE void publishRoomPlayback(const bool playing, const qlonglong position, const qlonglong duration) {
        local_backend_.snapshot_ = {.status = playing ? music_player::m0::PlaybackStatus::Playing
                                                      : music_player::m0::PlaybackStatus::Paused,
                                    .position_ms = position,
                                    .duration_ms = duration,
                                    .seekable = true,
                                    .generation = local_backend_.snapshot_.generation};
        if (local_backend_.event_callback_)
            local_backend_.event_callback_(
                {music_player::m0::PlaybackEventType::SnapshotChanged, local_backend_.snapshot_});
    }
    Q_INVOKABLE void stopFollowingRoom() { local_presenter_.showRoomPlayback(std::nullopt); }
    Q_INVOKABLE void resetLocalScenario() {
        local_presenter_.showRoomPlayback(std::nullopt);
        local_presenter_.setAudioFilesPicker(nullptr);
        local_files_picker_.reset();
        local_presenter_.setAudioFilesPicker(&local_files_picker_);
        local_file_locator_.reset();
        if (local_presenter_.sourceImportActive()) {
            static_cast<void>(local_presenter_.cancelSourceImport());
            local_source_discovery_.poll_results.clear();
            local_source_discovery_.publishCancelled();
            music_player::m0::test::PlayerPresenterTestAccess::pollSourceImport(local_presenter_);
        }
        if (local_presenter_.sourceImportState() != music_player::m0::PlayerPresenter::SourceImportState::Idle)
            static_cast<void>(local_presenter_.dismissSourceImportStatus());
        if (local_presenter_.queueLoaded())
            static_cast<void>(local_presenter_.clearQueue());
        local_selections_.clear();
        local_source_discovery_.reset();
        local_metadata_reader_.reset();
        local_backend_.resetObservations();
        if (local_output_backend_)
            local_output_backend_->resetSelection();
        drag_mime_.reset();
        clearAccessibleAnnouncements();
    }
    Q_INVOKABLE int sourceRequestCount() const noexcept {
        return static_cast<int>(local_source_discovery_.requests.size());
    }
    Q_INVOKABLE qulonglong sourceRequestGeneration(const int request_index) const noexcept {
        if (request_index < 0 || request_index >= sourceRequestCount())
            return 0;
        return static_cast<qulonglong>(
            local_source_discovery_.requests[static_cast<std::size_t>(request_index)].generation.value);
    }
    Q_INVOKABLE int sourceRequestRootCount(const int request_index) const noexcept {
        if (request_index < 0 || request_index >= sourceRequestCount())
            return 0;
        return static_cast<int>(local_source_discovery_.requests[static_cast<std::size_t>(request_index)].roots.size());
    }
    Q_INVOKABLE QString sourceRequestRoot(const int request_index, const int root_index) const {
        if (request_index < 0 || request_index >= sourceRequestCount())
            return {};
        const auto& roots = local_source_discovery_.requests[static_cast<std::size_t>(request_index)].roots;
        if (root_index < 0 || root_index >= static_cast<int>(roots.size()))
            return {};
        return displayPath(roots[static_cast<std::size_t>(root_index)]);
    }
    Q_INVOKABLE int sourceCancelCount() const noexcept {
        return static_cast<int>(local_source_discovery_.cancelled_generations.size());
    }
    Q_INVOKABLE qulonglong sourceCancelledGeneration(const int cancel_index) const noexcept {
        if (cancel_index < 0 || cancel_index >= sourceCancelCount())
            return 0;
        return static_cast<qulonglong>(
            local_source_discovery_.cancelled_generations[static_cast<std::size_t>(cancel_index)].value);
    }
    Q_INVOKABLE void enqueueSourceBatch(const QStringList& accepted_sources, const qulonglong issue_count,
                                        const qulonglong inspected_count) {
        std::vector<std::filesystem::path> paths;
        paths.reserve(static_cast<std::size_t>(accepted_sources.size()));
        for (const QString& source : accepted_sources)
            paths.push_back(localPath(source));
        local_source_discovery_.publishBatch(std::move(paths), static_cast<std::uint64_t>(issue_count),
                                             static_cast<std::uint64_t>(inspected_count));
    }
    Q_INVOKABLE void enqueueSourceCompletion(const qulonglong inspected_count, const qulonglong accepted_count,
                                             const qulonglong issue_count) {
        local_source_discovery_.publishCompleted(static_cast<std::uint64_t>(inspected_count),
                                                 static_cast<std::uint64_t>(accepted_count),
                                                 static_cast<std::uint64_t>(issue_count));
    }
    Q_INVOKABLE void enqueueSourceCancelled() { local_source_discovery_.publishCancelled(); }
    Q_INVOKABLE void pollSourceImport() {
        music_player::m0::test::PlayerPresenterTestAccess::pollSourceImport(local_presenter_);
    }
    Q_INVOKABLE int beginUrlDrag(QObject* target, const qreal x, const qreal y, const QStringList& sources) {
        drag_mime_ = std::make_unique<QMimeData>();
        QList<QUrl> urls;
        urls.reserve(sources.size());
        for (const QString& source : sources)
            urls.push_back(sourceToUrl(source));
        drag_mime_->setUrls(urls);
        QDragEnterEvent event{QPointF{x, y}.toPoint(), Qt::CopyAction, drag_mime_.get(), Qt::LeftButton,
                              Qt::NoModifier};
        return sendDropEvent(target, event);
    }
    Q_INVOKABLE int beginTextDrag(QObject* target, const qreal x, const qreal y, const QString& text) {
        drag_mime_ = std::make_unique<QMimeData>();
        drag_mime_->setText(text);
        QDragEnterEvent event{QPointF{x, y}.toPoint(), Qt::CopyAction, drag_mime_.get(), Qt::LeftButton,
                              Qt::NoModifier};
        return sendDropEvent(target, event);
    }
    Q_INVOKABLE int moveDrag(QObject* target, const qreal x, const qreal y) {
        if (!drag_mime_)
            return static_cast<int>(Qt::IgnoreAction);
        QDragMoveEvent event{QPointF{x, y}.toPoint(), Qt::CopyAction, drag_mime_.get(), Qt::LeftButton, Qt::NoModifier};
        return sendDropEvent(target, event);
    }
    Q_INVOKABLE int dropDrag(QObject* target, const qreal x, const qreal y) {
        if (!drag_mime_)
            return static_cast<int>(Qt::IgnoreAction);
        QDropEvent event{QPointF{x, y}, Qt::CopyAction, drag_mime_.get(), Qt::LeftButton, Qt::NoModifier};
        const int action = sendDropEvent(target, event);
        drag_mime_.reset();
        return action;
    }
    Q_INVOKABLE void cancelDrag(QObject* target) {
        if (auto* const window = qobject_cast<QWindow*>(target); window != nullptr) {
            QDragLeaveEvent event;
            QCoreApplication::sendEvent(window, &event);
        }
        drag_mime_.reset();
    }
    Q_INVOKABLE void clearAccessibleAnnouncements() {
        last_accessible_announcement.clear();
        accessible_announcement_count = 0;
        last_accessible_announcement_politeness = -1;
    }
    Q_INVOKABLE QString lastAccessibleAnnouncement() const { return last_accessible_announcement; }
    Q_INVOKABLE int accessibleAnnouncementCount() const noexcept { return accessible_announcement_count; }
    Q_INVOKABLE int lastAccessibleAnnouncementPoliteness() const noexcept {
        return last_accessible_announcement_politeness;
    }
    Q_INVOKABLE bool accessibleSetValue(QObject* object, const double value) const {
        QAccessibleInterface* const interface = QAccessible::queryAccessibleInterface(object);
        if (interface == nullptr || !interface->isValid() || interface->valueInterface() == nullptr)
            return false;
        interface->valueInterface()->setCurrentValue(value);
        return true;
    }
    Q_INVOKABLE bool accessibleAdjust(QObject* object, const bool increase) const {
        QAccessibleInterface* const interface = QAccessible::queryAccessibleInterface(object);
        if (interface == nullptr || !interface->isValid())
            return false;
        QAccessibleActionInterface* const actions = interface->actionInterface();
        const QString action =
            increase ? QAccessibleActionInterface::increaseAction() : QAccessibleActionInterface::decreaseAction();
        if (actions == nullptr || !actions->actionNames().contains(action))
            return false;
        actions->doAction(action);
        return true;
    }
    Q_INVOKABLE QVariantMap accessibleSnapshot(QObject* object) const {
        QVariantMap snapshot{{QStringLiteral("valid"), false},     {QStringLiteral("parentValid"), false},
                             {QStringLiteral("role"), -1},         {QStringLiteral("parentRole"), -1},
                             {QStringLiteral("name"), QString{}},  {QStringLiteral("description"), QString{}},
                             {QStringLiteral("focusable"), false}, {QStringLiteral("focused"), false},
                             {QStringLiteral("disabled"), false},  {QStringLiteral("childCount"), 0}};
        QAccessibleInterface* const interface = QAccessible::queryAccessibleInterface(object);
        if (interface == nullptr || !interface->isValid())
            return snapshot;

        const QAccessible::State state = interface->state();
        snapshot.insert(QStringLiteral("valid"), true);
        snapshot.insert(QStringLiteral("role"), static_cast<int>(interface->role()));
        snapshot.insert(QStringLiteral("name"), interface->text(QAccessible::Name));
        snapshot.insert(QStringLiteral("description"), interface->text(QAccessible::Description));
        snapshot.insert(QStringLiteral("focusable"), state.focusable != 0);
        snapshot.insert(QStringLiteral("focused"), state.focused != 0);
        snapshot.insert(QStringLiteral("disabled"), state.disabled != 0);
        snapshot.insert(QStringLiteral("childCount"), interface->childCount());

        QAccessibleInterface* const parent = interface->parent();
        if (parent != nullptr && parent->isValid()) {
            snapshot.insert(QStringLiteral("parentValid"), true);
            snapshot.insert(QStringLiteral("parentRole"), static_cast<int>(parent->role()));
        }
        return snapshot;
    }
    Q_INVOKABLE bool repeatKey(const int key, const int modifiers) {
        QPointer<QWindow> window = QGuiApplication::focusWindow();
        if (window == nullptr)
            return false;
        QTest::simulateEvent(window, true, key, Qt::KeyboardModifiers{modifiers}, {}, true, 1);
        if (window != nullptr)
            QTest::simulateEvent(window, false, key, Qt::KeyboardModifiers{modifiers}, {}, true, 1);
        return true;
    }
    Q_INVOKABLE void setSystemAppearance(const bool dark, const bool reduced_motion) {
        appearance_state_->dark = dark;
        appearance_state_->reduced_motion = reduced_motion;
        appearance_.refresh();
    }
    Q_INVOKABLE void setHighContrast(const bool enabled, const bool light) {
        if (!enabled) {
            appearance_state_->high_contrast.reset();
        } else if (light) {
            appearance_state_->high_contrast = music_player::m0::HighContrastPalette{
                QColor{QStringLiteral("#FCFCFD")}, QColor{QStringLiteral("#111115")}, QColor{QStringLiteral("#1A217A")},
                QColor{QStringLiteral("#FCFCFD")}, QColor{QStringLiteral("#5A5A62")}};
        } else {
            appearance_state_->high_contrast = music_player::m0::HighContrastPalette{
                QColor{QStringLiteral("#010102")}, QColor{QStringLiteral("#FAFAFA")}, QColor{QStringLiteral("#F8F800")},
                QColor{QStringLiteral("#010101")}, QColor{QStringLiteral("#BBBBBB")}};
        }
        appearance_.refresh();
    }

  private:
    void publishLocalMetadata(const bool with_artwork) {
        if (local_metadata_reader_.requests.empty())
            return;
        const auto& request = local_metadata_reader_.requests.back();
        music_player::m0::TrackMetadata track{
            .source_path = request.source_path,
            .file_size_bytes = 4'096,
            .tags = {.title = std::string{"Título controlado"},
                     .artist = std::string{"Artista"},
                     .album = std::string{"Álbum"},
                     .track_number = 7,
                     .year = 2026},
            .display_title = "Título controlado",
            .audio = {.duration_ms = 123'456,
                      .sample_rate_hz = 48'000,
                      .channels = 2,
                      .bit_depth = 24,
                      .bitrate_kbps = 320,
                      .container = "MP3",
                      .codec = "MPEG-1 Audio Layer III"},
            .diagnostics = {{.code = music_player::m0::MetadataDiagnosticCode::FieldTooLarge,
                             .field = music_player::m0::MetadataField::Title,
                             .observed_bytes = 16'385,
                             .limit_bytes = 16'384}},
            .artwork_diagnostics = {{.code = music_player::m0::ArtworkDiagnosticCode::AxisPixelsExceeded,
                                     .encoded_bytes = 1'024,
                                     .width_pixels = 4'097,
                                     .height_pixels = 320}},
        };
        if (with_artwork) {
            track.artwork = music_player::m0::ArtworkThumbnail{
                .width_pixels = 2,
                .height_pixels = 2,
                .rgba8888 = std::make_shared<const std::vector<std::uint8_t>>(
                    std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255})};
        }
        local_metadata_reader_.poll_results.push_back(
            {music_player::m0::MetadataPollStatus::Completed,
             music_player::m0::MetadataReadCompletion{request.generation, request.source_path, std::move(track)}});
    }

    [[nodiscard]] static QUrl sourceToUrl(const QString& source) {
        const bool windows_path =
            source.size() >= 3 && source.at(1) == u':' && (source.at(2) == u'/' || source.at(2) == u'\\');
        if (windows_path || source.startsWith(u'/'))
            return QUrl::fromLocalFile(source);
        return QUrl{source};
    }

    template <typename Event> static int sendDropEvent(QObject* target, Event& event) {
        auto* const window = qobject_cast<QWindow*>(target);
        if (window == nullptr)
            return static_cast<int>(Qt::IgnoreAction);
        QCoreApplication::sendEvent(window, &event);
        return static_cast<int>(event.isAccepted() ? event.dropAction() : Qt::IgnoreAction);
    }

    music_player::m0::PlayerPresenter& presenter_;
    QuickTestPlatform& platform_;
    music_player::m0::QtSystemAppearance& appearance_;
    std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state_;
    std::deque<std::vector<std::filesystem::path>>& local_selections_;
    music_player::m0::PlayerPresenter& local_presenter_;
    QuickTestPlaybackBackend& local_backend_;
    QuickTestSelectablePlaybackBackend* local_output_backend_;
    QuickTestMetadataReader& local_metadata_reader_;
    FakeLocalSourceDiscovery& local_source_discovery_;
    QuickTestAudioFileLocator& local_file_locator_;
    QuickTestAudioFilesPicker& local_files_picker_;
    std::unique_ptr<QMimeData> drag_mime_{};
};

class PlaylistsQuickScenario final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* binding READ binding CONSTANT)
    static std::filesystem::path fixturePath(const char* name) {
#ifdef Q_OS_WIN
        return std::filesystem::path("C:/Fixture") / name;
#else
        return std::filesystem::path("/Fixture") / name;
#endif
    }
    class Store final : public music_player::m0::PlaylistStore {
      public:
        music_player::m0::PlaylistLoadResult load() override {
            const std::lock_guard lock(mutex_);
            return {snapshot_, music_player::m0::PlaylistLoadStatus::Loaded};
        }
        music_player::m0::PlaylistSaveStatus save(const music_player::m0::PlaylistLibrarySnapshot& snapshot) override {
            const std::lock_guard lock(mutex_);
            snapshot_ = snapshot;
            ++saves;
            return music_player::m0::PlaylistSaveStatus::Saved;
        }
        std::atomic<int> saves{};

      private:
        std::mutex mutex_;
        music_player::m0::PlaylistLibrarySnapshot snapshot_{{{7, "Page owner", {{11, fixturePath("original.mp3")}}}}};
    };
    class Picker final : public music_player::m0::AudioFilesPicker {
      public:
        music_player::m0::AudioFilesSelectionCompletion pending;
        bool pick(music_player::m0::AudioFilesSelectionCompletion completion) override {
            if (pending)
                return false;
            pending = std::move(completion);
            return true;
        }
        void cancel() noexcept override { pending = {}; }
    };
    class Locator final : public music_player::m0::AudioFileLocator {
      public:
        music_player::m0::AudioFileLocationCompletion pending;
        bool locate(const std::filesystem::path&, music_player::m0::AudioFileLocationCompletion completion) override {
            if (pending)
                return false;
            pending = std::move(completion);
            return true;
        }
        void cancel() noexcept override { pending = {}; }
    };

  public:
    explicit PlaylistsQuickScenario(QObject* parent)
        : QObject(parent), store_(std::make_shared<Store>()), binding_(store_, {}, {}, nullptr, &picker_, &locator_) {
        QQmlEngine::setObjectOwnership(&binding_, QQmlEngine::CppOwnership);
        static_cast<void>(binding_.retryLoad());
    }
    QObject* binding() noexcept { return &binding_; }
    Q_INVOKABLE int saves() const noexcept { return store_->saves.load(); }
    Q_INVOKABLE int deliveries() const noexcept { return deliveries_; }
    Q_INVOKABLE bool holdReply(const bool replacement) {
        if (replacement) {
            held_location_ = std::exchange(locator_.pending, {});
            return static_cast<bool>(held_location_);
        }
        held_selection_ = std::exchange(picker_.pending, {});
        return static_cast<bool>(held_selection_);
    }
    Q_INVOKABLE void completeHeldReply(const bool replacement) {
        if (replacement) {
            auto completion = std::exchange(held_location_, {});
            if (completion)
                completion({music_player::m0::AudioFileLocationStatus::Selected, fixturePath("late.flac")});
        } else {
            auto completion = std::exchange(held_selection_, {});
            if (completion)
                completion({music_player::m0::AudioFilesSelectionStatus::Selected, {fixturePath("late.flac")}});
        }
        // Same-thread queued barrier follows the binding's queued completion.
        QMetaObject::invokeMethod(this, [this] { ++deliveries_; }, Qt::QueuedConnection);
    }

  private:
    std::shared_ptr<Store> store_;
    Picker picker_;
    Locator locator_;
    music_player::m0::PlaylistsBinding binding_;
    music_player::m0::AudioFilesSelectionCompletion held_selection_;
    music_player::m0::AudioFileLocationCompletion held_location_;
    int deliveries_{};
};

QObject* ScenarioDriver::createPlaylistsScenario() { return new PlaylistsQuickScenario(this); }
bool ScenarioDriver::releasePlaylistsScenario(QObject* scenario) {
    auto* const owned = qobject_cast<PlaylistsQuickScenario*>(scenario);
    if (owned == nullptr || owned->parent() != this)
        return false;
    owned->deleteLater();
    return true;
}

// Tests that exercise real exit own a fresh presenter and all borrowed fakes.
// No production lifecycle state is reset.
class DownloadsQuickScenario final : public QObject {
    Q_OBJECT
    Q_PROPERTY(music_player::m0::PlayerPresenter* presenter READ presenter CONSTANT)
    Q_PROPERTY(QObject* driver READ driver CONSTANT)

  public:
    DownloadsQuickScenario(music_player::m0::PlayerPresenter& fixture_presenter, QuickTestPlatform& fixture_platform,
                           music_player::m0::QtSystemAppearance& appearance,
                           std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state,
                           QObject* parent)
        : QObject(parent) {
        presenter_ = std::make_unique<music_player::m0::PlayerPresenter>(
            platform_, backend_, metadata_reader_, [this] { return takeSelection(); }, source_discovery_);
        files_picker_.selection_reader = [this] { return takeSelection(); };
        presenter_->setAudioFileLocator(&file_locator_);
        presenter_->setAudioFilesPicker(&files_picker_);
        driver_ = std::make_unique<ScenarioDriver>(fixture_presenter, fixture_platform, appearance,
                                                   std::move(appearance_state), selections_, *presenter_, backend_,
                                                   metadata_reader_, source_discovery_, file_locator_, files_picker_);
    }
    [[nodiscard]] music_player::m0::PlayerPresenter* presenter() const noexcept { return presenter_.get(); }
    [[nodiscard]] QObject* driver() const noexcept { return driver_.get(); }
    Q_INVOKABLE int localExitCalls() const noexcept { return platform_.exit_calls; }
    Q_INVOKABLE void bindPlaylistExitState(QObject* playlists) {
        const QPointer<QObject> binding{playlists};
        presenter_->setPlaylistExitPreparationProbe([binding] {
            using music_player::m0::ExitPreparationState;
            if (!binding)
                return ExitPreparationState::Ready;
            if (binding->property("busy").toBool())
                return ExitPreparationState::Pending;
            return binding->property("hasPendingChange").toBool() ? ExitPreparationState::Failed
                                                                  : ExitPreparationState::Ready;
        });
    }

  private:
    [[nodiscard]] std::vector<std::filesystem::path> takeSelection() {
        if (selections_.empty())
            return {};
        auto result = std::move(selections_.front());
        selections_.pop_front();
        return result;
    }
    QuickTestPlatform platform_{};
    QuickTestPlaybackBackend backend_{};
    QuickTestMetadataReader metadata_reader_{};
    FakeLocalSourceDiscovery source_discovery_{};
    QuickTestAudioFileLocator file_locator_{};
    QuickTestAudioFilesPicker files_picker_{};
    std::deque<std::vector<std::filesystem::path>> selections_{};
    // Reverse destruction releases driver/presenter before their borrowed fakes.
    std::unique_ptr<music_player::m0::PlayerPresenter> presenter_{};
    std::unique_ptr<ScenarioDriver> driver_{};
};

QObject* ScenarioDriver::createDownloadsScenario() {
    return new DownloadsQuickScenario(presenter_, platform_, appearance_, appearance_state_, this);
}

bool ScenarioDriver::releaseDownloadsScenario(QObject* scenario) {
    auto* const owned = qobject_cast<DownloadsQuickScenario*>(scenario);
    if (owned == nullptr || owned->parent() != this)
        return false;
    // The QML shell is destroyed first; defer the C++ owner until that teardown.
    owned->deleteLater();
    return true;
}

class EmptyShellQuickTestSetup final : public QObject {
    Q_OBJECT

  public:
    ~EmptyShellQuickTestSetup() override {
        QAccessible::installUpdateHandler(previous_accessibility_update_handler);
        previous_accessibility_update_handler = nullptr;
        if (translation_installed_ && QCoreApplication::instance() != nullptr)
            QCoreApplication::removeTranslator(&pt_br_translation_);
    }

  public slots:
    void applicationAvailable() {
        previous_accessibility_update_handler = QAccessible::installUpdateHandler(recordAccessibilityUpdate);
        Q_INIT_RESOURCE(m0_qt_shell_assets);
        Q_INIT_RESOURCE(m0_qt_fixture_data);
#ifdef RC_MUSIC_TEST_UI_VARIANT
        Q_INIT_RESOURCE(rc_music_test_ui_translations);
#else
        Q_INIT_RESOURCE(m0_qt_shell_ui_translations);
#endif
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        translation_installed_ = pt_br_translation_.load(QStringLiteral(":/i18n/music-player_pt_BR.qm")) &&
                                 QCoreApplication::installTranslator(&pt_br_translation_);
        if (!translation_installed_)
            qFatal("The pt-BR translation catalog could not be installed for Qt Quick tests");
        bundle_ = std::make_unique<music_player::m0::FixtureBundle>(music_player::m0::FixtureRepository{}.load(
            QStringLiteral(":/m0/data/fixture.v1.json"), QStringLiteral(":/m0/data/queue.v1.json")));
    }

    void qmlEngineAvailable(QQmlEngine* engine) {
        platform_ = QuickTestPlatform{};
        details_platform_ = QuickTestPlatform{};
        auto* contract = new music_player::m0::ShellUiContract(*bundle_, engine);
        auto* presenter = new music_player::m0::PlayerPresenter(*bundle_, platform_, engine);
        auto* details_presenter = new music_player::m0::PlayerPresenter(*bundle_, details_platform_, engine);
        pending_exit_autosave_ = std::make_unique<music_player::m0::PreferencesAutosave>(
            pending_exit_writer_, music_player::m0::Preferences{},
            music_player::m0::PreferencesAutosaveOptions{.now = [this] { return pending_exit_now_; }});
        auto* pending_exit_presenter =
            new music_player::m0::PlayerPresenter(*bundle_, pending_exit_platform_,
                                                  music_player::m0::PlayerPresenterPreferences{
                                                      .initial = {},
                                                      .load_status = music_player::m0::PreferencesLoadStatus::Missing,
                                                      .autosave = pending_exit_autosave_.get(),
                                                      .now = [this] { return pending_exit_now_; },
                                                  },
                                                  engine);
        engine->addImageProvider(QStringLiteral("queueart"), new TransparentQueueArtworkProvider);
        auto local_artwork_cache = std::make_shared<music_player::m0::ArtworkImageCache>();
        engine->addImageProvider(music_player::m0::ArtworkImageProvider::providerId(),
                                 new music_player::m0::ArtworkImageProvider(local_artwork_cache));
        auto* local_presenter = new music_player::m0::PlayerPresenter(
            platform_, local_backend_, local_metadata_reader_,
            [this] {
                if (local_selections_.empty())
                    return std::vector<std::filesystem::path>{};
                auto selection = std::move(local_selections_.front());
                local_selections_.pop_front();
                return selection;
            },
            local_source_discovery_, local_artwork_cache, engine);
        local_presenter->setAudioFileLocator(&local_file_locator_);
        local_files_picker_.selection_reader = [this] {
            if (local_selections_.empty())
                return std::vector<std::filesystem::path>{};
            auto selection = std::move(local_selections_.front());
            local_selections_.pop_front();
            return selection;
        };
        local_presenter->setAudioFilesPicker(&local_files_picker_);
        auto appearance_state = std::make_shared<music_player::m0::SystemAppearanceSnapshot>(false, false);
        auto* appearance =
            new music_player::m0::QtSystemAppearance([appearance_state] { return *appearance_state; }, engine);
        music_player::m0::bindShellAppearance(*presenter, *contract, *appearance);
        auto* product_contract = new music_player::m0::ShellUiContract(engine);
        music_player::m0::bindShellAppearance(*local_presenter, *product_contract, *appearance);
        engine->rootContext()->setContextProperty(QStringLiteral("testShellUiContract"), contract);
        engine->rootContext()->setContextProperty(QStringLiteral("testProductUiContract"), product_contract);
        engine->rootContext()->setContextProperty(QStringLiteral("testPlayerPresenter"), presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testDetailsFixturePresenter"), details_presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testPendingExitPresenter"), pending_exit_presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testLocalPlayerPresenter"), local_presenter);
        engine->rootContext()->setContextProperty(
            QStringLiteral("testScenarioDriver"),
            new ScenarioDriver(*presenter, platform_, *appearance, appearance_state, local_selections_,
                               *local_presenter, local_backend_, local_metadata_reader_, local_source_discovery_,
                               local_file_locator_, local_files_picker_, engine));
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
    QuickTestPlatform platform_;
    QuickTestPlatform details_platform_;
    QuickTestPlatform pending_exit_platform_;
    PendingPreferencesWriter pending_exit_writer_{};
    std::unique_ptr<music_player::m0::PreferencesAutosave> pending_exit_autosave_{};
    music_player::m0::PreferencesAutosave::TimePoint pending_exit_now_{};
    QuickTestSelectablePlaybackBackend local_backend_{};
    QuickTestMetadataReader local_metadata_reader_{};
    FakeLocalSourceDiscovery local_source_discovery_{};
    QuickTestAudioFileLocator local_file_locator_{};
    QuickTestAudioFilesPicker local_files_picker_{};
    std::deque<std::vector<std::filesystem::path>> local_selections_{};
    QTranslator pt_br_translation_{};
    bool translation_installed_{false};
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
