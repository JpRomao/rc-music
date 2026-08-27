#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariant>

#include <exception>

using music_player::m0::FixtureRepository;
using music_player::m0::ShellUiContract;

int main(int argc, char* argv[]) {
    Q_INIT_RESOURCE(m0_qt_shell_assets);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication application(argc, argv);
    try {
        const auto bundle = FixtureRepository{}.load(QStringLiteral(":/m0/data/fixture.v1.json"),
                                                     QStringLiteral(":/m0/data/queue.v1.json"));
        QQmlApplicationEngine engine;
        ShellUiContract contract(bundle, &engine);
        engine.setInitialProperties({{QStringLiteral("uiContract"), QVariant::fromValue(&contract)}});
        engine.loadFromModule(QStringLiteral("MusicPlayer.M0"), QStringLiteral("Main"));
        if (engine.rootObjects().isEmpty())
            return 1;
        return application.exec();
    } catch (const std::exception& error) {
        qCritical() << "Unable to start the frozen M0 shell:" << error.what();
        return 1;
    }
}
