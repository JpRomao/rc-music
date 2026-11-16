#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/domain/PlaybackTypes.hpp"
#include "presentation/ApplicationIdentity.hpp"
#include "presentation/ApplicationText.hpp"
#include "presentation/HighContrastPalette.hpp"
#include "presentation/LifecycleText.hpp"
#include "presentation/ShellUiContract.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJSValue>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>
#include <QTranslator>
#include <QWindow>
#include <QtQml/qqml.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>

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

QStringList auroraColors(bool dark) {
    if (dark) {
        return {QStringLiteral("#1B1917"), QStringLiteral("#24211D"), QStringLiteral("#2D2923"),
                QStringLiteral("#F2EADD"), QStringLiteral("#BFB09D"), QStringLiteral("#D6A27E"),
                QStringLiteral("#2D1C13"), QStringLiteral("#D6A27E"), QStringLiteral("#89715A"),
                QStringLiteral("#F2ADA0"), QStringLiteral("#E2C78B"), QStringLiteral("#6DDA9B")};
    }
    return {QStringLiteral("#F5F1E9"), QStringLiteral("#FFFCF6"), QStringLiteral("#EEE8DD"), QStringLiteral("#302D28"),
            QStringLiteral("#726557"), QStringLiteral("#934B32"), QStringLiteral("#FFFAF4"), QStringLiteral("#934B32"),
            QStringLiteral("#A58C73"), QStringLiteral("#9B3C32"), QStringLiteral("#795A21"), QStringLiteral("#167344")};
}

HighContrastPalette lightHighContrastPalette() {
    return {QColor{254, 245, 220}, QColor{11, 24, 48}, QColor{24, 15, 143}, QColor{255, 252, 235},
            QColor{107, 82, 110}};
}

class PrefixTranslator final : public QTranslator {
  public:
    [[nodiscard]] bool isEmpty() const override { return false; }

    [[nodiscard]] QString translate(const char*, const char* source_text, const char*, int) const override {
        if (source_text == nullptr)
            return {};
        return QStringLiteral("⟦") + QString::fromUtf8(source_text) + QStringLiteral("⟧");
    }
};

[[nodiscard]] bool isUserFacingLiteral(const QString& literal) {
    if (literal.isEmpty() || QRegularExpression{QStringLiteral(R"(^[+\-−×✕✓▶⏮⏭🔊🔇]+$)")}.match(literal).hasMatch() ||
        QRegularExpression{QStringLiteral(R"(^\{[a-zA-Z]+\}$)")}.match(literal).hasMatch()) {
        return false;
    }
    if (QStringList{QStringLiteral("0"), QStringLiteral(":"), QStringLiteral("\\n"), QStringLiteral(".")}.contains(
            literal)) {
        return false;
    }
    return literal.contains(QRegularExpression{QStringLiteral(R"([A-Za-zÀ-ɏ])")});
}

[[nodiscard]] bool isTechnicalQmlContext(const QString& line) {
    static const QRegularExpression technical_property{QStringLiteral(
        R"(\b(?:objectName|fallbackObjectName|imageObjectName|controlId|Accessible\.id|sequences?|property|color|source)\s*:)")};
    static const QRegularExpression boolean_selector{
        QStringLiteral(R"(^\s*(?:readonly\s+)?property\s+bool\s+[A-Za-z_][A-Za-z0-9_]*\s*:)")};
    const QString trimmed = line.trimmed();
    return technical_property.match(line).hasMatch() || boolean_selector.match(line).hasMatch() ||
           trimmed.startsWith(QStringLiteral("case ")) || line.contains(QStringLiteral("typeof "));
}

[[nodiscard]] bool isTechnicalQmlLiteral(const QString& line, qsizetype literal_start) {
    if (isTechnicalQmlContext(line))
        return true;
    const QString prefix = line.left(literal_start);
    const QString literal = line.mid(literal_start + 1).section(QLatin1Char{'"'}, 0, 0);
    // Classify individual data literals, never exempt the whole mixed expression:
    // a mode comparison must not conceal an untranslated label in its next branch.
    const auto matches = [&](const char* pattern) {
        return QRegularExpression{QString::fromLatin1(pattern)}.match(prefix).hasMatch();
    };
    if (matches(R"((?:===|!==|==|!=)\s*$)") || matches(R"(^\s*(?:textRole|valueRole|primaryObjectName)\s*:\s*$)") ||
        matches(R"(\bQt\.openUrlExternally\(\s*$)")) {
        return true;
    }
    const QStringList kinds{QStringLiteral("original"), QStringLiteral("romanized"), QStringLiteral("translation")};
    if (QStringList{QStringLiteral("home"), QStringLiteral("queue"), QStringLiteral("playlists"),
                    QStringLiteral("downloads"), QStringLiteral("preferences")}
            .contains(literal) &&
        matches(R"(^\s*(?:readonly\s+)?property\s+string\s+currentPage\s*:)")) {
        return true;
    }
    if (kinds.contains(literal) &&
        (matches(R"(^\s*value\s*:\s*$)") || matches(R"(^\s*readonly\s+property\s+var\s+versionKinds\s*:\s*\[.*$)"))) {
        return true;
    }
    if (literal == QStringLiteral("off") && matches(R"(^\s*readonly\s+property\s+string\s+noLyricsId\s*:\s*$)")) {
        return true;
    }
    if (QStringList{QStringLiteral("add"), QStringLiteral("queued"), QStringLiteral("romaji"), QStringLiteral("edit"),
                    QStringLiteral("current")}
            .contains(literal) &&
        matches(R"(\b(?:startForm|openForm)\(\s*$)")) {
        return true;
    }
    if (QStringList{QStringLiteral("create"), QStringLiteral("saveQueue"), QStringLiteral("rename")}.contains(
            literal) &&
        matches(R"(\bopenName\(\s*$)")) {
        return true;
    }
    if (QStringList{QStringLiteral("remove"), QStringLiteral("reload")}.contains(literal) &&
        matches(R"(^\s*confirmMode\s*=\s*$)")) {
        return true;
    }
    return (literal == QStringLiteral("ja") && matches(R"(^\s*languageField\.text\s*=.*\bromaji\s*\?\s*$)")) ||
           (literal == QStringLiteral("Latn") && matches(R"(^\s*scriptField\.text\s*=.*\bromaji\s*\?\s*$)"));
}

} // namespace

class ShellUiContractTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void configuresQtDisplayNameWithoutMovingStoredData();
    void configuresTheExistingArtworkAsTheApplicationAndWindowIcon();
    void exposesFrozenConstantsAndAuthenticatedResources();
    void productConstantsRetainFrozenBehaviorWithAuroraColors();
    void productAppearanceUsesAuroraWhileHistoricalPalettesStayFrozen();
    void interactionTokensFollowAuroraHighContrastAndFrozenFixture();
    void exposesTypeScaleAndLayoutBreakpoints();
    void productTextRetainsTheHistoricalTranslationContract();
    void historicalConstructorRetainsAcceptedVisualOverrides();
    void rejectsUnknownAssetMappings();
    void rejectsUnsupportedTransportStrings();
    void rejectsUnsupportedPreferenceStrings();
    void essentialQmlTextIsCentralizedAndExtractable();
    void distinguishesTechnicalBooleanSelectorsFromVisibleText();
    void distinguishesShortcutSequencesFromVisibleText_data();
    void distinguishesShortcutSequencesFromVisibleText();
    void distinguishesTechnicalDataFromVisibleText_data();
    void distinguishesTechnicalDataFromVisibleText();
    void lyricsAndPlaylistCatalogsTranslateWithoutChangingStoredKinds();
    void classifiesSingleInstanceFailuresThroughTheTextCatalog();
    void invalidSourcePathsHaveActionableFeedbackWithoutExternalDetails();
    void shellTextCanBeTranslatedWithoutChangingTheFixture();
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

void ShellUiContractTests::initTestCase() {
    Q_INIT_RESOURCE(m0_qt_shell_assets);
    Q_INIT_RESOURCE(m0_qt_fixture_data);
}

void ShellUiContractTests::configuresQtDisplayNameWithoutMovingStoredData() {
    const QString original_organization = QCoreApplication::organizationName();
    const QString original_application = QCoreApplication::applicationName();
    const QString original_display_name = QGuiApplication::applicationDisplayName();
    const QIcon original_window_icon = QGuiApplication::windowIcon();
    const auto restore_identity = qScopeGuard([&] {
        QCoreApplication::setOrganizationName(original_organization);
        QCoreApplication::setApplicationName(original_application);
        QGuiApplication::setApplicationDisplayName(original_display_name);
        QGuiApplication::setWindowIcon(original_window_icon);
    });

    QCoreApplication::setOrganizationName(QStringLiteral("MusicPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("MusicPlayer"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Previous display name"));
    const QString original_config_root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString original_data_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY(!original_config_root.isEmpty());
    QVERIFY(!original_data_root.isEmpty());

    for (int configuration = 0; configuration < 2; ++configuration) {
        ApplicationIdentity::configureQtApplication();
        QCOMPARE(QCoreApplication::organizationName(), QStringLiteral("MusicPlayer"));
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("MusicPlayer"));
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation), original_config_root);
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation), original_data_root);
        QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("RC Music"));
        QCOMPARE(ApplicationIdentity::displayName(), QStringLiteral("RC Music"));
    }
}

void ShellUiContractTests::configuresTheExistingArtworkAsTheApplicationAndWindowIcon() {
    const QString original_organization = QCoreApplication::organizationName();
    const QString original_application = QCoreApplication::applicationName();
    const QString original_display_name = QGuiApplication::applicationDisplayName();
    const QIcon original_window_icon = QGuiApplication::windowIcon();
    const auto restore_identity = qScopeGuard([&] {
        QCoreApplication::setOrganizationName(original_organization);
        QCoreApplication::setApplicationName(original_application);
        QGuiApplication::setApplicationDisplayName(original_display_name);
        QGuiApplication::setWindowIcon(original_window_icon);
    });
    const QIcon expected_icon{QStringLiteral(":/m0/assets/cover-track.svg")};
    QVERIFY(!expected_icon.isNull());
    QVERIFY(!expected_icon.pixmap(32, 32).isNull());

    QGuiApplication::setWindowIcon(QIcon{});
    ApplicationIdentity::configureQtApplication();
    QVERIFY(!QGuiApplication::windowIcon().isNull());
    QWindow window;
    QVERIFY(!window.isVisible());
    QVERIFY(!window.icon().isNull());
    for (const int size : {16, 32, 64}) {
        const QImage expected = expected_icon.pixmap(size, size).toImage();
        const QImage application_icon = QGuiApplication::windowIcon().pixmap(size, size).toImage();
        const QImage window_icon = window.icon().pixmap(size, size).toImage();
        QVERIFY(!expected.isNull());
        QVERIFY(!application_icon.isNull());
        QVERIFY(!window_icon.isNull());
        QCOMPARE(application_icon, expected);
        QCOMPARE(window_icon, expected);
    }
}

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
    QCOMPARE(contract.appName(), QStringLiteral("RC Music"));
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
    QCOMPARE(contract.actionRetry(), QStringLiteral("Tentar novamente"));
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
    QCOMPARE(contract.navigationColor(), contract.surfaceColor());
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
        QStringLiteral("typeEyebrow"),
        QStringLiteral("typeCaption"),
        QStringLiteral("typeBody"),
        QStringLiteral("typeSubtitle"),
        QStringLiteral("typeTitle"),
        QStringLiteral("typeSection"),
        QStringLiteral("typeHeading"),
        QStringLiteral("typeDisplay"),
        QStringLiteral("compactWidth"),
        QStringLiteral("wideWidth"),
        QStringLiteral("appName"),
        QStringLiteral("headingFontFamily"),
        QStringLiteral("bodyFontFamily"),
        QStringLiteral("symbolFontFamily"),
        QStringLiteral("applicationFont"),
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
        QStringLiteral("actionRetry"),
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
        QStringLiteral("navigationColor"),
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
        QStringLiteral("selectionColor"),
        QStringLiteral("hoverColor"),
        QStringLiteral("dividerColor"),
        QStringLiteral("highContrast"),
        QStringLiteral("darkTheme"),
        QStringLiteral("reducedMotion"),
        QStringLiteral("panelTransitionMs"),
        QStringLiteral("feedbackTransitionMs"),
        QStringLiteral("placeholderSource"),
        QStringLiteral("trackSource"),
    };
    const QStringList appearance_properties = {
        QStringLiteral("navigationColor"),   QStringLiteral("canvasColor"),
        QStringLiteral("surfaceColor"),      QStringLiteral("surfaceRaisedColor"),
        QStringLiteral("textPrimaryColor"),  QStringLiteral("textSecondaryColor"),
        QStringLiteral("accentColor"),       QStringLiteral("accentTextColor"),
        QStringLiteral("focusColor"),        QStringLiteral("borderColor"),
        QStringLiteral("errorColor"),        QStringLiteral("warningColor"),
        QStringLiteral("successColor"),      QStringLiteral("disabledTextColor"),
        QStringLiteral("selectionColor"),    QStringLiteral("hoverColor"),
        QStringLiteral("dividerColor"),      QStringLiteral("highContrast"),
        QStringLiteral("darkTheme"),         QStringLiteral("reducedMotion"),
        QStringLiteral("panelTransitionMs"), QStringLiteral("feedbackTransitionMs"),
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

void ShellUiContractTests::productConstantsRetainFrozenBehaviorWithAuroraColors() {
    QObject owner;
    ShellUiContract product(&owner);
    QCOMPARE(product.parent(), &owner);
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract historical(bundle);
    const QMetaObject* meta = product.metaObject();
    for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
        const QMetaProperty property = meta->property(index);
        const QVariant expected = property.read(&historical);
        QVERIFY2(expected.isValid(), property.name());
        if (!QString::fromLatin1(property.name()).endsWith(QStringLiteral("Color")))
            QVERIFY2(property.read(&product) == expected, property.name());
    }
    QCOMPARE(appearanceColors(product), auroraColors(false));
    QCOMPARE(product.disabledTextColor(), product.textPrimaryColor());
    QCOMPARE(appearanceColors(historical), paletteColors(bundle.themes.light));
}

void ShellUiContractTests::productAppearanceUsesAuroraWhileHistoricalPalettesStayFrozen() {
    ShellUiContract product(nullptr);
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract historical(bundle);
    QSignalSpy product_changed(&product, &ShellUiContract::appearanceChanged);
    QSignalSpy historical_changed(&historical, &ShellUiContract::appearanceChanged);
    QVERIFY(product_changed.isValid());
    QVERIFY(historical_changed.isValid());

    for (const ThemeMode theme : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark}) {
        for (const MotionMode motion : {MotionMode::System, MotionMode::Reduced}) {
            for (const bool system_dark : {false, true}) {
                for (const bool system_reduced : {false, true}) {
                    for (const std::optional<HighContrastPalette>& high_contrast :
                         {std::optional<HighContrastPalette>{},
                          std::optional<HighContrastPalette>{lightHighContrastPalette()},
                          std::optional<HighContrastPalette>{HighContrastPalette{}}}) {
                        for (int repetition = 0; repetition < 2; ++repetition) {
                            product.updateAppearance(theme, motion, system_dark, system_reduced, high_contrast);
                            historical.updateAppearance(theme, motion, system_dark, system_reduced, high_contrast);
                            const bool dark = theme == ThemeMode::Dark || (theme == ThemeMode::System && system_dark);
                            QCOMPARE(appearanceColors(product),
                                     high_contrast ? highContrastColors(*high_contrast) : auroraColors(dark));
                            QCOMPARE(appearanceColors(historical),
                                     high_contrast ? highContrastColors(*high_contrast)
                                                   : paletteColors(dark ? bundle.themes.dark : bundle.themes.light));
                            QCOMPARE(product.disabledTextColor(),
                                     high_contrast ? high_contrast->disabled_text.name() : product.textPrimaryColor());
                            QCOMPARE(product.highContrast(), historical.highContrast());
                            QCOMPARE(product.darkTheme(), historical.darkTheme());
                            QCOMPARE(product.reducedMotion(), historical.reducedMotion());
                            QCOMPARE(product.panelTransitionMs(), historical.panelTransitionMs());
                            QCOMPARE(product.feedbackTransitionMs(), historical.feedbackTransitionMs());
                            QCOMPARE(product_changed.count(), historical_changed.count());
                        }
                    }
                }
            }
        }
    }
    product.updateAppearance(ThemeMode::System, MotionMode::System, false, false);
    historical.updateAppearance(ThemeMode::System, MotionMode::System, false, false);
    QCOMPARE(appearanceColors(product), auroraColors(false));
    QCOMPARE(appearanceColors(historical), paletteColors(bundle.themes.light));
    QCOMPARE(product.panelTransitionMs(), bundle.motion.normal.panel_transition_ms);
    QCOMPARE(product.feedbackTransitionMs(), bundle.motion.normal.feedback_transition_ms);
    QCOMPARE(product_changed.count(), historical_changed.count());
}

void ShellUiContractTests::interactionTokensFollowAuroraHighContrastAndFrozenFixture() {
    ShellUiContract product(nullptr);
    const auto tokens = [&] {
        return QStringList{product.selectionColor(), product.hoverColor(), product.dividerColor()};
    };
    // Approved Aurora reference (docs/superpowers/specs/2026-11-15-aurora-shell-design.md).
    product.updateAppearance(ThemeMode::Light, MotionMode::System, false, false);
    QCOMPARE(tokens(), QStringList({QStringLiteral("#EEE0D2"), QStringLiteral("#EAE1D5"), QStringLiteral("#D7CAB9")}));
    QVERIFY(product.selectionColor() != product.navigationColor());
    QVERIFY(product.selectionColor() != product.surfaceRaisedColor());
    product.updateAppearance(ThemeMode::Dark, MotionMode::System, false, false);
    QCOMPARE(tokens(), QStringList({QStringLiteral("#3A2C23"), QStringLiteral("#353029"), QStringLiteral("#4C4236")}));
    QVERIFY(product.selectionColor() != product.navigationColor());

    const HighContrastPalette contrast = lightHighContrastPalette();
    product.updateAppearance(ThemeMode::Dark, MotionMode::System, false, false, contrast);
    QCOMPARE(tokens(), QStringList({contrast.highlight.name(), contrast.window.name(), contrast.window_text.name()}));

    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract historical(bundle);
    for (const ThemeMode theme : {ThemeMode::Light, ThemeMode::Dark}) {
        historical.updateAppearance(theme, MotionMode::System, false, false);
        QCOMPARE(historical.selectionColor(), historical.surfaceRaisedColor());
        QCOMPARE(historical.hoverColor(), historical.surfaceRaisedColor());
        QCOMPARE(historical.dividerColor(), historical.borderColor());
    }
}

void ShellUiContractTests::exposesTypeScaleAndLayoutBreakpoints() {
    ShellUiContract product(nullptr);
    const QList<int> scale{product.typeEyebrow(), product.typeCaption(), product.typeBody(),    product.typeSubtitle(),
                           product.typeTitle(),   product.typeSection(), product.typeHeading(), product.typeDisplay()};
    QCOMPARE(scale, QList<int>({10, 12, 14, 18, 20, 24, 30, 36}));
    QCOMPARE(product.compactWidth(), 900);
    QCOMPARE(product.wideWidth(), 1200);
    QVERIFY(product.minimumWidth() < product.compactWidth());
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract historical(bundle);
    QCOMPARE(historical.typeBody(), product.typeBody());
    QCOMPARE(historical.compactWidth(), product.compactWidth());
}

void ShellUiContractTests::productTextRetainsTheHistoricalTranslationContract() {
    ShellUiContract product(nullptr);
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract historical(bundle);
    PrefixTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    const auto restore_translator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
    const QMetaObject* meta = product.metaObject();
    for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
        const QMetaProperty property = meta->property(index);
        if (property.metaType() != QMetaType::fromType<QString>() ||
            QString::fromLatin1(property.name()).endsWith(QStringLiteral("Color")))
            continue;
        QVERIFY2(property.read(&product) == property.read(&historical), property.name());
    }
    QCOMPARE(product.appName(), QStringLiteral("RC Music"));
    QCOMPARE(product.openAudioText(), QStringLiteral("⟦Abrir áudio⟧"));
    QCOMPARE(product.errorBody(), QStringLiteral("⟦A faixa de teste está indisponível.⟧"));
    QCOMPARE(bundle.strings.app_name, QStringLiteral("Player de música"));
    QCOMPARE(bundle.strings.action_open_audio, QStringLiteral("Abrir áudio"));
}

void ShellUiContractTests::historicalConstructorRetainsAcceptedVisualOverrides() {
    FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    bundle.layout_tokens.radius_small_px = 9;
    bundle.layout_tokens.radius_medium_px = 13;
    bundle.layout_tokens.spacing_unit_px = 5;
    bundle.themes.light.surface = QStringLiteral("#FEFEFE");
    bundle.themes.dark.canvas = QStringLiteral("#101112");
    bundle.motion.normal.panel_transition_ms = 180;
    bundle.motion.normal.feedback_transition_ms = 130;
    bundle.motion.reduced.panel_transition_ms = 2;
    bundle.motion.reduced.feedback_transition_ms = 1;
    ShellUiContract contract(bundle);
    QCOMPARE(contract.radiusSmall(), 9);
    QCOMPARE(contract.radiusMedium(), 13);
    QCOMPARE(contract.spacingUnit(), 5);
    QCOMPARE(appearanceColors(contract), paletteColors(bundle.themes.light));
    QCOMPARE(contract.panelTransitionMs(), 180);
    QCOMPARE(contract.feedbackTransitionMs(), 130);
    contract.updateAppearance(ThemeMode::Dark, MotionMode::Reduced, false, false);
    QCOMPARE(appearanceColors(contract), paletteColors(bundle.themes.dark));
    QCOMPARE(contract.panelTransitionMs(), 2);
    QCOMPARE(contract.feedbackTransitionMs(), 1);
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
          &FixtureStrings::error_body, &FixtureStrings::action_skip, &FixtureStrings::action_retry}) {
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

void ShellUiContractTests::distinguishesTechnicalBooleanSelectorsFromVisibleText() {
    QVERIFY(isTechnicalQmlContext(
        QStringLiteral("readonly property bool outputRecovery: presenter.errorActionId === \"recover-audio-output\"")));
    QVERIFY(!isTechnicalQmlContext(QStringLiteral("property string titleText: \"Visible error\"")));
    QVERIFY(!isTechnicalQmlContext(QStringLiteral("text: \"Visible action\"")));
    QVERIFY(!isTechnicalQmlContext(QStringLiteral("Accessible.name: \"Visible description\"")));
}

void ShellUiContractTests::distinguishesShortcutSequencesFromVisibleText_data() {
    QTest::addColumn<QString>("line");
    QTest::addColumn<bool>("technical");
    QTest::newRow("single-sequence") << QStringLiteral("sequence: \"Space\"") << true;
    QTest::newRow("previous-aliases") << QStringLiteral("sequences: [\"Shift+Left\", \"Ctrl+Left\"]") << true;
    QTest::newRow("next-aliases") << QStringLiteral("sequences: [\"Shift+Right\", \"Ctrl+Right\"]") << true;
    QTest::newRow("visible-text") << QStringLiteral("text: \"Shift+Left\"") << false;
    QTest::newRow("visible-accessible-description")
        << QStringLiteral("Accessible.description: \"Shift+Right\"") << false;
    QTest::newRow("visible-string-property")
        << QStringLiteral("readonly property string sequenceText: \"Shift+Left\"") << false;
}

void ShellUiContractTests::distinguishesShortcutSequencesFromVisibleText() {
    QFETCH(QString, line);
    QFETCH(bool, technical);
    QCOMPARE(isTechnicalQmlContext(line), technical);
}

void ShellUiContractTests::essentialQmlTextIsCentralizedAndExtractable() {
    const QDir source_dir{QStringLiteral(M0_QML_SOURCE_DIR)};
    QVERIFY2(source_dir.exists(), qPrintable(source_dir.absolutePath()));

    QStringList violations;
    const QRegularExpression string_literal{QStringLiteral("\"((?:\\\\.|[^\"\\\\])*)\"")};
    QDirIterator iterator(source_dir.absolutePath(), {QStringLiteral("*.qml")}, QDir::Files);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        const bool catalog = QFileInfo{path}.baseName().endsWith(QStringLiteral("Text"));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char{'\n'});
        for (qsizetype index = 0; index < lines.size(); ++index) {
            const QString& line = lines.at(index);
            if (!catalog && line.contains(QStringLiteral("qsTr("))) {
                violations.append(QStringLiteral("%1:%2: qsTr fora de um catálogo *Text.qml")
                                      .arg(QFileInfo{path}.fileName())
                                      .arg(index + 1));
            }
            auto match_iterator = string_literal.globalMatch(line);
            while (match_iterator.hasNext()) {
                const auto match = match_iterator.next();
                const QString literal = match.captured(1);
                if (!isUserFacingLiteral(literal) || isTechnicalQmlLiteral(line, match.capturedStart()))
                    continue;
                if (catalog && line.contains(QStringLiteral("qsTr("), Qt::CaseSensitive))
                    continue;
                violations.append(QStringLiteral("%1:%2: texto visível fora de qsTr: %3")
                                      .arg(QFileInfo{path}.fileName())
                                      .arg(index + 1)
                                      .arg(literal));
            }
        }
    }
    QVERIFY2(violations.isEmpty(), qPrintable(violations.join(QLatin1Char{'\n'})));
}

void ShellUiContractTests::distinguishesTechnicalDataFromVisibleText_data() {
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("literal");
    QTest::addColumn<bool>("technical");
    QTest::newRow("model-role") << QStringLiteral("textRole: \"label\"") << QStringLiteral("label") << true;
    QTest::newRow("model-value-role") << QStringLiteral("valueRole: \"value\"") << QStringLiteral("value") << true;
    QTest::newRow("child-object-name") << QStringLiteral("primaryObjectName: \"playlist-row-name\"")
                                       << QStringLiteral("playlist-row-name") << true;
    QTest::newRow("state-comparison") << QStringLiteral("visible: row.jobState === \"failed\"")
                                      << QStringLiteral("failed") << true;
    QTest::newRow("mixed-state") << QStringLiteral("text: mode === \"romaji\" ? \"Revisar romaji\" : other")
                                 << QStringLiteral("romaji") << true;
    QTest::newRow("mixed-visible") << QStringLiteral("text: mode === \"romaji\" ? \"Revisar romaji\" : other")
                                   << QStringLiteral("Revisar romaji") << false;
    QTest::newRow("external-url") << QStringLiteral("Qt.openUrlExternally(\"https://www.lyricfind.com/\");")
                                  << QStringLiteral("https://www.lyricfind.com/") << true;
    QTest::newRow("visible-url") << QStringLiteral("text: \"https://www.lyricfind.com/\"")
                                 << QStringLiteral("https://www.lyricfind.com/") << false;
    QTest::newRow("kind-codes") << QStringLiteral("readonly property var versionKinds: [\"original\", \"romanized\"]")
                                << QStringLiteral("original") << true;
    QTest::newRow("kind-value") << QStringLiteral("value: \"translation\"") << QStringLiteral("translation") << true;
    QTest::newRow("kind-label") << QStringLiteral("label: \"Original\"") << QStringLiteral("Original") << false;
    QTest::newRow("off-id") << QStringLiteral("readonly property string noLyricsId: \"off\"") << QStringLiteral("off")
                            << true;
    QTest::newRow("form-mode") << QStringLiteral("root.startForm(\"romaji\", content);") << QStringLiteral("romaji")
                               << true;
    QTest::newRow("form-visible-argument")
        << QStringLiteral("root.startForm(\"Revisar romaji\", content);") << QStringLiteral("Revisar romaji") << false;
    QTest::newRow("playlist-mode") << QStringLiteral("onClicked: root.openName(\"saveQueue\")")
                                   << QStringLiteral("saveQueue") << true;
    QTest::newRow("confirmation-mode") << QStringLiteral("confirmMode = \"reload\";") << QStringLiteral("reload")
                                       << true;
    QTest::newRow("language-code") << QStringLiteral(
                                          "languageField.text = draft ? draft.language : (romaji ? \"ja\" : \"\");")
                                   << QStringLiteral("ja") << true;
    QTest::newRow("script-code") << QStringLiteral(
                                        "scriptField.text = draft ? draft.script : (romaji ? \"Latn\" : \"\");")
                                 << QStringLiteral("Latn") << true;
    QTest::newRow("visible-language") << QStringLiteral("text: \"ja\"") << QStringLiteral("ja") << false;
    QTest::newRow("visible-property") << QStringLiteral("readonly property string statusText: \"Falha ao salvar\"")
                                      << QStringLiteral("Falha ao salvar") << false;
    QTest::newRow("artwork-fallback-id") << QStringLiteral(
                                                "fallbackObjectName: root.editorial ? \"\" : \"cover-track\"")
                                         << QStringLiteral("cover-track") << true;
    QTest::newRow("artwork-image-id") << QStringLiteral("imageObjectName: \"embedded-artwork\"")
                                      << QStringLiteral("embedded-artwork") << true;
    QTest::newRow("workspace-first-code")
        << QStringLiteral("readonly property string currentPage: opened ? \"queue\" : \"home\"")
        << QStringLiteral("queue") << true;
    QTest::newRow("workspace-last-code") << QStringLiteral(
                                                "readonly property string currentPage: opened ? \"queue\" : \"home\"")
                                         << QStringLiteral("home") << true;
    QTest::newRow("workspace-visible-copy")
        << QStringLiteral("readonly property string currentPage: opened ? \"queue\" : \"Início\"")
        << QStringLiteral("Início") << false;
    QTest::newRow("visible-workspace-code") << QStringLiteral("text: \"home\"") << QStringLiteral("home") << false;
    QTest::newRow("visible-artwork-code")
        << QStringLiteral("text: \"cover-track\"") << QStringLiteral("cover-track") << false;
}

void ShellUiContractTests::distinguishesTechnicalDataFromVisibleText() {
    QFETCH(QString, line);
    QFETCH(QString, literal);
    QFETCH(bool, technical);
    const qsizetype start = line.indexOf(QLatin1Char{'"'} + literal + QLatin1Char{'"'});
    QVERIFY(start >= 0);
    QCOMPARE(isTechnicalQmlLiteral(line, start), technical);
}

void ShellUiContractTests::lyricsAndPlaylistCatalogsTranslateWithoutChangingStoredKinds() {
    PrefixTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    const auto remove_translator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
    QQmlEngine engine;
    for (const auto& [type, property, expected] : {std::tuple{"LyricsText", "search", "⟦Buscar letra⟧"},
                                                   std::tuple{"PlaylistsText", "create", "⟦Nova playlist⟧"}}) {
        QQmlComponent component{&engine};
        component.loadFromModule(QStringLiteral("MusicPlayer.M0"), QString::fromUtf8(type));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const std::unique_ptr<QObject> catalog{component.create()};
        QVERIFY(catalog != nullptr);
        QCOMPARE(catalog->property(property).toString(), QString::fromUtf8(expected));
    }
    ShellUiContract contract{nullptr};
    for (const QString& type : {QStringLiteral("LyricsVariantsPanel"), QStringLiteral("DownloadLyricsPanel")}) {
        QQmlComponent component{&engine};
        component.loadFromModule(QStringLiteral("MusicPlayer.M0"), type);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const std::unique_ptr<QObject> panel{
            component.createWithInitialProperties({{QStringLiteral("contract"), QVariant::fromValue(&contract)}})};
        QVERIFY2(panel != nullptr, qPrintable(component.errorString()));
        const QVariantList kinds = panel->property("versionKinds").value<QJSValue>().toVariant().toList();
        QCOMPARE(kinds.size(), 3);
        for (int index = 0; index < kinds.size(); ++index) {
            const QString expected =
                QStringList{QStringLiteral("original"), QStringLiteral("romanized"), QStringLiteral("translation")}.at(
                    index);
            if (type == QStringLiteral("LyricsVariantsPanel")) {
                QCOMPARE(kinds.at(index).toMap().value(QStringLiteral("value")).toString(), expected);
                QVERIFY(
                    kinds.at(index).toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("⟦")));
            } else {
                QCOMPARE(kinds.at(index).toString(), expected);
            }
        }
    }
}

void ShellUiContractTests::classifiesSingleInstanceFailuresThroughTheTextCatalog() {
    QCOMPARE(ApplicationText::openSourcesFailureTitle(), QStringLiteral("Não foi possível abrir os arquivos"));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("tooManyPaths"), false),
             QStringLiteral("A solicitação contém arquivos demais ou excede o limite permitido."));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("payloadTooLarge"), false),
             QStringLiteral("A solicitação contém arquivos demais ou excede o limite permitido."));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("queueFull"), false),
             QStringLiteral("O player está processando muitas solicitações. Tente novamente em instantes."));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("temporarilyUnavailable"), false),
             QStringLiteral("O player está concluindo uma operação. Tente novamente em instantes."));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("shuttingDown"), false),
             QStringLiteral("O player está sendo encerrado. Tente abrir os arquivos novamente."));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("ignored"), true),
             QStringLiteral("O player já está aberto, mas não respondeu a tempo."));
    QCOMPARE(ApplicationText::openSourcesFailureMessage(QStringLiteral("invalidResponse"), false),
             QStringLiteral("Não foi possível encaminhar os arquivos para o player."));
}

void ShellUiContractTests::invalidSourcePathsHaveActionableFeedbackWithoutExternalDetails() {
    const QString message = ApplicationText::openSourcesFailureMessage(QStringLiteral("invalidPaths"), false);
    QCOMPARE(message, QStringLiteral("A solicitação contém um caminho inválido ou ambíguo. Use caminhos completos "
                                     "ou abra os arquivos pelo seletor do RC Music."));
    QVERIFY(!message.contains(QStringLiteral("invalidPaths")));
    const QString hostile_detail = QStringLiteral("C:private-track <b>hostile</b>\nexception detail");
    const QString fallback = ApplicationText::openSourcesFailureMessage(hostile_detail, false);
    QCOMPARE(fallback, QStringLiteral("Não foi possível encaminhar os arquivos para o player."));
    QVERIFY(!fallback.contains(QStringLiteral("private-track")));
    QVERIFY(!fallback.contains(QStringLiteral("<b>")));
}

void ShellUiContractTests::shellTextCanBeTranslatedWithoutChangingTheFixture() {
    const FixtureBundle bundle = FixtureRepository{}.load(fixture_resource_path, queue_resource_path);
    ShellUiContract contract(bundle);
    LifecycleText lifecycle_text;
    PrefixTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    const QString translated_app_name = contract.appName();

    const QStringList translated{
        contract.openAudioText(),
        contract.emptyTitle(),
        contract.emptyHint(),
        contract.actionPlay(),
        contract.actionPause(),
        contract.actionPrevious(),
        contract.actionNext(),
        contract.skipBackwardShort(),
        contract.skipForwardShort(),
        contract.skipBackwardName(),
        contract.skipForwardName(),
        contract.progressName(),
        contract.volumeName(),
        contract.actionMute(),
        contract.actionUnmute(),
        contract.queueTitle(),
        contract.detailsTitle(),
        contract.notInformed(),
        contract.errorTitle(),
        contract.errorBody(),
        contract.actionSkip(),
        contract.actionRetry(),
        contract.preferencesTitle(),
        contract.sectionGeneral(),
        contract.sectionPlayback(),
        contract.sectionAppearance(),
        contract.themeSystem(),
        contract.themeLight(),
        contract.themeDark(),
        contract.motionSystem(),
        contract.motionReduced(),
        ApplicationText::localAudioDialogFilter({QStringLiteral("*.wav")}),
        ApplicationText::openSourcesFailureTitle(),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("invalidPaths"), false),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("tooManyPaths"), false),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("queueFull"), false),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("temporarilyUnavailable"), false),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("shuttingDown"), false),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("ignored"), true),
        ApplicationText::openSourcesFailureMessage(QStringLiteral("invalidResponse"), false),
        lifecycle_text.showAction(),
        lifecycle_text.hideAction(),
        lifecycle_text.exitAction(),
        lifecycle_text.noTrack(),
        lifecycle_text.trayTitle(),
        lifecycle_text.trayMessage(),
        lifecycle_text.continueAction(),
        lifecycle_text.cancelAction(),
        lifecycle_text.trayNoticeEnabled(),
        lifecycle_text.unavailableTitle(),
        lifecycle_text.unavailableMessage(),
        lifecycle_text.dismissAction(),
    };
    QVERIFY(QCoreApplication::removeTranslator(&translator));

    QCOMPARE(translated_app_name, QStringLiteral("RC Music"));
    QCOMPARE(contract.appName(), QStringLiteral("RC Music"));
    for (const QString& value : translated) {
        QVERIFY2(value.startsWith(QStringLiteral("⟦")) && value.endsWith(QStringLiteral("⟧")), qPrintable(value));
    }
    QCOMPARE(bundle.strings.app_name, QStringLiteral("Player de música"));
    QCOMPARE(bundle.strings.action_open_audio, QStringLiteral("Abrir áudio"));
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

QTEST_MAIN(music_player::m0::test::ShellUiContractTests)
#include "ShellUiContractTests.moc"
