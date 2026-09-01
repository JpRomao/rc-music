#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/ports/LocalSourceDiscovery.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QAccessible>
#include <QClipboard>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QStringList>
#include <QTranslator>
#include <QVariantMap>
#include <QWindow>
#include <QtQuickTest/quicktest.h>
#include <QtTest/qtestkeyboard.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class QuickTestPlatform final : public music_player::m0::PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return tray_available; }
    void hideWindow() override { ++hide_calls; }
    void minimizeWindow() override { ++minimize_calls; }
    void showAndActivateWindow() override { ++restore_calls; }
    void requestExit() override { ++exit_calls; }
    bool tray_available{false};
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

class QuickTestPlaybackBackend final : public music_player::m0::PlaybackBackend {
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
        return music_player::m0::PlaybackCommandResult::Accepted;
    }
    music_player::m0::PlaybackCommandResult seekTo(std::int64_t) override {
        return music_player::m0::PlaybackCommandResult::Accepted;
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
        unload_calls = 0;
    }

    music_player::m0::PlaybackSnapshot snapshot_{};
    music_player::m0::PlaybackEventCallback event_callback_{};
    std::vector<std::filesystem::path> loaded_paths{};
    int play_calls{0};
    int unload_calls{0};

  private:
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
                   QObject* parent = nullptr)
        : QObject(parent), presenter_(presenter), platform_(platform), appearance_(appearance),
          appearance_state_(std::move(appearance_state)), local_selections_(local_selections),
          local_presenter_(local_presenter), local_backend_(local_backend),
          local_metadata_reader_(local_metadata_reader), local_source_discovery_(local_source_discovery) {}
    Q_INVOKABLE void setTrayAvailable(const bool available) {
        platform_.tray_available = available;
        presenter_.refreshPlatformAvailability();
    }
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
    Q_INVOKABLE void publishLocalReady() { local_backend_.publish(music_player::m0::PlaybackStatus::Ready); }
    Q_INVOKABLE void publishLocalPlaying() { local_backend_.publish(music_player::m0::PlaybackStatus::Playing); }
    Q_INVOKABLE void publishLocalUnseekablePlaying() {
        local_backend_.publish(music_player::m0::PlaybackStatus::Playing, false);
    }
    Q_INVOKABLE void publishLocalError() {
        local_backend_.snapshot_.error = music_player::m0::PlaybackErrorCode::InvalidMedia;
        local_backend_.publish(music_player::m0::PlaybackStatus::Error);
    }
    Q_INVOKABLE void publishCompleteLocalMetadata() {
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
    Q_INVOKABLE void resetLocalScenario() {
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
    QuickTestMetadataReader& local_metadata_reader_;
    FakeLocalSourceDiscovery& local_source_discovery_;
    std::unique_ptr<QMimeData> drag_mime_{};
};

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
        Q_INIT_RESOURCE(m0_qt_shell_ui_translations);
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
        auto* local_presenter = new music_player::m0::PlayerPresenter(
            *bundle_, platform_, local_backend_, local_metadata_reader_,
            [this] {
                if (local_selections_.empty())
                    return std::vector<std::filesystem::path>{};
                auto selection = std::move(local_selections_.front());
                local_selections_.pop_front();
                return selection;
            },
            local_source_discovery_, engine);
        auto appearance_state = std::make_shared<music_player::m0::SystemAppearanceSnapshot>(false, false);
        auto* appearance =
            new music_player::m0::QtSystemAppearance([appearance_state] { return *appearance_state; }, engine);
        music_player::m0::bindShellAppearance(*presenter, *contract, *appearance);
        engine->rootContext()->setContextProperty(QStringLiteral("testShellUiContract"), contract);
        engine->rootContext()->setContextProperty(QStringLiteral("testPlayerPresenter"), presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testDetailsFixturePresenter"), details_presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testPendingExitPresenter"), pending_exit_presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testLocalPlayerPresenter"), local_presenter);
        engine->rootContext()->setContextProperty(
            QStringLiteral("testScenarioDriver"),
            new ScenarioDriver(*presenter, platform_, *appearance, appearance_state, local_selections_,
                               *local_presenter, local_backend_, local_metadata_reader_, local_source_discovery_,
                               engine));
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
    QuickTestPlatform platform_;
    QuickTestPlatform details_platform_;
    QuickTestPlatform pending_exit_platform_;
    PendingPreferencesWriter pending_exit_writer_{};
    std::unique_ptr<music_player::m0::PreferencesAutosave> pending_exit_autosave_{};
    music_player::m0::PreferencesAutosave::TimePoint pending_exit_now_{};
    QuickTestPlaybackBackend local_backend_{};
    QuickTestMetadataReader local_metadata_reader_{};
    FakeLocalSourceDiscovery local_source_discovery_{};
    std::deque<std::vector<std::filesystem::path>> local_selections_{};
    QTranslator pt_br_translation_{};
    bool translation_installed_{false};
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
