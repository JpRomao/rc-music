#include "ShellLaunchOptions.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>
#include <QWindow>

#include <exception>
#include <stdexcept>

using music_player::m0::FixtureRepository;
using music_player::m0::PlayerPresenter;
using music_player::m0::ShellUiContract;
using music_player::m0::experiment::InitialShellScenario;
using music_player::m0::experiment::parseShellLaunchOptions;
using music_player::m0::experiment::PlayerPresenterScenarioControl;

namespace {
class ShellPlatform final : public music_player::m0::PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return false; }
    void bindWindow(QWindow& window) noexcept { window_ = &window; }
    void hideWindow() override {
        if (window_ != nullptr)
            window_->hide();
    }
    void minimizeWindow() override {
        if (window_ != nullptr)
            window_->showMinimized();
    }
    void showAndActivateWindow() override {
        if (window_ == nullptr)
            return;
        window_->showNormal();
        window_->requestActivate();
    }
    void requestExit() override { QGuiApplication::quit(); }

  private:
    QPointer<QWindow> window_;
};

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
    QGuiApplication application(argc, argv);
    try {
        const auto options = parseShellLaunchOptions(application.arguments());
#ifndef M0_QT_SHELL_TEST_HOOKS
        if (options.initial_scenario != InitialShellScenario::Empty || options.smoke_error_recovery)
            throw std::invalid_argument{"M0 shell scenario controls are unavailable in this build"};
#endif
        const auto bundle = FixtureRepository{}.load(QStringLiteral(":/m0/data/fixture.v1.json"),
                                                     QStringLiteral(":/m0/data/queue.v1.json"));
        QQmlApplicationEngine engine;
        ShellUiContract contract(bundle, &engine);
        ShellPlatform platform;
        PlayerPresenter presenter(bundle, platform, &engine);
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
        platform.bindWindow(*window);
#ifdef M0_QT_SHELL_TEST_HOOKS
        if (options.smoke_error_recovery) {
            QPointer<QObject> root = engine.rootObjects().constFirst();
            QTimer::singleShot(0, &application, [&application, root, &presenter]() {
                const bool passed = root != nullptr && runErrorRecoverySmoke(*root, presenter);
                if (!passed)
                    qCritical() << "The frozen unavailable scenario did not recover through the real QML action";
                application.exit(passed ? 0 : 2);
            });
        }
#endif
        return application.exec();
    } catch (const std::exception& error) {
        qCritical() << "Unable to start the frozen M0 shell:" << error.what();
        return 1;
    }
}
