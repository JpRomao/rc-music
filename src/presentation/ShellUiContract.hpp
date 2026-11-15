#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "presentation/HighContrastPalette.hpp"

#include <QFont>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace music_player::m0 {

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
struct FixtureBundle;
#endif

class ShellUiContract final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ShellUiContract)
    QML_UNCREATABLE("ShellUiContract is provided by the application bootstrap")
    Q_PROPERTY(int clientWidth READ clientWidth CONSTANT)
    Q_PROPERTY(int clientHeight READ clientHeight CONSTANT)
    Q_PROPERTY(int minimumWidth READ minimumWidth CONSTANT)
    Q_PROPERTY(int minimumHeight READ minimumHeight CONSTANT)
    Q_PROPERTY(int queuePanelMinimumWidth READ queuePanelMinimumWidth CONSTANT)
    Q_PROPERTY(int queuePanelMaximumWidth READ queuePanelMaximumWidth CONSTANT)
    Q_PROPERTY(int controlTargetMinimum READ controlTargetMinimum CONSTANT)
    Q_PROPERTY(int controlTargetPreferred READ controlTargetPreferred CONSTANT)
    Q_PROPERTY(int radiusMedium READ radiusMedium CONSTANT)
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int spacingUnit READ spacingUnit CONSTANT)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString headingFontFamily READ headingFontFamily CONSTANT)
    Q_PROPERTY(QString bodyFontFamily READ bodyFontFamily CONSTANT)
    Q_PROPERTY(QString symbolFontFamily READ symbolFontFamily CONSTANT)
    Q_PROPERTY(QFont applicationFont READ applicationFont CONSTANT)
    Q_PROPERTY(QString openAudioText READ openAudioText CONSTANT)
    Q_PROPERTY(QString emptyTitle READ emptyTitle CONSTANT)
    Q_PROPERTY(QString emptyHint READ emptyHint CONSTANT)
    Q_PROPERTY(QString actionPlay READ actionPlay CONSTANT)
    Q_PROPERTY(QString actionPause READ actionPause CONSTANT)
    Q_PROPERTY(QString actionPrevious READ actionPrevious CONSTANT)
    Q_PROPERTY(QString actionNext READ actionNext CONSTANT)
    Q_PROPERTY(QString skipBackwardShort READ skipBackwardShort CONSTANT)
    Q_PROPERTY(QString skipForwardShort READ skipForwardShort CONSTANT)
    Q_PROPERTY(QString skipBackwardName READ skipBackwardName CONSTANT)
    Q_PROPERTY(QString skipForwardName READ skipForwardName CONSTANT)
    Q_PROPERTY(QString progressName READ progressName CONSTANT)
    Q_PROPERTY(QString volumeName READ volumeName CONSTANT)
    Q_PROPERTY(QString actionMute READ actionMute CONSTANT)
    Q_PROPERTY(QString actionUnmute READ actionUnmute CONSTANT)
    Q_PROPERTY(QString queueTitle READ queueTitle CONSTANT)
    Q_PROPERTY(QString detailsTitle READ detailsTitle CONSTANT)
    Q_PROPERTY(QString notInformed READ notInformed CONSTANT)
    Q_PROPERTY(QString errorTitle READ errorTitle CONSTANT)
    Q_PROPERTY(QString errorBody READ errorBody CONSTANT)
    Q_PROPERTY(QString actionSkip READ actionSkip CONSTANT)
    Q_PROPERTY(QString actionRetry READ actionRetry CONSTANT)
    Q_PROPERTY(QString preferencesTitle READ preferencesTitle CONSTANT)
    Q_PROPERTY(QString sectionGeneral READ sectionGeneral CONSTANT)
    Q_PROPERTY(QString sectionPlayback READ sectionPlayback CONSTANT)
    Q_PROPERTY(QString sectionAppearance READ sectionAppearance CONSTANT)
    Q_PROPERTY(QString themeSystem READ themeSystem CONSTANT)
    Q_PROPERTY(QString themeLight READ themeLight CONSTANT)
    Q_PROPERTY(QString themeDark READ themeDark CONSTANT)
    Q_PROPERTY(QString motionSystem READ motionSystem CONSTANT)
    Q_PROPERTY(QString motionReduced READ motionReduced CONSTANT)
    Q_PROPERTY(QString canvasColor READ canvasColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString surfaceColor READ surfaceColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString navigationColor READ navigationColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString surfaceRaisedColor READ surfaceRaisedColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString textPrimaryColor READ textPrimaryColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString textSecondaryColor READ textSecondaryColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString accentColor READ accentColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString accentTextColor READ accentTextColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString focusColor READ focusColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString borderColor READ borderColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString errorColor READ errorColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString warningColor READ warningColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString successColor READ successColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString disabledTextColor READ disabledTextColor NOTIFY appearanceChanged)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY appearanceChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY appearanceChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY appearanceChanged)
    Q_PROPERTY(int panelTransitionMs READ panelTransitionMs NOTIFY appearanceChanged)
    Q_PROPERTY(int feedbackTransitionMs READ feedbackTransitionMs NOTIFY appearanceChanged)
    Q_PROPERTY(QUrl placeholderSource READ placeholderSource CONSTANT)
    Q_PROPERTY(QUrl trackSource READ trackSource CONSTANT)

  public:
    explicit ShellUiContract(QObject* parent);
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    explicit ShellUiContract(const FixtureBundle& bundle, QObject* parent = nullptr);
#endif
    void updateAppearance(ThemeMode theme, MotionMode motion, bool system_dark, bool system_reduced_motion,
                          std::optional<HighContrastPalette> high_contrast = std::nullopt);
    [[nodiscard]] int clientWidth() const noexcept;
    [[nodiscard]] int clientHeight() const noexcept;
    [[nodiscard]] int minimumWidth() const noexcept;
    [[nodiscard]] int minimumHeight() const noexcept;
    [[nodiscard]] int queuePanelMinimumWidth() const noexcept;
    [[nodiscard]] int queuePanelMaximumWidth() const noexcept;
    [[nodiscard]] int controlTargetMinimum() const noexcept;
    [[nodiscard]] int controlTargetPreferred() const noexcept;
    [[nodiscard]] int radiusMedium() const noexcept;
    [[nodiscard]] int radiusSmall() const noexcept;
    [[nodiscard]] int spacingUnit() const noexcept;
    [[nodiscard]] QString appName() const;
    [[nodiscard]] QString headingFontFamily() const;
    [[nodiscard]] QString bodyFontFamily() const;
    [[nodiscard]] QString symbolFontFamily() const;
    [[nodiscard]] QFont applicationFont() const;
    [[nodiscard]] QString openAudioText() const;
    [[nodiscard]] QString emptyTitle() const;
    [[nodiscard]] QString emptyHint() const;
    [[nodiscard]] QString actionPlay() const;
    [[nodiscard]] QString actionPause() const;
    [[nodiscard]] QString actionPrevious() const;
    [[nodiscard]] QString actionNext() const;
    [[nodiscard]] QString skipBackwardShort() const;
    [[nodiscard]] QString skipForwardShort() const;
    [[nodiscard]] QString skipBackwardName() const;
    [[nodiscard]] QString skipForwardName() const;
    [[nodiscard]] QString progressName() const;
    [[nodiscard]] QString volumeName() const;
    [[nodiscard]] QString actionMute() const;
    [[nodiscard]] QString actionUnmute() const;
    [[nodiscard]] QString queueTitle() const;
    [[nodiscard]] QString detailsTitle() const;
    [[nodiscard]] QString notInformed() const;
    [[nodiscard]] QString errorTitle() const;
    [[nodiscard]] QString errorBody() const;
    [[nodiscard]] QString actionSkip() const;
    [[nodiscard]] QString actionRetry() const;
    [[nodiscard]] QString preferencesTitle() const;
    [[nodiscard]] QString sectionGeneral() const;
    [[nodiscard]] QString sectionPlayback() const;
    [[nodiscard]] QString sectionAppearance() const;
    [[nodiscard]] QString themeSystem() const;
    [[nodiscard]] QString themeLight() const;
    [[nodiscard]] QString themeDark() const;
    [[nodiscard]] QString motionSystem() const;
    [[nodiscard]] QString motionReduced() const;
    [[nodiscard]] QString canvasColor() const;
    [[nodiscard]] QString surfaceColor() const;
    [[nodiscard]] QString navigationColor() const;
    [[nodiscard]] QString surfaceRaisedColor() const;
    [[nodiscard]] QString textPrimaryColor() const;
    [[nodiscard]] QString textSecondaryColor() const;
    [[nodiscard]] QString accentColor() const;
    [[nodiscard]] QString accentTextColor() const;
    [[nodiscard]] QString focusColor() const;
    [[nodiscard]] QString borderColor() const;
    [[nodiscard]] QString errorColor() const;
    [[nodiscard]] QString warningColor() const;
    [[nodiscard]] QString successColor() const;
    [[nodiscard]] QString disabledTextColor() const;
    [[nodiscard]] bool highContrast() const noexcept;
    [[nodiscard]] bool darkTheme() const noexcept;
    [[nodiscard]] bool reducedMotion() const noexcept;
    [[nodiscard]] int panelTransitionMs() const noexcept;
    [[nodiscard]] int feedbackTransitionMs() const noexcept;
    [[nodiscard]] QUrl placeholderSource() const;
    [[nodiscard]] QUrl trackSource() const;

  signals:
    void appearanceChanged();

  private:
    struct Window final {
        int client_width_px;
        int client_height_px;
        int minimum_width_px;
        int minimum_height_px;
    };
    struct LayoutTokens final {
        int queue_panel_min_width_px;
        int queue_panel_max_width_px;
        int control_target_min_px;
        int control_target_preferred_px;
        int radius_small_px;
        int radius_medium_px;
        int spacing_unit_px;
    };
    struct Palette final {
        QString canvas;
        QString surface;
        QString surface_raised;
        QString text_primary;
        QString text_secondary;
        QString accent;
        QString on_accent;
        QString border;
        QString focus;
        QString error;
        QString warning;
        QString success;
        QString navigation;
    };
    struct MotionTimings final {
        int panel_transition_ms;
        int feedback_transition_ms;
    };

    [[nodiscard]] const Palette& palette() const noexcept;

    Window window_{960, 640, 720, 520};
    LayoutTokens layout_{320, 380, 40, 44, 8, 12, 4};
    // Production uses Aurora. The fixture constructor restores authenticated
    // historical palettes independently; do not recolor the frozen M0 inputs.
    Palette light_palette_{QStringLiteral("#F5F1E9"), QStringLiteral("#FFFCF6"), QStringLiteral("#EEE8DD"),
                           QStringLiteral("#302D28"), QStringLiteral("#726557"), QStringLiteral("#934B32"),
                           QStringLiteral("#FFFAF4"), QStringLiteral("#A58C73"), QStringLiteral("#934B32"),
                           QStringLiteral("#9B3C32"), QStringLiteral("#795A21"), QStringLiteral("#167344"),
                           QStringLiteral("#EEE8DD")};
    Palette dark_palette_{QStringLiteral("#1B1917"), QStringLiteral("#24211D"), QStringLiteral("#2D2923"),
                          QStringLiteral("#F2EADD"), QStringLiteral("#BFB09D"), QStringLiteral("#D6A27E"),
                          QStringLiteral("#2D1C13"), QStringLiteral("#89715A"), QStringLiteral("#D6A27E"),
                          QStringLiteral("#F2ADA0"), QStringLiteral("#E2C78B"), QStringLiteral("#6DDA9B"),
                          QStringLiteral("#1E1C19")};
    MotionTimings normal_motion_{160, 120};
    MotionTimings reduced_motion_timings_{0, 0};
    std::optional<HighContrastPalette> high_contrast_;
    bool dark_theme_{false};
    bool reduced_motion_{false};
    QUrl placeholder_source_{QStringLiteral("qrc:/m0/assets/cover-placeholder.svg")};
    QUrl track_source_{QStringLiteral("qrc:/m0/assets/cover-track.svg")};
};

} // namespace music_player::m0
