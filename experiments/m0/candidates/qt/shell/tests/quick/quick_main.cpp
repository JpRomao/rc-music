#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QtQuickTest/quicktest.h>

#include <memory>

namespace {

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
        engine->rootContext()->setContextProperty(QStringLiteral("testShellUiContract"), contract);
    }

  private:
    std::unique_ptr<music_player::m0::FixtureBundle> bundle_;
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(m0_qt_empty_shell, EmptyShellQuickTestSetup)

#include "quick_main.moc"
