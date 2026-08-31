#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/domain/PlaybackTypes.hpp"
#include "presentation/HighContrastPalette.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QtQml/qqml.h>

#include <optional>
#include <stdexcept>

namespace music_player::m0::test {
namespace {

const QString fixture_resource_path = QStringLiteral(":/m0/data/fixture.v1.json");
const QString queue_resource_path = QStringLiteral(":/m0/data/queue.v1.json");

QStringList appearanceColors(const ShellUiContract& contract) {
    return {contract.canvasColor(),      contract.surfaceColor(),       contract.surfaceRaisedColor(),
            contract.textPrimaryColor(), contract.textSecondaryColor(), contract.accentColor(),
            contract.accentTextColor(),  contract.focusColor(),         contract.borderColor(),
            contract.errorColor(),       contract.warningColor(),       contract.successColor()};
}

QStringList paletteColors(const FixturePalette& palette) {
    return {palette.canvas,         palette.surface, palette.surface_raised, palette.text_primary,
            palette.text_secondary, palette.accent,  palette.on_accent,      palette.focus,
            palette.border,         palette.error,   palette.warning,        palette.success};
}

QStringList highContrastColors(const HighContrastPalette& palette) {
    const QString window = palette.window.name();
    const QString text = palette.window_text.name();
    return {window, window, window, text, text, palette.highlight.name(), palette.highlighted_text.name(),
            text,   text,   text,   text, text};
}

HighContrastPalette lightHighContrastPalette() {
    return {QColor{254, 245, 220}, QColor{11, 24, 48}, QColor{24, 15, 143}, QColor{255, 252, 235},
            QColor{107, 82, 110}};
}

} // namespace

class ShellUiContractTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void exposesFrozenConstantsAndAuthenticatedResources();
    void rejectsUnknownAssetMappings();
    void rejectsUnsupportedTransportStrings();
    void rejectsUnsupportedPreferenceStrings();
    void themeModesUseFixturePalettesWithExplicitPrecedence();
    void motionModesUseFixtureTimingsWithExplicitPrecedence();
    void appearanceNotificationsReflectOnlyEffectiveChanges();
    void appearanceIsCoherentWhenNotified();
    void rejectsInvalidAppearanceModesWithoutMutation();
    void highContrastPaletteHasDeterministicDefaults();
    void highContrastOverridesEveryThemeWithoutChangingMotion();
    void leavingHighContrastRestoresTheRequestedTheme();
    void highContrastChangesAreAtomicAndIdempotent();
    void highContrastDarkThemeUsesWindowBrightness();
    void rejectsInvalidHighContrastColorsWithoutMutation();
    void rejectsInvalidAppearanceModesEvenInHighContrast();
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
    QCOMPARE(contract.queuePanelMinimumWidth(), 320);
    QCOMPARE(contract.queuePanelMaximumWidth(), 380);
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
    QCOMPARE(contract.queueTitle(), QStringLiteral("Fila"));
    QCOMPARE(contract.detailsTitle(), QStringLiteral("Detalhes da faixa"));
    QCOMPARE(contract.notInformed(), QStringLiteral("Não informado"));
    QCOMPARE(contract.errorTitle(), QStringLiteral("Não foi possível abrir esta faixa"));
    QCOMPARE(contract.errorBody(), QStringLiteral("A faixa de teste está indisponível."));
    QCOMPARE(contract.actionSkip(), QStringLiteral("Pular"));
    QCOMPARE(contract.preferencesTitle(), QStringLiteral("Preferências"));
    QCOMPARE(contract.sectionGeneral(), QStringLiteral("Geral"));
    QCOMPARE(contract.sectionPlayback(), QStringLiteral("Reprodução"));
    QCOMPARE(contract.sectionAppearance(), QStringLiteral("Aparência"));
    QCOMPARE(contract.themeSystem(), QStringLiteral("Sistema"));
    QCOMPARE(contract.themeLight(), QStringLiteral("Claro"));
    QCOMPARE(contract.themeDark(), QStringLiteral("Escuro"));
    QCOMPARE(contract.motionSystem(), QStringLiteral("Sistema"));
    QCOMPARE(contract.motionReduced(), QStringLiteral("Reduzido"));
    QCOMPARE(contract.canvasColor(), QStringLiteral("#F6F7F9"));
    QCOMPARE(contract.surfaceColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(contract.surfaceRaisedColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(contract.textPrimaryColor(), QStringLiteral("#15171A"));
    QCOMPARE(contract.textSecondaryColor(), QStringLiteral("#4F5660"));
    QCOMPARE(contract.accentColor(), QStringLiteral("#4F46E5"));
    QCOMPARE(contract.accentTextColor(), QStringLiteral("#FFFFFF"));
    QCOMPARE(contract.focusColor(), QStringLiteral("#2457D6"));
    QCOMPARE(contract.borderColor(), QStringLiteral("#8A929E"));
    QCOMPARE(contract.errorColor(), QStringLiteral("#B42318"));
    QCOMPARE(contract.warningColor(), QStringLiteral("#8A4B08"));
    QCOMPARE(contract.successColor(), QStringLiteral("#167344"));
    QCOMPARE(contract.disabledTextColor(), contract.textPrimaryColor());
    QVERIFY(!contract.highContrast());
    QVERIFY(!contract.darkTheme());
    QVERIFY(!contract.reducedMotion());
    QCOMPARE(contract.panelTransitionMs(), 160);
    QCOMPARE(contract.feedbackTransitionMs(), 120);
    QCOMPARE(contract.placeholderSource(), QUrl(QStringLiteral("qrc:/m0/assets/cover-placeholder.svg")));
    QCOMPARE(contract.trackSource(), QUrl(QStringLiteral("qrc:/m0/assets/cover-track.svg")));
    const QMetaObject* meta = contract.metaObject();
    const QStringList expected_properties = {
        QStringLiteral("clientWidth"),
        QStringLiteral("clientHeight"),
        QStringLiteral("minimumWidth"),
        QStringLiteral("minimumHeight"),
        QStringLiteral("queuePanelMinimumWidth"),
        QStringLiteral("queuePanelMaximumWidth"),
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
        QStringLiteral("queueTitle"),
        QStringLiteral("detailsTitle"),
        QStringLiteral("notInformed"),
        QStringLiteral("errorTitle"),
        QStringLiteral("errorBody"),
        QStringLiteral("actionSkip"),
        QStringLiteral("preferencesTitle"),
        QStringLiteral("sectionGeneral"),
        QStringLiteral("sectionPlayback"),
        QStringLiteral("sectionAppearance"),
        QStringLiteral("themeSystem"),
        QStringLiteral("themeLight"),
        QStringLiteral("themeDark"),
        QStringLiteral("motionSystem"),
        QStringLiteral("motionReduced"),
        QStringLiteral("canvasColor"),
        QStringLiteral("surfaceColor"),
        QStringLiteral("surfaceRaisedColor"),
        QStringLiteral("textPrimaryColor"),
        QStringLiteral("textSecondaryColor"),
        QStringLiteral("accentColor"),
        QStringLiteral("accentTextColor"),
        QStringLiteral("focusColor"),
        QStringLiteral("borderColor"),
        QStringLiteral("errorColor"),
        QStringLiteral("warningColor"),
        QStringLiteral("successColor"),
        QStringLiteral("disabledTextColor"),
        QStringLiteral("highContrast"),
        QStringLiteral("darkTheme"),
        QStringLiteral("reducedMotion"),
        QStringLiteral("panelTransitionMs"),
        QStringLiteral("feedbackTransitionMs"),
        QStringLiteral("placeholderSource"),
        QStringLiteral("trackSource"),
    };
    const QStringList appearance_properties = {
        QStringLiteral("canvasColor"),        QStringLiteral("surfaceColor"),
        QStringLiteral("surfaceRaisedColor"), QStringLiteral("textPrimaryColor"),
        QStringLiteral("textSecondaryColor"), QStringLiteral("accentColor"),
        QStringLiteral("accentTextColor"),    QStringLiteral("focusColor"),
        QStringLiteral("borderColor"),        QStringLiteral("errorColor"),
        QStringLiteral("warningColor"),       QStringLiteral("successColor"),
        QStringLiteral("disabledTextColor"),  QStringLiteral("highContrast"),
        QStringLiteral("darkTheme"),          QStringLiteral("reducedMotion"),
        QStringLiteral("panelTransitionMs"),  QStringLiteral("feedbackTransitionMs"),
    };
    QStringList actual_properties;
    for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
        const QMetaProperty property = meta->property(index);
        actual_properties.append(QString::fromLatin1(property.name()));
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        if (appearance_properties.contains(QString::fromLatin1(property.name()))) {
            QVERIFY(!property.isConstant());
            QVERIFY(property.hasNotifySignal());
            QCOMPARE(property.notifySignal().methodSignature(), QByteArray{"appearanceChanged()"});
        } else {
            QVERIFY(property.isConstant());
            QVERIFY(!property.hasNotifySignal());
        }
    }
    QCOMPARE(actual_properties, expected_properties);
    QCOMPARE(meta->methodCount(), meta->methodOffset() + 1);
    QCOMPARE(meta->method(meta->methodOffset()).methodType(), QMetaMethod::Signal);
    QCOMPARE(meta->method(meta->methodOffset()).methodSignature(), QByteArray{"appearanceChanged()"});
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
          &FixtureStrings::queue_title, &FixtureStrings::not_informed, &FixtureStrings::error_title,
          &FixtureStrings::error_body, &FixtureStrings::action_skip}) {
        FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
        bundle.strings.*member = QStringLiteral("drifted");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, static_cast<void>(ShellUiContract{bundle}));
    }
}

void ShellUiContractTests::rejectsUnsupportedPreferenceStrings() {
    for (QString FixtureStrings::* const member :
         {&FixtureStrings::preferences_title, &FixtureStrings::section_general, &FixtureStrings::section_playback,
          &FixtureStrings::section_appearance, &FixtureStrings::theme_system, &FixtureStrings::theme_light,
          &FixtureStrings::theme_dark, &FixtureStrings::motion_system, &FixtureStrings::motion_reduced}) {
        FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
        bundle.strings.*member = QStringLiteral("drifted");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, static_cast<void>(ShellUiContract{bundle}));
    }
}

void ShellUiContractTests::themeModesUseFixturePalettesWithExplicitPrecedence() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    for (const ThemeMode theme : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark}) {
        for (const bool system_dark : {false, true}) {
            contract.updateAppearance(theme, MotionMode::System, system_dark, false);
            const bool expected_dark = theme == ThemeMode::Dark || (theme == ThemeMode::System && system_dark);
            QCOMPARE(contract.darkTheme(), expected_dark);
            QCOMPARE(appearanceColors(contract),
                     paletteColors(expected_dark ? bundle.themes.dark : bundle.themes.light));
            QCOMPARE(contract.disabledTextColor(), contract.textPrimaryColor());
            QVERIFY(!contract.highContrast());
            QVERIFY(!contract.reducedMotion());
            QCOMPARE(contract.panelTransitionMs(), 160);
            QCOMPARE(contract.feedbackTransitionMs(), 120);
        }
    }
    contract.updateAppearance(ThemeMode::System, MotionMode::System, false, false);
    QCOMPARE(appearanceColors(contract), paletteColors(bundle.themes.light));
}

void ShellUiContractTests::motionModesUseFixtureTimingsWithExplicitPrecedence() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    for (const MotionMode motion : {MotionMode::System, MotionMode::Reduced}) {
        for (const bool system_reduced : {false, true}) {
            contract.updateAppearance(ThemeMode::Light, motion, true, system_reduced);
            const bool expected_reduced = motion == MotionMode::Reduced || system_reduced;
            QCOMPARE(contract.reducedMotion(), expected_reduced);
            QCOMPARE(contract.panelTransitionMs(), expected_reduced ? 0 : 160);
            QCOMPARE(contract.feedbackTransitionMs(), expected_reduced ? 0 : 120);
            QVERIFY(!contract.darkTheme());
            QCOMPARE(appearanceColors(contract), paletteColors(bundle.themes.light));
        }
    }
    contract.updateAppearance(ThemeMode::Light, MotionMode::System, false, false);
    QVERIFY(!contract.reducedMotion());
    QCOMPARE(contract.panelTransitionMs(), bundle.motion.normal.panel_transition_ms);
    QCOMPARE(contract.feedbackTransitionMs(), bundle.motion.normal.feedback_transition_ms);
}

void ShellUiContractTests::appearanceNotificationsReflectOnlyEffectiveChanges() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
    QVERIFY(changed.isValid());

    contract.updateAppearance(ThemeMode::Light, MotionMode::System, false, false);
    contract.updateAppearance(ThemeMode::System, MotionMode::System, false, false);
    QCOMPARE(changed.count(), 0);

    contract.updateAppearance(ThemeMode::Dark, MotionMode::System, false, false);
    QCOMPARE(changed.count(), 1);
    contract.updateAppearance(ThemeMode::System, MotionMode::System, true, false);
    QCOMPARE(changed.count(), 1);

    contract.updateAppearance(ThemeMode::Dark, MotionMode::Reduced, false, false);
    QCOMPARE(changed.count(), 2);
    contract.updateAppearance(ThemeMode::System, MotionMode::System, true, true);
    QCOMPARE(changed.count(), 2);

    contract.updateAppearance(ThemeMode::Light, MotionMode::Reduced, true, true);
    QCOMPARE(changed.count(), 3);
    contract.updateAppearance(ThemeMode::Light, MotionMode::System, true, true);
    QCOMPARE(changed.count(), 3);

    contract.updateAppearance(ThemeMode::System, MotionMode::System, true, false);
    QCOMPARE(changed.count(), 4);
    contract.updateAppearance(ThemeMode::System, MotionMode::System, true, false);
    QCOMPARE(changed.count(), 4);
}

void ShellUiContractTests::appearanceIsCoherentWhenNotified() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
    bool observed_complete_appearance = false;
    QObject::connect(&contract, &ShellUiContract::appearanceChanged, &contract, [&] {
        observed_complete_appearance = contract.darkTheme() && contract.reducedMotion() &&
                                       contract.panelTransitionMs() == 0 && contract.feedbackTransitionMs() == 0 &&
                                       appearanceColors(contract) == paletteColors(bundle.themes.dark);
    });

    contract.updateAppearance(ThemeMode::Dark, MotionMode::Reduced, false, false);

    QCOMPARE(changed.count(), 1);
    QVERIFY(observed_complete_appearance);
}

void ShellUiContractTests::rejectsInvalidAppearanceModesWithoutMutation() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    for (const bool initially_dark_and_reduced : {false, true}) {
        ShellUiContract contract(bundle);
        contract.updateAppearance(initially_dark_and_reduced ? ThemeMode::Dark : ThemeMode::Light,
                                  initially_dark_and_reduced ? MotionMode::Reduced : MotionMode::System, false, false);
        QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
        const QStringList original_colors = appearanceColors(contract);
        const auto verify_unchanged = [&] {
            QCOMPARE(contract.darkTheme(), initially_dark_and_reduced);
            QCOMPARE(contract.reducedMotion(), initially_dark_and_reduced);
            QCOMPARE(contract.panelTransitionMs(), initially_dark_and_reduced ? 0 : 160);
            QCOMPARE(contract.feedbackTransitionMs(), initially_dark_and_reduced ? 0 : 120);
            QCOMPARE(appearanceColors(contract), original_colors);
            QCOMPARE(changed.count(), 0);
        };

        for (const int invalid_theme : {-1, 3}) {
            QVERIFY_THROWS_EXCEPTION(
                std::invalid_argument,
                contract.updateAppearance(static_cast<ThemeMode>(invalid_theme), MotionMode::Reduced, true, true));
            verify_unchanged();
        }
        for (const int invalid_motion : {-1, 2}) {
            QVERIFY_THROWS_EXCEPTION(
                std::invalid_argument,
                contract.updateAppearance(initially_dark_and_reduced ? ThemeMode::Light : ThemeMode::Dark,
                                          static_cast<MotionMode>(invalid_motion), false, false));
            verify_unchanged();
        }
    }
}

void ShellUiContractTests::highContrastPaletteHasDeterministicDefaults() {
    const HighContrastPalette palette;
    QCOMPARE(palette.window, QColor(Qt::black));
    QCOMPARE(palette.window_text, QColor(Qt::white));
    QCOMPARE(palette.highlight, QColor(Qt::yellow));
    QCOMPARE(palette.highlighted_text, QColor(Qt::black));
    QCOMPARE(palette.disabled_text, QColor(128, 128, 128));
    QVERIFY(palette == HighContrastPalette{});
    for (QColor HighContrastPalette::* const member :
         {&HighContrastPalette::window, &HighContrastPalette::window_text, &HighContrastPalette::highlight,
          &HighContrastPalette::highlighted_text, &HighContrastPalette::disabled_text}) {
        HighContrastPalette changed = palette;
        changed.*member = QColor(18, 52, 86);
        QVERIFY(palette != changed);
    }
}

void ShellUiContractTests::highContrastOverridesEveryThemeWithoutChangingMotion() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    struct Scenario final {
        HighContrastPalette palette;
        bool dark;
    };
    for (const auto& scenario : {Scenario{HighContrastPalette{}, true}, Scenario{lightHighContrastPalette(), false}}) {
        ShellUiContract contract(bundle);
        for (const ThemeMode theme : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark}) {
            for (const bool system_dark : {false, true}) {
                for (const MotionMode motion : {MotionMode::System, MotionMode::Reduced}) {
                    for (const bool system_reduced : {false, true}) {
                        contract.updateAppearance(theme, motion, system_dark, system_reduced, scenario.palette);
                        const bool reduced = motion == MotionMode::Reduced || system_reduced;
                        QVERIFY(contract.highContrast());
                        QCOMPARE(appearanceColors(contract), highContrastColors(scenario.palette));
                        QCOMPARE(contract.disabledTextColor(), scenario.palette.disabled_text.name());
                        QCOMPARE(contract.darkTheme(), scenario.dark);
                        QCOMPARE(contract.reducedMotion(), reduced);
                        QCOMPARE(contract.panelTransitionMs(), reduced ? 0 : 160);
                        QCOMPARE(contract.feedbackTransitionMs(), reduced ? 0 : 120);
                    }
                }
            }
        }
    }
}

void ShellUiContractTests::leavingHighContrastRestoresTheRequestedTheme() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    for (const ThemeMode theme : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark}) {
        for (const bool system_dark : {false, true}) {
            ShellUiContract contract(bundle);
            QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
            contract.updateAppearance(theme, MotionMode::Reduced, system_dark, false, HighContrastPalette{});
            QVERIFY(contract.highContrast());
            contract.updateAppearance(theme, MotionMode::Reduced, system_dark, false);
            const bool dark = theme == ThemeMode::Dark || (theme == ThemeMode::System && system_dark);
            QVERIFY(!contract.highContrast());
            QCOMPARE(contract.darkTheme(), dark);
            QCOMPARE(appearanceColors(contract), paletteColors(dark ? bundle.themes.dark : bundle.themes.light));
            QCOMPARE(contract.disabledTextColor(), contract.textPrimaryColor());
            QVERIFY(contract.reducedMotion());
            QCOMPARE(contract.panelTransitionMs(), 0);
            QCOMPARE(contract.feedbackTransitionMs(), 0);
            QCOMPARE(changed.count(), 2);
        }
    }
}

void ShellUiContractTests::highContrastChangesAreAtomicAndIdempotent() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    HighContrastPalette expected;
    bool expected_reduced = false;
    int observations = 0;
    QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
    QObject::connect(&contract, &ShellUiContract::appearanceChanged, &contract, [&] {
        ++observations;
        QVERIFY(contract.highContrast());
        QVERIFY(contract.darkTheme());
        QCOMPARE(appearanceColors(contract), highContrastColors(expected));
        QCOMPARE(contract.disabledTextColor(), expected.disabled_text.name());
        QCOMPARE(contract.reducedMotion(), expected_reduced);
        QCOMPARE(contract.panelTransitionMs(), expected_reduced ? 0 : 160);
        QCOMPARE(contract.feedbackTransitionMs(), expected_reduced ? 0 : 120);
    });
    contract.updateAppearance(ThemeMode::Light, MotionMode::System, false, false, expected);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(observations, 1);
    int expected_count = 1;
    for (QColor HighContrastPalette::* const member :
         {&HighContrastPalette::window, &HighContrastPalette::window_text, &HighContrastPalette::highlight,
          &HighContrastPalette::highlighted_text, &HighContrastPalette::disabled_text}) {
        expected.*member = QColor(18, 52, 86);
        contract.updateAppearance(ThemeMode::Dark, MotionMode::System, false, false, expected);
        ++expected_count;
        QCOMPARE(changed.count(), expected_count);
        QCOMPARE(observations, expected_count);
        contract.updateAppearance(ThemeMode::Light, MotionMode::System, true, false, expected);
        contract.updateAppearance(ThemeMode::System, MotionMode::System, false, false, expected);
        QCOMPARE(changed.count(), expected_count);
        QCOMPARE(observations, expected_count);
    }
    HighContrastPalette caller_copy = expected;
    contract.updateAppearance(ThemeMode::Dark, MotionMode::System, true, false, caller_copy);
    caller_copy.window = QColor(Qt::white);
    QCOMPARE(appearanceColors(contract), highContrastColors(expected));
    QCOMPARE(changed.count(), expected_count);

    expected_reduced = true;
    contract.updateAppearance(ThemeMode::Light, MotionMode::Reduced, false, false, expected);
    ++expected_count;
    QCOMPARE(changed.count(), expected_count);
    QCOMPARE(observations, expected_count);
    contract.updateAppearance(ThemeMode::System, MotionMode::System, true, true, expected);
    QCOMPARE(changed.count(), expected_count);
}

void ShellUiContractTests::highContrastDarkThemeUsesWindowBrightness() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    struct Scenario final {
        QColor window;
        bool dark;
    };
    for (const auto& scenario : {Scenario{QColor(Qt::black), true}, Scenario{QColor(Qt::white), false},
                                 Scenario{QColor(127, 127, 127), true}, Scenario{QColor(128, 128, 128), false},
                                 Scenario{QColor(Qt::blue), true}, Scenario{QColor(Qt::yellow), false}}) {
        ShellUiContract contract(bundle);
        HighContrastPalette palette;
        palette.window = scenario.window;
        contract.updateAppearance(ThemeMode::Dark, MotionMode::System, true, false, palette);
        QCOMPARE(contract.darkTheme(), scenario.dark);
        QCOMPARE(contract.canvasColor(), scenario.window.name());
        QVERIFY(contract.highContrast());
    }
}

void ShellUiContractTests::rejectsInvalidHighContrastColorsWithoutMutation() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    for (const bool initially_high_contrast : {false, true}) {
        ShellUiContract contract(bundle);
        contract.updateAppearance(ThemeMode::Dark, MotionMode::Reduced, false, false,
                                  initially_high_contrast ? std::optional{HighContrastPalette{}} : std::nullopt);
        const QStringList original = appearanceColors(contract);
        const QString original_disabled = contract.disabledTextColor();
        QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
        for (QColor HighContrastPalette::* const member :
             {&HighContrastPalette::window, &HighContrastPalette::window_text, &HighContrastPalette::highlight,
              &HighContrastPalette::highlighted_text, &HighContrastPalette::disabled_text}) {
            for (const QColor& invalid :
                 {QColor{}, QColor(0, 0, 0, 0), QColor(18, 52, 86, 254), QColor::fromRgbF(0.1F, 0.2F, 0.3F, 0.999F)}) {
                HighContrastPalette palette = lightHighContrastPalette();
                palette.*member = invalid;
                QVERIFY_THROWS_EXCEPTION(
                    std::invalid_argument,
                    contract.updateAppearance(ThemeMode::Light, MotionMode::System, false, false, palette));
                QCOMPARE(appearanceColors(contract), original);
                QCOMPARE(contract.disabledTextColor(), original_disabled);
                QCOMPARE(contract.highContrast(), initially_high_contrast);
                QVERIFY(contract.darkTheme());
                QVERIFY(contract.reducedMotion());
                QCOMPARE(contract.panelTransitionMs(), 0);
                QCOMPARE(contract.feedbackTransitionMs(), 0);
                QCOMPARE(changed.count(), 0);
            }
        }
    }
}

void ShellUiContractTests::rejectsInvalidAppearanceModesEvenInHighContrast() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    const HighContrastPalette original_palette;
    contract.updateAppearance(ThemeMode::Dark, MotionMode::Reduced, false, false, original_palette);
    QSignalSpy changed(&contract, &ShellUiContract::appearanceChanged);
    const auto verify_unchanged = [&] {
        QVERIFY(contract.highContrast());
        QVERIFY(contract.darkTheme());
        QVERIFY(contract.reducedMotion());
        QCOMPARE(contract.panelTransitionMs(), 0);
        QCOMPARE(contract.feedbackTransitionMs(), 0);
        QCOMPARE(appearanceColors(contract), highContrastColors(original_palette));
        QCOMPARE(contract.disabledTextColor(), original_palette.disabled_text.name());
        QCOMPARE(changed.count(), 0);
    };
    for (const int invalid_theme : {-1, 3}) {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 contract.updateAppearance(static_cast<ThemeMode>(invalid_theme), MotionMode::System,
                                                           false, false, lightHighContrastPalette()));
        verify_unchanged();
    }
    for (const int invalid_motion : {-1, 2}) {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 contract.updateAppearance(ThemeMode::Light, static_cast<MotionMode>(invalid_motion),
                                                           false, false, lightHighContrastPalette()));
        verify_unchanged();
    }
}

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::ShellUiContractTests)
#include "ShellUiContractTests.moc"
