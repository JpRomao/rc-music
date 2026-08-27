#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/ShellAppearanceBinding.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QtQuickTest/quicktest.h>
#include <QtTest/qtestkeyboard.h>

#include <memory>

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

class ScenarioDriver final : public QObject {
    Q_OBJECT
  public:
    ScenarioDriver(music_player::m0::PlayerPresenter& presenter, QuickTestPlatform& platform,
                   music_player::m0::QtSystemAppearance& appearance,
                   std::shared_ptr<music_player::m0::SystemAppearanceSnapshot> appearance_state,
                   QObject* parent = nullptr)
        : QObject(parent), presenter_(presenter), platform_(platform), appearance_(appearance),
          appearance_state_(std::move(appearance_state)) {}
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
        platform_ = QuickTestPlatform{};
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
            new ScenarioDriver(*presenter, platform_, *appearance, appearance_state, engine));
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
    QuickTestPlatform platform_;
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
