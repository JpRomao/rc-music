#include "OpenSourcesCommand.hpp"
#include "OpenSourcesDispatcher.hpp"
#include "QtSingleInstanceCoordinator.hpp"
#include "ShellLaunchOptions.hpp"
#include "adapters/QtShellPlatform.hpp"
#ifdef Q_OS_WIN
#include "adapters/QtDiagnosticPrivacy.hpp"
#include "adapters/QtJsonWindowsPreferencesStore.hpp"
#include "adapters/QtWindowsDiagnosticLogStore.hpp"
#include "adapters/WindowsPowerEvents.hpp"
#include "adapters/WindowsSmtcAdapter.hpp"
#include "m0/adapters/SingleWorkerDiagnosticWriter.hpp"
#include "m0/adapters/SingleWorkerPreferencesWriter.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#ifdef M0_QT_COMBINED_AUDIO
#include "adapters/QtJsonWindowsSessionStore.hpp"
#include "m0/adapters/SingleWorkerSessionWriter.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/application/SessionStartup.hpp"
#endif
#endif
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/domain/WindowPlacement.hpp"
#include "presentation/ApplicationIdentity.hpp"
#include "presentation/ApplicationText.hpp"
#include "presentation/ArtworkImageProvider.hpp"
#include "presentation/LifecycleText.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
#include "presentation/ShellPlatformBinding.hpp"
#include "presentation/ShellUiContract.hpp"
#ifdef Q_OS_WIN
#include "presentation/ShellDiagnosticBinding.hpp"
#include "presentation/SystemMediaControlsBinding.hpp"
#include "presentation/SystemPowerEventsBinding.hpp"
#endif

#ifdef M0_QT_COMBINED_AUDIO
#include "adapters/QtAudioFileLocator.hpp"
#include "adapters/QtAudioFilesPicker.hpp"
#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"
#include "m0/adapters/SingleWorkerMetadataReader.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"
#include "m0/domain/LocalAudioFilePolicy.hpp"

#include <QDir>
#include <QMessageBox>
#ifdef M0_QT_SHELL_TEST_HOOKS
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#endif
#endif

#ifdef M0_QT_SHELL_TEST_HOOKS
#include "ShellLifecycleSmoke.hpp"

#include <QAction>
#include <QMenu>
#endif

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QRect>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QVariant>
#include <QWindow>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using music_player::m0::FixtureRepository;
using music_player::m0::PlayerPresenter;
using music_player::m0::ShellUiContract;
using music_player::m0::experiment::InitialShellScenario;
using music_player::m0::experiment::InitialWindowDisposition;
using music_player::m0::experiment::parseShellLaunchOptions;
using music_player::m0::experiment::PlayerPresenterScenarioControl;
using music_player::m0::experiment::resolveInitialWindowDisposition;
using music_player::m0::experiment::resolveShellSourcePaths;
using music_player::m0::experiment::validateShellLaunchOptions;

namespace {
constexpr music_player::m0::WindowSize minimum_window_size{720, 520};

[[nodiscard]] bool
applyRestoredWindowPlacement(QWindow& window, const std::optional<music_player::m0::WindowPlacement>& saved_placement) {
    if (!saved_placement.has_value())
        return false;

    const QList<QScreen*> screens = QGuiApplication::screens();
    std::vector<music_player::m0::WindowWorkArea> work_areas;
    work_areas.reserve(static_cast<std::size_t>(screens.size()));
    for (const QScreen* const screen : screens) {
        if (screen == nullptr) {
            work_areas.push_back({});
            continue;
        }
        const QRect available = screen->availableGeometry();
        work_areas.push_back({available.x(), available.y(), available.width(), available.height()});
    }

    const auto resolved = music_player::m0::resolveWindowPlacement(*saved_placement, work_areas, minimum_window_size);
    if (!resolved.has_value() || resolved->screen_index >= static_cast<std::size_t>(screens.size()))
        return false;
    QScreen* const target_screen = screens.at(static_cast<qsizetype>(resolved->screen_index));
    if (target_screen == nullptr)
        return false;

    window.setScreen(target_screen);
    const auto& placement = resolved->placement;
    window.setGeometry(placement.x, placement.y, placement.width, placement.height);
    return true;
}

#ifdef M0_QT_COMBINED_AUDIO
[[nodiscard]] QString localAudioDialogFilter() {
    QStringList patterns;
    for (const std::string_view extension : music_player::m0::LocalAudioFilePolicy::candidateExtensions()) {
        const QString suffix = QString::fromLatin1(extension.data(), static_cast<qsizetype>(extension.size()));
        patterns.append(QStringLiteral("*") + suffix);
        patterns.append(QStringLiteral("*") + suffix.toUpper());
    }
    return music_player::m0::ApplicationText::localAudioDialogFilter(patterns);
}

[[nodiscard]] std::vector<std::filesystem::path> nativeSourcePaths(const QStringList& paths) {
    std::vector<std::filesystem::path> native_paths;
    native_paths.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
#ifdef Q_OS_WIN
        native_paths.emplace_back(path.toStdWString());
#else
        const QByteArray utf8 = path.toUtf8();
        native_paths.emplace_back(std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())});
#endif
    }
    return native_paths;
}

[[nodiscard]] int reportOpenSourcesFailure(const music_player::m0::QtSingleInstanceStartResult& result,
                                           const bool suppress_dialog) {
    qCritical().noquote() << "Unable to open shell sources:" << result.detail;
#ifdef M0_QT_SHELL_TEST_HOOKS
    // The executable harness needs closed outcomes, never the external detail.
    if (suppress_dialog) {
        if (result.detail == QStringLiteral("invalidPaths"))
            std::fputs("M0_SINGLE_INSTANCE_REJECTED invalidPaths\n", stdout);
        else if (result.detail == QStringLiteral("tooManyPaths"))
            std::fputs("M0_SINGLE_INSTANCE_REJECTED tooManyPaths\n", stdout);
        else if (result.detail == QStringLiteral("temporarilyUnavailable"))
            std::fputs("M0_SINGLE_INSTANCE_REJECTED temporarilyUnavailable\n", stdout);
        std::fflush(stdout);
    }
#endif
    if (!suppress_dialog) {
        QMessageBox::critical(
            nullptr, music_player::m0::ApplicationText::openSourcesFailureTitle(),
            music_player::m0::ApplicationText::openSourcesFailureMessage(
                result.detail, result.outcome == music_player::m0::QtSingleInstanceOutcome::TimedOut));
    }
    return result.outcome == music_player::m0::QtSingleInstanceOutcome::Rejected ? 2 : 1;
}
#endif
#ifdef M0_QT_SHELL_TEST_HOOKS
[[nodiscard]] bool isVisible(const QObject* object) {
    return object != nullptr && object->property("visible").toBool();
}

[[nodiscard]] bool runErrorRecoverySmoke(QObject& root, PlayerPresenter& presenter) {
    QObject* const transport_before = root.findChild<QObject*>(QStringLiteral("transport-bar"));
    QObject* const error_state = root.findChild<QObject*>(QStringLiteral("error-state"));
    QObject* const error_skip = root.findChild<QObject*>(QStringLiteral("error-skip"));
    QAbstractItemModel* const queue_model = presenter.queueModelObject();
    if (presenter.playbackState() != PlayerPresenter::PlaybackState::Error || queue_model == nullptr ||
        queue_model->rowCount() != 1000 || transport_before == nullptr ||
        transport_before->property("enabled").toBool() || !isVisible(error_state) || error_skip == nullptr ||
        !QMetaObject::invokeMethod(error_skip, "clicked", Qt::DirectConnection)) {
        return false;
    }
    QCoreApplication::processEvents();
    QObject* const transport_after = root.findChild<QObject*>(QStringLiteral("transport-bar"));
    QObject* const playing_state = root.findChild<QObject*>(QStringLiteral("playing-state"));
    return presenter.playbackState() == PlayerPresenter::PlaybackState::Playing &&
           presenter.queueModelObject() == queue_model && queue_model->rowCount() == 1000 &&
           transport_after == transport_before && transport_after->property("enabled").toBool() &&
           !isVisible(error_state) && isVisible(playing_state);
}

[[nodiscard]] music_player::m0::Preferences restartPreferences() {
    return {.theme = music_player::m0::ThemeMode::Dark,
            .motion = music_player::m0::MotionMode::Reduced,
            .volume = 0.37,
            .muted = true,
            .skip_backward_seconds = 7,
            .skip_forward_seconds = 20,
            .close_behavior = music_player::m0::CloseBehavior::Exit,
            .restore_last_session = false,
            .start_minimized = true,
            .queue_end_behavior = music_player::m0::QueueEndBehavior::StopClearSelection,
            .window_placement = music_player::m0::WindowPlacement{20, 30, 720, 520}};
}

[[nodiscard]] bool writeRestartPreferences(PlayerPresenter& presenter) {
    const bool theme = presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme);
    const bool motion = presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion);
    const bool volume = presenter.setVolume(0.37);
    const bool muted = presenter.toggleMute();
    const bool backward = presenter.setSkipBackwardSeconds(7);
    const bool forward = presenter.setSkipForwardSeconds(20);
    const bool close = presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit);
    const bool restore = presenter.setRestoreLastSession(false);
    const bool minimized = presenter.setStartMinimized(true);
    const bool queue_end = presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection);
    auto expected_before_geometry = restartPreferences();
    expected_before_geometry.window_placement.reset();
    auto actual_before_geometry = presenter.preferences();
    actual_before_geometry.window_placement.reset();
    const bool snapshot = actual_before_geometry == expected_before_geometry;
    if (!(theme && motion && volume && muted && backward && forward && close && restore && minimized && queue_end &&
          snapshot)) {
        qCritical() << "Unable to apply the complete restart preference snapshot" << theme << motion << volume << muted
                    << backward << forward << close << restore << minimized << queue_end << snapshot;
    }
    return theme && motion && volume && muted && backward && forward && close && restore && minimized && queue_end &&
           snapshot;
}
#endif
} // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // Installed before Qt, plugins and user inputs; never restored during teardown.
    if (!music_player::m0::installProcessDiagnosticPrivacy())
        return 1;
#endif
    Q_INIT_RESOURCE(m0_qt_shell_assets);
    Q_INIT_RESOURCE(m0_qt_shell_ui_translations);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    music_player::m0::ApplicationIdentity::configureQtApplication();
    application.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
    std::unique_ptr<music_player::m0::QtWindowsDiagnosticLogStore> diagnostic_store;
    std::unique_ptr<music_player::m0::SingleWorkerDiagnosticWriter> diagnostics;
#endif
    try {
        QTranslator pt_br_translation;
        if (!pt_br_translation.load(QStringLiteral(":/i18n/music-player_pt_BR.qm")) ||
            !QCoreApplication::installTranslator(&pt_br_translation)) {
            throw std::runtime_error{"the pt-BR translation catalog could not be installed"};
        }
        const auto options = parseShellLaunchOptions(application.arguments());
        validateShellLaunchOptions(options,
#ifdef M0_QT_SHELL_TEST_HOOKS
                                   true,
#else
                                   false,
#endif
#ifdef M0_QT_COMBINED_AUDIO
                                   true
#else
                                   false
#endif
        );
#ifndef Q_OS_WIN
        if (options.diagnostics_enabled)
            throw std::invalid_argument{"diagnostics is available only on Windows"};
#endif
#if defined(Q_OS_WIN) || defined(M0_QT_COMBINED_AUDIO)
        QString config_root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#ifdef M0_QT_SHELL_TEST_HOOKS
        if (options.test_config_root.has_value())
            config_root = *options.test_config_root;
#endif
#endif
#ifdef M0_QT_COMBINED_AUDIO
#ifdef M0_QT_SHELL_TEST_HOOKS
        const bool suppress_source_failure_dialog = options.smoke_single_instance_executable;
#else
        constexpr bool suppress_source_failure_dialog = false;
#endif
        music_player::m0::OpenSourcesCommand initial_sources;
        try {
            initial_sources.paths = resolveShellSourcePaths(options.source_paths, QDir::currentPath());
        } catch (const std::invalid_argument&) {
            return reportOpenSourcesFailure(
                {music_player::m0::QtSingleInstanceOutcome::Rejected, QStringLiteral("invalidPaths")},
                suppress_source_failure_dialog);
        }
        music_player::m0::QtSingleInstanceCoordinator single_instance{music_player::m0::QtSingleInstanceOptions{
            .lock_file_path = QDir{config_root}.filePath(QStringLiteral("single-instance.lock")),
            .server_name_prefix = QStringLiteral("music-player.open-sources.v1"),
        }};
        const auto single_instance_result = single_instance.start(initial_sources);
        if (single_instance_result.outcome == music_player::m0::QtSingleInstanceOutcome::Forwarded) {
#ifdef M0_QT_SHELL_TEST_HOOKS
            if (options.smoke_single_instance_executable) {
                std::fputs("M0_SINGLE_INSTANCE_FORWARDED\n", stdout);
                std::fflush(stdout);
            }
#endif
            return 0;
        }
        if (single_instance_result.outcome != music_player::m0::QtSingleInstanceOutcome::Primary) {
            return reportOpenSourcesFailure(single_instance_result, suppress_source_failure_dialog);
        }
#endif
#ifdef Q_OS_WIN
        if (options.diagnostics_enabled) {
            diagnostic_store = std::make_unique<music_player::m0::QtWindowsDiagnosticLogStore>(config_root);
            diagnostics = std::make_unique<music_player::m0::SingleWorkerDiagnosticWriter>(*diagnostic_store);
            static_cast<void>(diagnostics->submit(
                {music_player::m0::DiagnosticSeverity::Info, music_player::m0::DiagnosticEvent::ApplicationStarted}));
        }
        music_player::m0::QtJsonWindowsPreferencesStore preferences_store(config_root);
        const music_player::m0::PreferencesLoadResult loaded_preferences = preferences_store.load();
        music_player::m0::SingleWorkerPreferencesWriter preferences_writer(preferences_store);
        music_player::m0::PreferencesAutosave preferences_autosave(preferences_writer, loaded_preferences.preferences);
        music_player::m0::PlayerPresenterPreferences presenter_preferences{
            .initial = loaded_preferences.preferences,
            .load_status = loaded_preferences.status,
            .autosave = &preferences_autosave,
        };
#ifdef M0_QT_SHELL_TEST_HOOKS
        if (options.smoke_single_instance_executable)
            presenter_preferences.exit_flush_timeout = std::chrono::seconds{10};
#endif
#ifdef M0_QT_COMBINED_AUDIO
#ifdef M0_QT_SHELL_TEST_HOOKS
        QSemaphore session_crash_barrier;
        std::atomic_bool session_crash_gate_armed{false};
        music_player::m0::QtJsonWindowsSessionStoreOptions session_store_options;
        if (options.smoke_session_crash_before_primary_commit) {
            session_store_options.inject_fault = [&session_crash_barrier, &session_crash_gate_armed](
                                                     const music_player::m0::SessionStoreFaultPoint point) {
                if (point != music_player::m0::SessionStoreFaultPoint::BeforePrimaryCommit ||
                    !session_crash_gate_armed.load(std::memory_order_acquire)) {
                    return false;
                }
                std::fputs("M0_SESSION_BEFORE_PRIMARY_COMMIT\n", stdout);
                std::fflush(stdout);
                session_crash_barrier.acquire();
                return false;
            };
        }
        music_player::m0::QtJsonWindowsSessionStore session_store(config_root, std::move(session_store_options));
#else
        music_player::m0::QtJsonWindowsSessionStore session_store(config_root);
#endif
        music_player::m0::SessionLoadResult loaded_session =
            music_player::m0::loadSessionForStartup(session_store, loaded_preferences.preferences.restore_last_session);
        music_player::m0::SingleWorkerSessionWriter session_writer(session_store);
        music_player::m0::SessionAutosave session_autosave(session_writer, loaded_session.snapshot);
        presenter_preferences.session = {
            .initial = std::move(loaded_session.snapshot),
            .load_status = loaded_session.status,
            .autosave = &session_autosave,
        };
#endif
#endif
        const auto bundle = FixtureRepository{}.load(QStringLiteral(":/m0/data/fixture.v1.json"),
                                                     QStringLiteral(":/m0/data/queue.v1.json"));
        ShellUiContract contract(bundle);
        music_player::m0::LifecycleText lifecycle_text;
        std::function<bool()> availability_reader;
#ifdef M0_QT_SHELL_TEST_HOOKS
        bool tray_smoke_available = !options.smoke_start_minimized_no_tray;
        if (options.smoke_tray_lifecycle || options.smoke_preferences_restart_write ||
            options.smoke_preferences_restart_read || options.smoke_session_restart_read ||
            options.smoke_session_crash_before_primary_commit || options.smoke_session_recovery_repair ||
            options.smoke_session_defaults_read || options.smoke_single_instance_executable ||
            options.smoke_start_minimized_no_tray || options.smoke_startup_source_precedence)
            availability_reader = [&tray_smoke_available] { return tray_smoke_available; };
#endif
        music_player::m0::QtShellPlatform platform(
            QIcon{QStringLiteral(":/m0/assets/cover-track.svg")}, contract.appName(),
            {contract.actionPlay(), contract.actionPause(), contract.actionPrevious(), contract.actionNext(),
             lifecycle_text.showAction(), lifecycle_text.hideAction(), lifecycle_text.exitAction(),
             lifecycle_text.noTrack()},
            std::move(availability_reader));
        music_player::m0::QtSystemAppearance system_appearance;
#ifdef M0_QT_COMBINED_AUDIO
        auto artwork_cache = std::make_shared<music_player::m0::ArtworkImageCache>();
        music_player::m0::QtMultimediaPlaybackBackend backend;
#ifdef M0_QT_SHELL_TEST_HOOKS
        if (options.smoke_single_instance_executable) {
            std::fputs("M0_SINGLE_INSTANCE_BACKEND_READY\n", stdout);
            std::fflush(stdout);
        }
#endif
        music_player::m0::SingleWorkerMetadataReader metadata_reader(
            std::make_unique<music_player::m0::TagLibMetadataExtractor>());
        music_player::m0::QtAudioFilesPicker audio_files_picker(contract.openAudioText(), localAudioDialogFilter());
        // Synchronous constructors remain a deterministic test seam; production binds the async port below.
        PlayerPresenter::AudioFilePicker audio_file_picker = [] { return std::vector<std::filesystem::path>{}; };
        music_player::m0::QtAudioFileLocator file_locator(music_player::m0::ApplicationText::locateAudioDialogTitle(),
                                                          localAudioDialogFilter());
        PlayerPresenter presenter(bundle, platform, backend, metadata_reader, std::move(audio_file_picker),
                                  artwork_cache,
#ifdef Q_OS_WIN
                                  std::move(presenter_preferences)
#else
                                  nullptr
#endif
        );
        bool shell_platform_ready = false;
        bool source_activation_pending = false;
        bool source_import_was_active = presenter.sourceImportActive();
        bool single_instance_stopped = false;
        music_player::m0::OpenSourcesDispatcher source_dispatcher{
            [&] {
                if (!shell_platform_ready) {
                    source_activation_pending = true;
                    return true;
                }
                return presenter.requestRestore();
            },
            [&] { return presenter.sourceImportActive(); },
            [&](const QStringList& paths) {
                QAbstractItemModel* const queue_model = presenter.queueModelObject();
                if (queue_model == nullptr || queue_model->rowCount() < 0)
                    return false;
                return presenter.openSourcePaths(nativeSourcePaths(paths),
                                                 static_cast<std::size_t>(queue_model->rowCount()));
            }};
        std::optional<music_player::m0::OpenSourcesCommand> retained_source_command;
        std::function<void()> pump_source_commands;
        QObject source_ipc_context;
        pump_source_commands = [&] {
            if (single_instance_stopped)
                return;
            if (retained_source_command.has_value()) {
                if (!source_dispatcher.enqueue(*retained_source_command))
                    return;
                retained_source_command.reset();
            }
            while (auto command = single_instance.takeNextCommand()) {
                if (!source_dispatcher.enqueue(*command)) {
                    retained_source_command = std::move(*command);
                    return;
                }
            }
        };
        presenter.setSourceExitDrainedProbe([&] {
            return !retained_source_command.has_value() && single_instance.pendingCommandCount() == 0 &&
                   source_dispatcher.hasDrainedAcceptedWork();
        });
        QObject::connect(&single_instance, &music_player::m0::QtSingleInstanceCoordinator::commandAvailable,
                         &source_ipc_context, pump_source_commands, Qt::DirectConnection);
        QObject::connect(&presenter, &PlayerPresenter::sourceImportChanged, &source_ipc_context, [&] {
            const bool source_import_active = presenter.sourceImportActive();
            const bool source_import_completed = source_import_was_active && !source_import_active;
            source_import_was_active = source_import_active;
            if (!source_import_completed)
                return;
            source_dispatcher.importerStateChanged();
            pump_source_commands();
        });
        const std::function<void()> stop_single_instance = [&] {
            if (single_instance_stopped)
                return;
            single_instance_stopped = true;
            single_instance.beginShutdown();
            source_dispatcher.stopAccepting();
        };
        QObject::connect(&presenter, &PlayerPresenter::lifecycleChanged, &source_ipc_context, [&] {
            if (presenter.exitCommitted()) {
                stop_single_instance();
            } else if (presenter.exiting()) {
                single_instance.setAccepting(false);
                pump_source_commands();
            } else {
                single_instance.setAccepting(true);
                source_dispatcher.retryPending();
                pump_source_commands();
            }
        });
        QObject::connect(&application, &QCoreApplication::aboutToQuit, &source_ipc_context, stop_single_instance);
        pump_source_commands();
#else
#ifdef Q_OS_WIN
        PlayerPresenter presenter(bundle, platform, std::move(presenter_preferences));
#else
        PlayerPresenter presenter(bundle, platform);
#endif
#endif
#ifdef Q_OS_WIN
        std::unique_ptr<music_player::m0::ShellDiagnosticBinding> diagnostic_binding;
        if (diagnostics)
            diagnostic_binding = std::make_unique<music_player::m0::ShellDiagnosticBinding>(presenter, *diagnostics);
#endif
        // Destroy QML roots before the C++ objects referenced by their required properties.
        QQmlApplicationEngine engine;
#ifdef M0_QT_COMBINED_AUDIO
        engine.addImageProvider(music_player::m0::ArtworkImageProvider::providerId(),
                                new music_player::m0::ArtworkImageProvider(artwork_cache));
#endif
        music_player::m0::bindShellAppearance(presenter, contract, system_appearance);
#ifdef M0_QT_SHELL_TEST_HOOKS
        if (options.smoke_preferences_restart_read &&
            (presenter.preferences() != restartPreferences() || !contract.darkTheme())) {
            throw std::runtime_error{"persisted preferences were not applied before QML creation"};
        }
#ifdef M0_QT_COMBINED_AUDIO
        if (options.smoke_preferences_restart_read &&
            (presenter.hasCurrentTrack() || presenter.queueModelObject()->rowCount() != 0)) {
            throw std::runtime_error{"disabled session restore performed hydration"};
        }
        const bool restored_session_smoke = options.smoke_session_restart_read ||
                                            options.smoke_session_crash_before_primary_commit ||
                                            options.smoke_session_recovery_repair;
        if (restored_session_smoke && (!presenter.hasCurrentTrack() || presenter.queueModelObject()->rowCount() != 1 ||
                                       presenter.currentOccurrenceId() != QStringLiteral("local-77") ||
                                       presenter.playbackState() == PlayerPresenter::PlaybackState::Playing)) {
            throw std::runtime_error{"persisted session was not restored safely before QML creation"};
        }
        if (options.smoke_session_recovery_repair &&
            presenter.preferenceWarning() != PlayerPresenter::PreferenceWarning::RecoveredFromBackup) {
            throw std::runtime_error{"the corrupt primary did not expose its recovered-backup warning"};
        }
        if (options.smoke_session_defaults_read &&
            (presenter.hasCurrentTrack() || presenter.queueModelObject()->rowCount() != 0 ||
             presenter.playbackState() == PlayerPresenter::PlaybackState::Playing ||
             presenter.preferenceWarning() != PlayerPresenter::PreferenceWarning::UsingDefaults)) {
            throw std::runtime_error{
                "invalid session slots did not start with safe defaults and an actionable warning"};
        }
#endif
#endif
        if (options.initial_scenario == InitialShellScenario::Unavailable &&
            (!presenter.loadFrozenQueue() || !PlayerPresenterScenarioControl::enterUnavailable(presenter))) {
            throw std::runtime_error{"unable to enter the frozen unavailable scenario"};
        }
        engine.setInitialProperties({{QStringLiteral("uiContract"), QVariant::fromValue(&contract)},
                                     {QStringLiteral("presenter"), QVariant::fromValue(&presenter)},
                                     {QStringLiteral("startupReady"), false}});
        engine.loadFromModule(QStringLiteral("MusicPlayer.M0"), QStringLiteral("Main"));
        if (engine.rootObjects().isEmpty())
            return 1;
        auto* const window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
        if (window == nullptr)
            return 1;
        if (window->isVisible())
            throw std::runtime_error{"the QML root became visible before startup placement was resolved"};
        (void)applyRestoredWindowPlacement(*window, presenter.preferences().window_placement);
        music_player::m0::bindShellPlatform(presenter, platform, *window);
#ifdef M0_QT_COMBINED_AUDIO
        file_locator.bindWindow(*window);
        presenter.setAudioFileLocator(&file_locator);
        audio_files_picker.bindWindow(*window);
        presenter.setAudioFilesPicker(&audio_files_picker);
#endif
#ifdef Q_OS_WIN
        music_player::m0::WindowsSmtcAdapter system_media_controls;
        music_player::m0::SystemMediaControlsBinding system_media_controls_binding(presenter, system_media_controls);
        static_cast<void>(system_media_controls.bindWindow(*window));
        music_player::m0::WindowsPowerEvents system_power_events;
        music_player::m0::SystemPowerEventsBinding system_power_events_binding(presenter, system_power_events);
        static_cast<void>(system_power_events.start());
#endif
        bool has_explicit_startup_sources = false;
#ifdef M0_QT_COMBINED_AUDIO
        has_explicit_startup_sources = !initial_sources.paths.isEmpty();
#endif
        const InitialWindowDisposition initial_window_disposition = resolveInitialWindowDisposition(
            presenter.startMinimized(), platform.trayAvailable(), has_explicit_startup_sources);
        if (!window->setProperty("startupReady", true)) {
            throw std::runtime_error{"the QML root rejected the startup-ready transition"};
        }
        if (initial_window_disposition == InitialWindowDisposition::HideToTray && !presenter.requestHide())
            throw std::runtime_error{"the shell could not honor its start-minimized preference"};
        if (initial_window_disposition == InitialWindowDisposition::ShowWithTrayUnavailableNotice)
            presenter.reportTrayUnavailable();
#ifdef M0_QT_COMBINED_AUDIO
        shell_platform_ready = true;
        if (std::exchange(source_activation_pending, false) && !presenter.requestRestore())
            throw std::runtime_error{"the shell could not activate for its initial source command"};
#endif
#ifdef M0_QT_SHELL_TEST_HOOKS
        bool tray_smoke_completed = false;
        bool combined_startup_smoke_completed = false;
        bool preferences_restart_completed = false;
        bool startup_behavior_completed = false;
#ifdef M0_QT_COMBINED_AUDIO
        bool session_restart_completed = false;
        bool session_recovery_completed = false;
        bool session_defaults_completed = false;
        bool single_instance_completed = false;
        int single_instance_command_count = 0;
        bool session_crash_clear_requested = false;
        int session_recovery_phase = 0;
        bool diagnostics_completed = false;
        QTimer diagnostics_timer;
        if (options.smoke_diagnostics) {
            diagnostics_timer.setInterval(10);
            QObject::connect(&diagnostics_timer, &QTimer::timeout, &application, [&] {
                const auto state = presenter.playbackState();
                if (state == PlayerPresenter::PlaybackState::Error) {
                    application.exit(2);
                    return;
                }
                if (state != PlayerPresenter::PlaybackState::Ready && state != PlayerPresenter::PlaybackState::Paused &&
                    state != PlayerPresenter::PlaybackState::Playing)
                    return;
                diagnostics_timer.stop();
                qWarning("PRIVATE_DIAGNOSTIC_QT https://example.invalid/private?secret=test");
                std::fputs("PRIVATE_DIAGNOSTIC_NATIVE\n", stderr);
                std::fflush(stderr);
                std::fputs("M0_DIAGNOSTICS_DECODER_READY\n", stdout);
                std::fflush(stdout);
                diagnostics_completed = presenter.requestExit();
                if (!diagnostics_completed)
                    application.exit(2);
            });
            diagnostics_timer.start();
            QTimer::singleShot(10'000, &application, [&] {
                if (!diagnostics_completed)
                    application.exit(2);
            });
        }
        QTimer session_restart_timer;
        QTimer session_recovery_timer;
        QTimer session_crash_timer;
        QTimer single_instance_resume_timer;
#endif
        QTimer preferences_restart_timer;
#ifdef M0_QT_COMBINED_AUDIO
        if (options.smoke_single_instance_executable) {
            QTimer* const presenter_timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
            if (presenter_timer == nullptr)
                throw std::runtime_error{"the single-instance smoke could not find the presenter timer"};
            const QString resume_barrier = QDir{config_root}.filePath(QStringLiteral("single-instance-resume-drain"));
            single_instance_resume_timer.setInterval(10);
            QObject::connect(&single_instance_resume_timer, &QTimer::timeout, &application,
                             [&application, &presenter, presenter_timer, resume_barrier, &single_instance_command_count,
                              &single_instance_resume_timer] {
                                 if (single_instance_command_count != 2 || !QFileInfo::exists(resume_barrier))
                                     return;
                                 single_instance_resume_timer.stop();
                                 if (!presenter.exiting() || presenter.exitCommitted()) {
                                     qCritical()
                                         << "The single-instance smoke lost its reversible exit before drain resume";
                                     application.exit(2);
                                     return;
                                 }
                                 presenter_timer->start();
                             });
            single_instance_resume_timer.start();
            QObject::connect(
                &single_instance, &music_player::m0::QtSingleInstanceCoordinator::commandAvailable, &application,
                [&application, window, &presenter, &source_dispatcher, presenter_timer,
                 &single_instance_command_count] {
                    ++single_instance_command_count;
                    if (single_instance_command_count == 1) {
                        const bool restored =
                            window->isVisible() && !window->windowStates().testFlag(Qt::WindowMinimized);
                        if (!restored || !presenter.sourceImportActive() ||
                            source_dispatcher.pendingCommandCount() != 0) {
                            qCritical() << "The single-instance smoke could not hold its first active import";
                            application.exit(2);
                            return;
                        }
                        presenter_timer->stop();
                        std::fputs("M0_SINGLE_INSTANCE_IPC_RESTORED\n", stdout);
                        std::fputs("M0_SINGLE_INSTANCE_IMPORT_HELD 1\n", stdout);
                        std::fflush(stdout);
                        return;
                    }
                    if (single_instance_command_count == 2) {
                        const bool second_queued =
                            presenter.sourceImportActive() && source_dispatcher.pendingCommandCount() == 1;
                        const bool exit_requested = second_queued && presenter.requestExit();
                        if (!exit_requested || !presenter.exiting() || presenter.exitCommitted()) {
                            qCritical() << "The single-instance smoke did not enter reversible source draining"
                                        << "secondQueued" << second_queued << "exitRequested" << exit_requested;
                            application.exit(2);
                            return;
                        }
                        presenter_timer->stop();
                        std::fputs("M0_SINGLE_INSTANCE_EXIT_DRAINING 2\n", stdout);
                        std::fflush(stdout);
                        return;
                    }
                    qCritical() << "The single-instance smoke accepted a command after the exit intent";
                    application.exit(2);
                },
                Qt::DirectConnection);
            QObject::connect(
                &presenter, &PlayerPresenter::sourceImportChanged, &application,
                [&application, window, &presenter, &single_instance_completed, &single_instance_command_count] {
                    if (presenter.sourceImportActive() || single_instance_completed)
                        return;
                    QAbstractItemModel* const queue_model = presenter.queueModelObject();
                    const QFileInfo source_info{presenter.detailsFilePath()};
                    const bool restored = window->isVisible() && !(window->windowStates() & Qt::WindowMinimized);
                    const bool imported =
                        single_instance_command_count == 2 && presenter.exiting() && !presenter.exitCommitted() &&
                        queue_model != nullptr && queue_model->rowCount() == 2 &&
                        presenter.sourceImportAcceptedCount() == 1 && presenter.sourceImportIssueCount() == 0 &&
                        presenter.hasDetailsFilePath() && source_info.isAbsolute() && source_info.exists() &&
                        source_info.fileName() == QStringLiteral("A ativa & abertura; 日本 (01) 'tema'.wav");
                    if (!restored || !imported) {
                        qCritical() << "The primary shell did not drain both forwarded sources"
                                    << "restored" << restored << "imported" << imported;
                        application.exit(2);
                        return;
                    }
                    single_instance_completed = true;
                    std::fputs("M0_SINGLE_INSTANCE_COMPLETED\n", stdout);
                    std::fflush(stdout);
                });
            if (!presenter.startMinimized() || window->isVisible())
                throw std::runtime_error{"the start-minimized primary became visible before forwarding"};
            std::fputs("M0_SINGLE_INSTANCE_STARTUP_HIDDEN\n", stdout);
            std::fflush(stdout);
            if (!presenter.requestHide())
                throw std::runtime_error{"the single-instance smoke could not hide its primary window"};
            QCoreApplication::processEvents();
            if (window->isVisible() && !(window->windowStates() & Qt::WindowMinimized))
                throw std::runtime_error{"the single-instance smoke primary remained active before forwarding"};
            const QByteArray endpoint = single_instance.serverName().toUtf8();
            if (endpoint.isEmpty())
                throw std::runtime_error{"the single-instance smoke primary has no IPC endpoint"};
            std::fprintf(stdout, "M0_SINGLE_INSTANCE_READY %s\n", endpoint.constData());
            std::fflush(stdout);
            QTimer::singleShot(15'000, &application, [&application, &single_instance_completed] {
                if (!single_instance_completed) {
                    qCritical() << "The single-instance executable smoke did not complete before its watchdog";
                    application.exit(2);
                }
            });
        }
#endif
        if (options.smoke_tray_lifecycle) {
            QTimer::singleShot(
                0, &application,
                [&application, window, &presenter, &platform, &tray_smoke_available, &tray_smoke_completed] {
                    tray_smoke_completed = music_player::m0::experiment::runShellLifecycleSmoke(
                        *window, presenter, platform, tray_smoke_available);
                    if (!tray_smoke_completed) {
                        qCritical() << "The shell did not complete its close, tray, restore and exit lifecycle";
                        application.exit(2);
                    }
                });
            QTimer::singleShot(3000, &application, [&application] {
                qCritical() << "The tray exit action did not terminate the shell gracefully";
                application.exit(2);
            });
        }
        if (options.smoke_error_recovery) {
            QPointer<QObject> root = engine.rootObjects().constFirst();
            QTimer::singleShot(0, &application, [&application, root, &presenter]() {
                const bool passed = root != nullptr && runErrorRecoverySmoke(*root, presenter);
                if (!passed)
                    qCritical() << "The frozen unavailable scenario did not recover through the real QML action";
                application.exit(passed ? 0 : 2);
            });
        }
#ifdef M0_QT_COMBINED_AUDIO
        if (options.smoke_combined_startup) {
            QPointer<QObject> root = engine.rootObjects().constFirst();
            QTimer::singleShot(
                0, &application, [&application, root, window, &presenter, &combined_startup_smoke_completed] {
                    QObject* const empty_state =
                        root != nullptr ? root->findChild<QObject*>(QStringLiteral("emptyState")) : nullptr;
                    combined_startup_smoke_completed = root != nullptr && window->isVisible() &&
                                                       !presenter.usesFrozenFixture() && !presenter.hasCurrentTrack() &&
                                                       presenter.queueModelObject() != nullptr &&
                                                       presenter.queueModelObject()->rowCount() == 0 &&
                                                       isVisible(empty_state) && presenter.requestExit();
                    if (!combined_startup_smoke_completed) {
                        qCritical() << "The combined audio shell did not start in its real empty state";
                        application.exit(2);
                    }
                });
            QTimer::singleShot(3000, &application, [&application] {
                qCritical() << "The combined audio shell did not exit through its production lifecycle";
                application.exit(2);
            });
        }
        if (options.smoke_startup_source_precedence) {
            QTimer::singleShot(0, &application, [&application, window, &presenter, &startup_behavior_completed] {
                const bool visible = window->isVisible() && !window->windowStates().testFlag(Qt::WindowMinimized);
                if (!visible || !presenter.startMinimized()) {
                    qCritical() << "An explicit startup source did not reveal the shell";
                    application.exit(2);
                    return;
                }
                std::fputs("M0_STARTUP_EXPLICIT_SOURCE_VISIBLE\n", stdout);
                std::fflush(stdout);
                startup_behavior_completed = presenter.requestExit();
                if (!startup_behavior_completed)
                    application.exit(2);
            });
            QTimer::singleShot(10'000, &application, [&application] {
                qCritical() << "The explicit-source startup smoke did not exit before its watchdog";
                application.exit(2);
            });
        }
#endif
        if (options.smoke_preferences_restart_write) {
            QTimer::singleShot(
                0, &application,
                [&application, window, &presenter, &preferences_restart_completed, &preferences_restart_timer] {
                    if (presenter.startMinimized() || !window->isVisible() ||
                        window->windowStates().testFlag(Qt::WindowMinimized)) {
                        qCritical() << "The default startup preference did not reveal the shell";
                        application.exit(2);
                        return;
                    }
                    std::fputs("M0_STARTUP_PREFERENCE_OFF_VISIBLE\n", stdout);
                    std::fflush(stdout);
                    const bool written = writeRestartPreferences(presenter);
                    if (!written) {
                        application.exit(2);
                        return;
                    }
                    const auto placement = restartPreferences().window_placement.value();
                    window->setGeometry(placement.x, placement.y, placement.width, placement.height);
                    preferences_restart_timer.setInterval(10);
                    QObject::connect(
                        &preferences_restart_timer, &QTimer::timeout, &application,
                        [&application, &presenter, &preferences_restart_completed, &preferences_restart_timer] {
                            if (presenter.preferences() != restartPreferences())
                                return;
                            preferences_restart_timer.stop();
                            const bool exited = presenter.requestExit();
                            preferences_restart_completed =
                                exited &&
                                presenter.preferenceWarning() != PlayerPresenter::PreferenceWarning::SaveFailed;
                            if (!preferences_restart_completed)
                                application.exit(2);
                        });
                    preferences_restart_timer.start();
                });
        }
        if (options.smoke_preferences_restart_read) {
            QTimer::singleShot(
                0, &application,
                [&application, window, &platform, &contract, &presenter, &preferences_restart_completed] {
                    QAction* const show_hide = platform.menu()->findChild<QAction*>(QStringLiteral("tray-show-hide"));
                    const bool initially_hidden = !window->isVisible();
                    if (show_hide != nullptr)
                        show_hide->trigger();
                    QCoreApplication::processEvents();
                    const bool restored = window->isVisible() && !window->windowStates().testFlag(Qt::WindowMinimized);
                    if (show_hide != nullptr)
                        show_hide->trigger();
                    QCoreApplication::processEvents();
                    const bool hidden_again = !window->isVisible();
                    if (show_hide != nullptr)
                        show_hide->trigger();
                    QCoreApplication::processEvents();
                    const bool restored_again =
                        window->isVisible() && !window->windowStates().testFlag(Qt::WindowMinimized);
                    const bool state_matches =
                        presenter.preferences() == restartPreferences() && contract.darkTheme() && initially_hidden &&
                        restored && hidden_again && restored_again && window->geometry() == QRect{20, 30, 720, 520} &&
                        window->property("color").value<QColor>() == QColor{contract.canvasColor()};
                    if (state_matches) {
                        std::fputs("M0_STARTUP_MINIMIZED_TRAY_ROUNDTRIP\n", stdout);
                        std::fflush(stdout);
                    }
                    preferences_restart_completed = state_matches && presenter.requestExit();
                    if (!preferences_restart_completed)
                        application.exit(2);
                });
        }
        if (options.smoke_preferences_restart_write || options.smoke_preferences_restart_read) {
            QTimer::singleShot(3000, &application, [&application] { application.exit(2); });
        }
        if (options.smoke_start_minimized_no_tray) {
            QTimer::singleShot(
                0, &application, [&application, window, &platform, &presenter, &startup_behavior_completed] {
                    const bool visible = window->isVisible() && !window->windowStates().testFlag(Qt::WindowMinimized);
                    const bool warned =
                        presenter.windowNotice() == PlayerPresenter::WindowNotice::TrayUnavailableNotice;
                    const bool state_matches =
                        presenter.startMinimized() && !platform.trayAvailable() && visible && warned;
                    if (state_matches) {
                        std::fputs("M0_STARTUP_NO_TRAY_VISIBLE_WARNING\n", stdout);
                        std::fflush(stdout);
                    }
                    startup_behavior_completed = state_matches && presenter.requestExit();
                    if (!startup_behavior_completed)
                        application.exit(2);
                });
            QTimer::singleShot(3'000, &application, [&application] {
                qCritical() << "The no-tray startup smoke did not exit before its watchdog";
                application.exit(2);
            });
        }
#ifdef M0_QT_COMBINED_AUDIO
        if (options.smoke_session_restart_read) {
            session_restart_timer.setInterval(10);
            QObject::connect(&session_restart_timer, &QTimer::timeout, &application,
                             [&application, &presenter, &session_restart_completed, &session_restart_timer] {
                                 if (presenter.playbackState() == PlayerPresenter::PlaybackState::Error) {
                                     qCritical() << "The persisted session became unavailable during restoration";
                                     application.exit(2);
                                     return;
                                 }
                                 if (presenter.playbackState() != PlayerPresenter::PlaybackState::Paused ||
                                     presenter.confirmedPositionMs() != 750 || presenter.seekPending()) {
                                     return;
                                 }
                                 session_restart_timer.stop();
                                 session_restart_completed = presenter.requestExit();
                                 if (!session_restart_completed)
                                     application.exit(2);
                             });
            session_restart_timer.start();
            QTimer::singleShot(5000, &application, [&application, &session_restart_completed] {
                if (!session_restart_completed) {
                    qCritical() << "The persisted session did not settle paused at its confirmed position";
                    application.exit(2);
                }
            });
        }
        if (options.smoke_session_crash_before_primary_commit) {
            session_crash_timer.setInterval(10);
            QObject::connect(&session_crash_timer, &QTimer::timeout, &application,
                             [&application, &presenter, &session_crash_clear_requested, &session_crash_gate_armed,
                              &session_crash_timer] {
                                 if (presenter.playbackState() == PlayerPresenter::PlaybackState::Error) {
                                     qCritical() << "The persisted session became unavailable before the crash gate";
                                     application.exit(2);
                                     return;
                                 }
                                 if (session_crash_clear_requested ||
                                     presenter.playbackState() != PlayerPresenter::PlaybackState::Paused ||
                                     presenter.confirmedPositionMs() < 700 || presenter.confirmedPositionMs() > 800 ||
                                     presenter.seekPending()) {
                                     return;
                                 }
                                 session_crash_clear_requested = presenter.clearQueue();
                                 session_crash_gate_armed.store(session_crash_clear_requested,
                                                                std::memory_order_release);
                                 if (!session_crash_clear_requested) {
                                     qCritical() << "The crash smoke could not schedule its structural checkpoint";
                                     application.exit(2);
                                     return;
                                 }
                                 session_crash_timer.stop();
                             });
            session_crash_timer.start();
        }
        if (options.smoke_session_recovery_repair) {
            QPointer<QObject> root = engine.rootObjects().constFirst();
            session_recovery_timer.setInterval(10);
            QObject::connect(&session_recovery_timer, &QTimer::timeout, &application,
                             [&application, root, &presenter, &session_recovery_completed, &session_recovery_timer,
                              &session_recovery_phase, config_root] {
                                 if (presenter.playbackState() == PlayerPresenter::PlaybackState::Error) {
                                     qCritical() << "The recovered session became unavailable during repair";
                                     application.exit(2);
                                     return;
                                 }
                                 const bool restored_paused =
                                     presenter.playbackState() == PlayerPresenter::PlaybackState::Paused &&
                                     presenter.confirmedPositionMs() == 750 && !presenter.seekPending();
                                 if (session_recovery_phase == 0) {
                                     if (!restored_paused || presenter.preferenceWarning() !=
                                                                 PlayerPresenter::PreferenceWarning::SaveFailed) {
                                         return;
                                     }
                                     std::fputs("M0_SESSION_REPLACE_FAILED\n", stdout);
                                     std::fflush(stdout);
                                     session_recovery_phase = 1;
                                     return;
                                 }
                                 if (session_recovery_phase == 1) {
                                     if (!QFile::exists(QDir{config_root}.filePath(QStringLiteral("m0-session-retry"))))
                                         return;
                                     QObject* const retry =
                                         root != nullptr
                                             ? root->findChild<QObject*>(QStringLiteral("preference-persistence-retry"))
                                             : nullptr;
                                     if (retry == nullptr || !retry->property("visible").toBool() ||
                                         !QMetaObject::invokeMethod(retry, "clicked", Qt::DirectConnection)) {
                                         qCritical() << "The real persistence retry action was not available";
                                         application.exit(2);
                                         return;
                                     }
                                     session_recovery_phase = 2;
                                     return;
                                 }
                                 if (!restored_paused || presenter.preferenceWarning() !=
                                                             PlayerPresenter::PreferenceWarning::NoPreferenceWarning) {
                                     return;
                                 }
                                 session_recovery_timer.stop();
                                 session_recovery_completed = presenter.requestExit();
                                 if (!session_recovery_completed)
                                     application.exit(2);
                             });
            session_recovery_timer.start();
            QTimer::singleShot(10'000, &application, [&application, &session_recovery_completed] {
                if (!session_recovery_completed) {
                    qCritical() << "The recovered session did not complete its real replace retry";
                    application.exit(2);
                }
            });
        }
        if (options.smoke_session_defaults_read) {
            QPointer<QObject> root = engine.rootObjects().constFirst();
            QTimer::singleShot(0, &application, [&application, root, &presenter, &session_defaults_completed] {
                QObject* const dismiss =
                    root != nullptr ? root->findChild<QObject*>(QStringLiteral("preference-persistence-dismiss"))
                                    : nullptr;
                const bool dismissed =
                    dismiss != nullptr && dismiss->property("visible").toBool() &&
                    QMetaObject::invokeMethod(dismiss, "clicked", Qt::DirectConnection) &&
                    presenter.preferenceWarning() == PlayerPresenter::PreferenceWarning::NoPreferenceWarning;
                session_defaults_completed = dismissed && presenter.requestExit();
                if (!session_defaults_completed)
                    application.exit(2);
            });
            QTimer::singleShot(3'000, &application, [&application, &session_defaults_completed] {
                if (!session_defaults_completed)
                    application.exit(2);
            });
        }
#endif
#endif
        const int result = application.exec();
#ifdef Q_OS_WIN
        if (diagnostics)
            static_cast<void>(diagnostics->submit(
                {music_player::m0::DiagnosticSeverity::Info, music_player::m0::DiagnosticEvent::ApplicationStopping}));
#endif
#ifdef M0_QT_COMBINED_AUDIO
        stop_single_instance();
#endif
#ifdef M0_QT_SHELL_TEST_HOOKS
        if (options.smoke_tray_lifecycle &&
            (result != 0 || !tray_smoke_completed || !presenter.exiting() || window->isVisible())) {
            qCritical() << "The shell exited without completing the real, latched window close:"
                        << "result" << result << "completed" << tray_smoke_completed << "exiting" << presenter.exiting()
                        << "visible" << window->isVisible();
            return 2;
        }
        if (options.smoke_combined_startup &&
            (result != 0 || !combined_startup_smoke_completed || !presenter.exiting() || window->isVisible())) {
            qCritical() << "The combined audio shell exited without completing its startup smoke:"
                        << "result" << result << "completed" << combined_startup_smoke_completed << "exiting"
                        << presenter.exiting() << "visible" << window->isVisible();
            return 2;
        }
        if ((options.smoke_preferences_restart_write || options.smoke_preferences_restart_read) &&
            (result != 0 || !preferences_restart_completed || !presenter.exiting())) {
            qCritical() << "The preferences restart smoke did not complete";
            return 2;
        }
        if ((options.smoke_start_minimized_no_tray || options.smoke_startup_source_precedence) &&
            (result != 0 || !startup_behavior_completed || !presenter.exitCommitted())) {
            qCritical() << "The startup behavior smoke did not complete";
            return 2;
        }
#ifdef M0_QT_COMBINED_AUDIO
        if (options.smoke_single_instance_executable &&
            (result != 0 || !single_instance_completed || !presenter.exitCommitted())) {
            qCritical() << "The single-instance executable smoke did not complete its graceful exit";
            return 2;
        }
        if (options.smoke_diagnostics && (result != 0 || !diagnostics_completed || !presenter.exitCommitted()))
            return 2;
        if (options.smoke_session_restart_read && (result != 0 || !session_restart_completed || !presenter.exiting())) {
            qCritical() << "The session restart smoke did not complete";
            return 2;
        }
        if (options.smoke_session_recovery_repair &&
            (result != 0 || !session_recovery_completed || !presenter.exiting())) {
            qCritical() << "The session recovery smoke did not complete";
            return 2;
        }
        if (options.smoke_session_defaults_read &&
            (result != 0 || !session_defaults_completed || !presenter.exiting())) {
            qCritical() << "The invalid-session defaults smoke did not complete";
            return 2;
        }
        if (options.smoke_session_crash_before_primary_commit) {
            qCritical() << "The forced-termination smoke exited without being killed at its transaction gate";
            return 2;
        }
#endif
#endif
        return result;
    } catch (const std::exception& error) {
#ifdef Q_OS_WIN
        if (diagnostics)
            static_cast<void>(diagnostics->submit({music_player::m0::DiagnosticSeverity::Error,
                                                   music_player::m0::DiagnosticEvent::StartupFailed,
                                                   music_player::m0::DiagnosticCode::Unknown}));
#endif
        qCritical() << "Unable to start the frozen M0 shell:" << error.what();
        return 1;
    }
}
