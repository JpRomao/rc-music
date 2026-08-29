#include "ShellLaunchOptions.hpp"
#include "adapters/QtShellPlatform.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/LifecycleText.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
#include "presentation/ShellPlatformBinding.hpp"
#include "presentation/ShellUiContract.hpp"

#ifdef M0_QT_COMBINED_AUDIO
#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"
#include "m0/adapters/SingleWorkerMetadataReader.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <QFileDialog>
#endif

#ifdef M0_QT_SHELL_TEST_HOOKS
#include "ShellLifecycleSmoke.hpp"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>
#include <QWindow>

#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

using music_player::m0::FixtureRepository;
using music_player::m0::PlayerPresenter;
using music_player::m0::ShellUiContract;
using music_player::m0::experiment::InitialShellScenario;
using music_player::m0::experiment::parseShellLaunchOptions;
using music_player::m0::experiment::PlayerPresenterScenarioControl;

namespace {
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
#endif
} // namespace

int main(int argc, char* argv[]) {
    Q_INIT_RESOURCE(m0_qt_shell_assets);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        const auto options = parseShellLaunchOptions(application.arguments());
#ifndef M0_QT_SHELL_TEST_HOOKS
        if (options.initial_scenario != InitialShellScenario::Empty || options.smoke_error_recovery ||
            options.smoke_tray_lifecycle || options.smoke_combined_startup)
            throw std::invalid_argument{"M0 shell scenario controls are unavailable in this build"};
#endif
#ifndef M0_QT_COMBINED_AUDIO
        if (options.smoke_combined_startup)
            throw std::invalid_argument{"the combined startup smoke requires audio support"};
#endif
        const auto bundle = FixtureRepository{}.load(QStringLiteral(":/m0/data/fixture.v1.json"),
                                                     QStringLiteral(":/m0/data/queue.v1.json"));
        ShellUiContract contract(bundle);
        music_player::m0::LifecycleText lifecycle_text;
        std::function<bool()> availability_reader;
#ifdef M0_QT_SHELL_TEST_HOOKS
        bool tray_smoke_available = true;
        if (options.smoke_tray_lifecycle)
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
        music_player::m0::QtMultimediaPlaybackBackend backend;
        music_player::m0::SingleWorkerMetadataReader metadata_reader(
            std::make_unique<music_player::m0::TagLibMetadataExtractor>());
        PlayerPresenter::AudioFilePicker audio_file_picker = [&contract]() -> std::optional<std::filesystem::path> {
            const QString selected =
                QFileDialog::getOpenFileName(nullptr, contract.openAudioText(), {},
                                             QStringLiteral("Áudio local (*.mp3 *.wav);;Todos os arquivos (*)"));
            if (selected.isEmpty())
                return std::nullopt;
#ifdef Q_OS_WIN
            return std::filesystem::path{selected.toStdWString()};
#else
            return std::filesystem::path{selected.toUtf8().toStdString()};
#endif
        };
        PlayerPresenter presenter(bundle, platform, backend, metadata_reader, std::move(audio_file_picker));
#else
        PlayerPresenter presenter(bundle, platform);
#endif
        // Destroy QML roots before the C++ objects referenced by their required properties.
        QQmlApplicationEngine engine;
        music_player::m0::bindShellAppearance(presenter, contract, system_appearance);
        if (options.initial_scenario == InitialShellScenario::Unavailable &&
            (!presenter.loadFrozenQueue() || !PlayerPresenterScenarioControl::enterUnavailable(presenter))) {
            throw std::runtime_error{"unable to enter the frozen unavailable scenario"};
        }
        engine.setInitialProperties({{QStringLiteral("uiContract"), QVariant::fromValue(&contract)},
                                     {QStringLiteral("presenter"), QVariant::fromValue(&presenter)}});
        engine.loadFromModule(QStringLiteral("MusicPlayer.M0"), QStringLiteral("Main"));
        if (engine.rootObjects().isEmpty())
            return 1;
        auto* const window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
        if (window == nullptr)
            return 1;
        music_player::m0::bindShellPlatform(presenter, platform, *window);
#ifdef M0_QT_SHELL_TEST_HOOKS
        bool tray_smoke_completed = false;
        bool combined_startup_smoke_completed = false;
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
#endif
#endif
        const int result = application.exec();
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
#endif
        return result;
    } catch (const std::exception& error) {
        qCritical() << "Unable to start the frozen M0 shell:" << error.what();
        return 1;
    }
}
