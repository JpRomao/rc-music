#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
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
    ScenarioDriver(music_player::m0::PlayerPresenter& presenter, music_player::m0::QtSystemAppearance& appearance,
                   std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state,
                   QObject* parent = nullptr)
        : QObject(parent), presenter_(presenter), appearance_(appearance),
          appearance_state_(std::move(appearance_state)) {}
    Q_INVOKABLE bool enterUnavailable() {
        return music_player::m0::experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter_);
    }
    Q_INVOKABLE qulonglong seekCommitAttemptCount() const noexcept {
        return static_cast<qulonglong>(
            music_player::m0::test::PlayerPresenterTestAccess::seekCommitAttemptCount(presenter_));
    }
    Q_INVOKABLE void setSystemAppearance(const bool dark, const bool reduced_motion) {
        *appearance_state_ = {dark, reduced_motion};
        appearance_.refresh();
    }

  private:
    music_player::m0::PlayerPresenter& presenter_;
    music_player::m0::QtSystemAppearance& appearance_;
    std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state_;
};

class EmptyShellQuickTestSetup final : public QObject {
    Q_OBJECT

  public slots:
    void applicationAvailable() {
        Q_INIT_RESOURCE(m0_qt_shell_assets);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        bundle_ = std::make_unique<music_player::m0::FixtureBundle>(music_player::m0::FixtureRepository{}.load(
            QStringLiteral(":/m0/data/fixture.v1.json"), QStringLiteral(":/m0/data/queue.v1.json")));
    }

    void qmlEngineAvailable(QQmlEngine* engine) {
        auto* contract = new music_player::m0::ShellUiContract(*bundle_, engine);
        auto* presenter = new music_player::m0::PlayerPresenter(*bundle_, platform_, engine);
        auto appearance_state = std::make_shared<music_player::m0::SystemAppearanceSnapshot>(false, false);
        auto* appearance =
            new music_player::m0::QtSystemAppearance([appearance_state] { return *appearance_state; }, engine);
        music_player::m0::bindShellAppearance(*presenter, *contract, *appearance);
        engine->rootContext()->setContextProperty(QStringLiteral("testShellUiContract"), contract);
        engine->rootContext()->setContextProperty(QStringLiteral("testPlayerPresenter"), presenter);
        engine->rootContext()->setContextProperty(
            QStringLiteral("testScenarioDriver"),
            new ScenarioDriver(*presenter, *appearance, appearance_state, engine));
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
    QuickTestPlatform platform_;
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
