#pragma once

#include "m0/adapters/fixture/FixtureRepository.hpp"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

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
    Q_PROPERTY(QString notInformed READ notInformed CONSTANT)
    Q_PROPERTY(QString errorTitle READ errorTitle CONSTANT)
    Q_PROPERTY(QString errorBody READ errorBody CONSTANT)
    Q_PROPERTY(QString actionSkip READ actionSkip CONSTANT)
    Q_PROPERTY(QString canvasColor READ canvasColor CONSTANT)
    Q_PROPERTY(QString surfaceColor READ surfaceColor CONSTANT)
    Q_PROPERTY(QString textPrimaryColor READ textPrimaryColor CONSTANT)
    Q_PROPERTY(QString textSecondaryColor READ textSecondaryColor CONSTANT)
    Q_PROPERTY(QString accentColor READ accentColor CONSTANT)
    Q_PROPERTY(QString accentTextColor READ accentTextColor CONSTANT)
    Q_PROPERTY(QString focusColor READ focusColor CONSTANT)
    Q_PROPERTY(QString borderColor READ borderColor CONSTANT)
    Q_PROPERTY(QUrl placeholderSource READ placeholderSource CONSTANT)
    Q_PROPERTY(QUrl trackSource READ trackSource CONSTANT)

  public:
    explicit ShellUiContract(const FixtureBundle& bundle, QObject* parent = nullptr);
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
    [[nodiscard]] QString notInformed() const;
    [[nodiscard]] QString errorTitle() const;
    [[nodiscard]] QString errorBody() const;
    [[nodiscard]] QString actionSkip() const;
    [[nodiscard]] QString canvasColor() const;
    [[nodiscard]] QString surfaceColor() const;
    [[nodiscard]] QString textPrimaryColor() const;
    [[nodiscard]] QString textSecondaryColor() const;
    [[nodiscard]] QString accentColor() const;
    [[nodiscard]] QString accentTextColor() const;
    [[nodiscard]] QString focusColor() const;
    [[nodiscard]] QString borderColor() const;
    [[nodiscard]] QUrl placeholderSource() const;
    [[nodiscard]] QUrl trackSource() const;

  private:
    FixtureWindow window_;
    FixtureLayoutTokens layout_;
    FixtureStrings strings_;
    FixturePalette palette_;
    QUrl placeholder_source_{QStringLiteral("qrc:/m0/assets/cover-placeholder.svg")};
    QUrl track_source_{QStringLiteral("qrc:/m0/assets/cover-track.svg")};
};

} // namespace music_player::m0
