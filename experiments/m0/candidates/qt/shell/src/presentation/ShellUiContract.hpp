#pragma once

#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/domain/PlaybackTypes.hpp"
#include "presentation/HighContrastPalette.hpp"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace music_player::m0 {

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
    explicit ShellUiContract(const FixtureBundle& bundle, QObject* parent = nullptr);
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
    [[nodiscard]] const FixturePalette& palette() const noexcept;

    FixtureWindow window_;
    FixtureLayoutTokens layout_;
    FixtureStrings strings_;
    FixtureThemes themes_;
    FixtureMotion motion_;
    std::optional<HighContrastPalette> high_contrast_;
    bool dark_theme_{false};
    bool reduced_motion_{false};
    QUrl placeholder_source_{QStringLiteral("qrc:/m0/assets/cover-placeholder.svg")};
    QUrl track_source_{QStringLiteral("qrc:/m0/assets/cover-track.svg")};
};

} // namespace music_player::m0
