#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMetaProperty>
#include <QStringList>
#include <QTest>
#include <QtQml/qqml.h>

#include <stdexcept>

namespace music_player::m0::test {
namespace {

const QString fixture_resource_path = QStringLiteral(":/m0/data/fixture.v1.json");
const QString queue_resource_path = QStringLiteral(":/m0/data/queue.v1.json");

} // namespace

class ShellUiContractTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void exposesFrozenConstantsAndAuthenticatedResources();
    void rejectsUnknownAssetMappings();
    void rejectsUnsupportedTransportStrings();
};

void ShellUiContractTests::initTestCase() { Q_INIT_RESOURCE(m0_qt_shell_assets); }

void ShellUiContractTests::exposesFrozenConstantsAndAuthenticatedResources() {
    QVERIFY(qmlTypeId("MusicPlayer.M0", 1, 0, "ShellUiContract") >= 0);
    QVERIFY(qmlTypeId("MusicPlayer.M0", 1, 0, "PlayerPresenter") >= 0);
    QVERIFY(QFile::exists(fixture_resource_path));
    QVERIFY(QFile::exists(queue_resource_path));
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    QCOMPARE(contract.clientWidth(), 960);
    QCOMPARE(contract.clientHeight(), 640);
    QCOMPARE(contract.minimumWidth(), 720);
    QCOMPARE(contract.minimumHeight(), 520);
    QCOMPARE(contract.controlTargetMinimum(), 40);
    QCOMPARE(contract.controlTargetPreferred(), 44);
    QCOMPARE(contract.radiusSmall(), 8);
    QCOMPARE(contract.radiusMedium(), 12);
    QCOMPARE(contract.spacingUnit(), 4);
    QCOMPARE(contract.appName(), QStringLiteral("Player de música"));
    QCOMPARE(contract.openAudioText(), QStringLiteral("Abrir áudio"));
    QCOMPARE(contract.emptyTitle(), QStringLiteral("Abra um áudio para começar"));
    QCOMPARE(contract.emptyHint(), QStringLiteral("ou arraste arquivos para esta janela"));
    QCOMPARE(contract.actionPlay(), QStringLiteral("Reproduzir"));
    QCOMPARE(contract.actionPause(), QStringLiteral("Pausar"));
    QCOMPARE(contract.actionPrevious(), QStringLiteral("Faixa anterior"));
    QCOMPARE(contract.actionNext(), QStringLiteral("Próxima faixa"));
    QCOMPARE(contract.skipBackwardShort(), QStringLiteral("−{seconds}"));
    QCOMPARE(contract.skipForwardShort(), QStringLiteral("+{seconds}"));
    QCOMPARE(contract.skipBackwardName(), QStringLiteral("Voltar {seconds} segundos"));
    QCOMPARE(contract.skipForwardName(), QStringLiteral("Avançar {seconds} segundos"));
    QCOMPARE(contract.progressName(), QStringLiteral("Posição da faixa"));
    QCOMPARE(contract.volumeName(), QStringLiteral("Volume"));
    QCOMPARE(contract.actionMute(), QStringLiteral("Silenciar"));
    QCOMPARE(contract.actionUnmute(), QStringLiteral("Ativar som"));
    QCOMPARE(contract.errorTitle(), QStringLiteral("Não foi possível abrir esta faixa"));
    QCOMPARE(contract.errorBody(), QStringLiteral("A faixa de teste está indisponível."));
    QCOMPARE(contract.actionSkip(), QStringLiteral("Pular"));
    QCOMPARE(contract.canvasColor(), QStringLiteral("#F6F7F9"));
    QCOMPARE(contract.surfaceColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(contract.textPrimaryColor(), QStringLiteral("#15171A"));
    QCOMPARE(contract.textSecondaryColor(), QStringLiteral("#4F5660"));
    QCOMPARE(contract.accentColor(), QStringLiteral("#4F46E5"));
    QCOMPARE(contract.accentTextColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(contract.focusColor(), QStringLiteral("#2457D6"));
    QCOMPARE(contract.borderColor(), QStringLiteral("#8A929E"));
    QCOMPARE(contract.placeholderSource(), QUrl(QStringLiteral("qrc:/m0/assets/cover-placeholder.svg")));
    QCOMPARE(contract.trackSource(), QUrl(QStringLiteral("qrc:/m0/assets/cover-track.svg")));
    const QMetaObject* meta = contract.metaObject();
    const QStringList expected_properties = {
        QStringLiteral("clientWidth"),
        QStringLiteral("clientHeight"),
        QStringLiteral("minimumWidth"),
        QStringLiteral("minimumHeight"),
        QStringLiteral("controlTargetMinimum"),
        QStringLiteral("controlTargetPreferred"),
        QStringLiteral("radiusMedium"),
        QStringLiteral("radiusSmall"),
        QStringLiteral("spacingUnit"),
        QStringLiteral("appName"),
        QStringLiteral("openAudioText"),
        QStringLiteral("emptyTitle"),
        QStringLiteral("emptyHint"),
        QStringLiteral("actionPlay"),
        QStringLiteral("actionPause"),
        QStringLiteral("actionPrevious"),
        QStringLiteral("actionNext"),
        QStringLiteral("skipBackwardShort"),
        QStringLiteral("skipForwardShort"),
        QStringLiteral("skipBackwardName"),
        QStringLiteral("skipForwardName"),
        QStringLiteral("progressName"),
        QStringLiteral("volumeName"),
        QStringLiteral("actionMute"),
        QStringLiteral("actionUnmute"),
        QStringLiteral("errorTitle"),
        QStringLiteral("errorBody"),
        QStringLiteral("actionSkip"),
        QStringLiteral("canvasColor"),
        QStringLiteral("surfaceColor"),
        QStringLiteral("textPrimaryColor"),
        QStringLiteral("textSecondaryColor"),
        QStringLiteral("accentColor"),
        QStringLiteral("accentTextColor"),
        QStringLiteral("focusColor"),
        QStringLiteral("borderColor"),
        QStringLiteral("placeholderSource"),
        QStringLiteral("trackSource"),
    };
    QStringList actual_properties;
    for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
        const QMetaProperty property = meta->property(index);
        actual_properties.append(QString::fromLatin1(property.name()));
        QVERIFY(property.isReadable());
        QVERIFY(property.isConstant());
        QVERIFY(!property.isWritable());
    }
    QCOMPARE(actual_properties, expected_properties);
    QCOMPARE(meta->methodCount(), meta->methodOffset());
    QCOMPARE(meta->indexOfProperty("fixturePath"), -1);
    QCOMPARE(meta->indexOfProperty("queuePath"), -1);
    QCOMPARE(meta->indexOfProperty("backend"), -1);
    QCOMPARE(meta->indexOfProperty("filesystem"), -1);

    struct ExpectedResource final {
        QString path;
        QByteArray sha256;
        qsizetype size;
    };
    for (const ExpectedResource& resource :
         {ExpectedResource{contract.placeholderSource().toString(), QByteArray{M0_PLACEHOLDER_SHA256},
                           M0_PLACEHOLDER_SIZE},
          ExpectedResource{contract.trackSource().toString(), QByteArray{M0_TRACK_SHA256}, M0_TRACK_SIZE},
          ExpectedResource{fixture_resource_path, QByteArray{M0_FIXTURE_SHA256}, M0_FIXTURE_SIZE},
          ExpectedResource{queue_resource_path, QByteArray{M0_QUEUE_SHA256}, M0_QUEUE_SIZE}}) {
        QString resource_path = resource.path;
        QVERIFY(resource_path.startsWith(QStringLiteral("qrc:/m0/assets/")) ||
                resource_path.startsWith(QStringLiteral(":/m0/data/")));
        resource_path.replace(QStringLiteral("qrc:/"), QStringLiteral(":/"));
        QFile file(resource_path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QCOMPARE(bytes.size(), resource.size);
        QCOMPARE(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(), resource.sha256);
    }
    QStringList actual_resource_paths;
    QDirIterator resource_iterator(QStringLiteral(":/m0"), QDir::Files, QDirIterator::Subdirectories);
    while (resource_iterator.hasNext()) {
        actual_resource_paths.append(resource_iterator.next());
    }
    actual_resource_paths.sort(Qt::CaseSensitive);
    QCOMPARE(
        actual_resource_paths,
        QStringList({QStringLiteral(":/m0/assets/cover-placeholder.svg"), QStringLiteral(":/m0/assets/cover-track.svg"),
                     QStringLiteral(":/m0/data/fixture.v1.json"), QStringLiteral(":/m0/data/queue.v1.json")}));
    QVERIFY(!QFile::exists(QStringLiteral(":/m0/assets/assets/cover-placeholder.svg")));
    QVERIFY(!QFile::exists(QStringLiteral(":/m0/data/assets/cover-placeholder.svg")));
}

void ShellUiContractTests::rejectsUnknownAssetMappings() {
    QVERIFY(QFile::exists(fixture_resource_path));
    QVERIFY(QFile::exists(queue_resource_path));
    FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    bundle.assets[0].relative_path = QStringLiteral("assets/untrusted.svg");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, static_cast<void>(ShellUiContract{bundle}));

    bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    bundle.assets.removeFirst();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, static_cast<void>(ShellUiContract{bundle}));
}

void ShellUiContractTests::rejectsUnsupportedTransportStrings() {
    for (QString FixtureStrings::* const member :
         {&FixtureStrings::action_play, &FixtureStrings::action_pause, &FixtureStrings::action_previous,
          &FixtureStrings::action_next, &FixtureStrings::skip_backward_short, &FixtureStrings::skip_forward_short,
          &FixtureStrings::skip_backward_name, &FixtureStrings::skip_forward_name, &FixtureStrings::progress_name,
          &FixtureStrings::volume_name, &FixtureStrings::action_mute, &FixtureStrings::action_unmute,
          &FixtureStrings::error_title, &FixtureStrings::error_body, &FixtureStrings::action_skip}) {
        FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
        bundle.strings.*member = QStringLiteral("drifted");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, static_cast<void>(ShellUiContract{bundle}));
    }
}

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::ShellUiContractTests)
#include "ShellUiContractTests.moc"
