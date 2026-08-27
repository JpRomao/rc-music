#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QtQuickTest/quicktest.h>

#include <memory>

namespace {

class QuickTestPlatform final : public music_player::m0::PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return false; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}
};

class ScenarioDriver final : public QObject {
    Q_OBJECT
  public:
    explicit ScenarioDriver(music_player::m0::PlayerPresenter& presenter, QObject* parent = nullptr)
        : QObject(parent), presenter_(presenter) {}
    Q_INVOKABLE bool enterUnavailable() {
        return music_player::m0::experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter_);
    }
    Q_INVOKABLE qulonglong seekCommitAttemptCount() const noexcept {
        return static_cast<qulonglong>(
            music_player::m0::test::PlayerPresenterTestAccess::seekCommitAttemptCount(presenter_));
    }

  private:
    music_player::m0::PlayerPresenter& presenter_;
};

class EmptyShellQuickTestSetup final : public QObject {
    Q_OBJECT

  public slots:
    void applicationAvailable() {
        Q_INIT_RESOURCE(m0_qt_shell_assets);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        bundle_ = std::make_unique<music_player::m0::FixtureBundle>(music_player::m0::FixtureRepository{}.load(
            QStringLiteral(":/m0/data/fixture.v1.json"), QStringLiteral(":/m0/data/queue.v1.json")));
        presenter_ = std::make_unique<music_player::m0::PlayerPresenter>(*bundle_, platform_);
    }

    void qmlEngineAvailable(QQmlEngine* engine) {
        auto* contract = new music_player::m0::ShellUiContract(*bundle_, engine);
        engine->rootContext()->setContextProperty(QStringLiteral("testShellUiContract"), contract);
        engine->rootContext()->setContextProperty(QStringLiteral("testPlayerPresenter"), presenter_.get());
        engine->rootContext()->setContextProperty(QStringLiteral("testScenarioDriver"),
                                                  new ScenarioDriver(*presenter_, engine));
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
    QuickTestPlatform platform_;
    std::unique_ptr<music_player::m0::PlayerPresenter> presenter_;
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
