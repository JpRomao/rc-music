#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QAccessible>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QStringList>
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
    void publish(const music_player::m0::PlaybackStatus status) {
        snapshot_.status = status;
        snapshot_.duration_ms = 180'000;
        snapshot_.seekable = true;
        if (event_callback_)
            event_callback_({music_player::m0::PlaybackEventType::SnapshotChanged, snapshot_});
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
        return {music_player::m0::MetadataPollStatus::Pending, std::nullopt};
    }
    [[nodiscard]] std::optional<music_player::m0::MetadataReadCompletion> tryTakeCompletion() override {
        return std::nullopt;
    }
    music_player::m0::MetadataCommandResult shutdown() override {
        return music_player::m0::MetadataCommandResult::Accepted;
    }

    std::vector<music_player::m0::MetadataReadRequest> requests{};
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
                   QuickTestPlaybackBackend& local_backend, QObject* parent = nullptr)
        : QObject(parent), presenter_(presenter), platform_(platform), appearance_(appearance),
          appearance_state_(std::move(appearance_state)), local_selections_(local_selections),
          local_backend_(local_backend) {}
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
    music_player::m0::PlayerPresenter& presenter_;
    QuickTestPlatform& platform_;
    music_player::m0::QtSystemAppearance& appearance_;
    std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state_;
    std::deque<std::vector<std::filesystem::path>>& local_selections_;
    QuickTestPlaybackBackend& local_backend_;
};

class EmptyShellQuickTestSetup final : public QObject {
    Q_OBJECT

  public:
    ~EmptyShellQuickTestSetup() override {
        QAccessible::installUpdateHandler(previous_accessibility_update_handler);
        previous_accessibility_update_handler = nullptr;
    }

  public slots:
    void applicationAvailable() {
        previous_accessibility_update_handler = QAccessible::installUpdateHandler(recordAccessibilityUpdate);
        Q_INIT_RESOURCE(m0_qt_shell_assets);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        bundle_ = std::make_unique<music_player::m0::FixtureBundle>(music_player::m0::FixtureRepository{}.load(
            QStringLiteral(":/m0/data/fixture.v1.json"), QStringLiteral(":/m0/data/queue.v1.json")));
    }

    void qmlEngineAvailable(QQmlEngine* engine) {
        platform_ = QuickTestPlatform{};
        auto* contract = new music_player::m0::ShellUiContract(*bundle_, engine);
        auto* presenter = new music_player::m0::PlayerPresenter(*bundle_, platform_, engine);
        auto* local_presenter = new music_player::m0::PlayerPresenter(
            *bundle_, platform_, local_backend_, local_metadata_reader_,
            [this] {
                if (local_selections_.empty())
                    return std::vector<std::filesystem::path>{};
                auto selection = std::move(local_selections_.front());
                local_selections_.pop_front();
                return selection;
            },
            engine);
        auto appearance_state = std::make_shared<music_player::m0::SystemAppearanceSnapshot>(false, false);
        auto* appearance =
            new music_player::m0::QtSystemAppearance([appearance_state] { return *appearance_state; }, engine);
        music_player::m0::bindShellAppearance(*presenter, *contract, *appearance);
        engine->rootContext()->setContextProperty(QStringLiteral("testShellUiContract"), contract);
        engine->rootContext()->setContextProperty(QStringLiteral("testPlayerPresenter"), presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testLocalPlayerPresenter"), local_presenter);
        engine->rootContext()->setContextProperty(QStringLiteral("testScenarioDriver"),
                                                  new ScenarioDriver(*presenter, platform_, *appearance,
                                                                     appearance_state, local_selections_,
                                                                     local_backend_, engine));
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
    QuickTestPlatform platform_;
    QuickTestPlaybackBackend local_backend_{};
    QuickTestMetadataReader local_metadata_reader_{};
    std::deque<std::vector<std::filesystem::path>> local_selections_{};
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
