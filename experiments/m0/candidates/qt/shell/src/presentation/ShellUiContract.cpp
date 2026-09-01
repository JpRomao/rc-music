#include "presentation/ShellUiContract.hpp"

#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

const FixtureAsset& assetById(const FixtureBundle& bundle, const QString& asset_id) {
    for (const FixtureAsset& asset : bundle.assets) {
        if (asset.asset_id == asset_id)
            return asset;
    }
    throw std::invalid_argument{"empty shell asset is missing"};
}

void validateHighContrastPalette(const HighContrastPalette& palette) {
    for (const QColor& color :
         {palette.window, palette.window_text, palette.highlight, palette.highlighted_text, palette.disabled_text}) {
        if (!color.isValid() || color.alphaF() != 1.0F)
            throw std::invalid_argument{"high contrast colors must be valid and fully opaque"};
    }
}

} // namespace

ShellUiContract::ShellUiContract(const FixtureBundle& bundle, QObject* parent)
    : QObject(parent), window_(bundle.window), layout_(bundle.layout_tokens), strings_(bundle.strings),
      themes_(bundle.themes), motion_(bundle.motion) {
    if (window_.client_width_px != 960 || window_.client_height_px != 640 || window_.minimum_width_px != 720 ||
        window_.minimum_height_px != 520 || layout_.queue_panel_min_width_px != 320 ||
        layout_.queue_panel_max_width_px != 380 || layout_.control_target_min_px != 40 ||
        layout_.control_target_preferred_px != 44 || strings_.app_name != QStringLiteral("Player de música") ||
        strings_.action_open_audio != QStringLiteral("Abrir áudio") ||
        strings_.empty_title != QStringLiteral("Abra um áudio para começar") ||
        strings_.empty_hint != QStringLiteral("ou arraste arquivos para esta janela") ||
        strings_.action_play != QStringLiteral("Reproduzir") || strings_.action_pause != QStringLiteral("Pausar") ||
        strings_.action_previous != QStringLiteral("Faixa anterior") ||
        strings_.action_next != QStringLiteral("Próxima faixa") ||
        strings_.skip_backward_short != QStringLiteral("−{seconds}") ||
        strings_.skip_forward_short != QStringLiteral("+{seconds}") ||
        strings_.skip_backward_name != QStringLiteral("Voltar {seconds} segundos") ||
        strings_.skip_forward_name != QStringLiteral("Avançar {seconds} segundos") ||
        strings_.progress_name != QStringLiteral("Posição da faixa") ||
        strings_.volume_name != QStringLiteral("Volume") || strings_.action_mute != QStringLiteral("Silenciar") ||
        strings_.action_unmute != QStringLiteral("Ativar som") || strings_.queue_title != QStringLiteral("Fila") ||
        strings_.not_informed != QStringLiteral("Não informado") ||
        strings_.error_title != QStringLiteral("Não foi possível abrir esta faixa") ||
        strings_.error_body != QStringLiteral("A faixa de teste está indisponível.") ||
        strings_.action_skip != QStringLiteral("Pular") ||
        strings_.action_retry != QStringLiteral("Tentar novamente") ||
        strings_.preferences_title != QStringLiteral("Preferências") ||
        strings_.section_general != QStringLiteral("Geral") ||
        strings_.section_playback != QStringLiteral("Reprodução") ||
        strings_.section_appearance != QStringLiteral("Aparência") ||
        strings_.theme_system != QStringLiteral("Sistema") || strings_.theme_light != QStringLiteral("Claro") ||
        strings_.theme_dark != QStringLiteral("Escuro") || strings_.motion_system != QStringLiteral("Sistema") ||
        strings_.motion_reduced != QStringLiteral("Reduzido") || themes_.light.canvas != QStringLiteral("#F6F7F9") ||
        themes_.light.accent != QStringLiteral("#4F46E5")) {
        throw std::invalid_argument{"unsupported empty shell fixture"};
    }
    const FixtureAsset& placeholder = assetById(bundle, QStringLiteral("cover-placeholder"));
    const FixtureAsset& track = assetById(bundle, QStringLiteral("cover-track"));
    if (placeholder.relative_path != QStringLiteral("assets/cover-placeholder.svg") ||
        track.relative_path != QStringLiteral("assets/cover-track.svg")) {
        throw std::invalid_argument{"empty shell asset mapping drifted"};
    }
}

void ShellUiContract::updateAppearance(ThemeMode theme, MotionMode motion, bool system_dark, bool system_reduced_motion,
                                       std::optional<HighContrastPalette> high_contrast) {
    bool next_dark = false;
    switch (theme) {
    case ThemeMode::System:
        next_dark = system_dark;
        break;
    case ThemeMode::Light:
        break;
    case ThemeMode::Dark:
        next_dark = true;
        break;
    default:
        throw std::invalid_argument{"unsupported theme mode"};
    }

    bool next_reduced = false;
    switch (motion) {
    case MotionMode::System:
        next_reduced = system_reduced_motion;
        break;
    case MotionMode::Reduced:
        next_reduced = true;
        break;
    default:
        throw std::invalid_argument{"unsupported motion mode"};
    }

    if (high_contrast) {
        validateHighContrastPalette(*high_contrast);
        // qGray estimates 8-bit brightness; 128 separates dark and light system backgrounds.
        next_dark = qGray(high_contrast->window.rgb()) < 128;
    }

    if (dark_theme_ == next_dark && reduced_motion_ == next_reduced && high_contrast_ == high_contrast)
        return;

    dark_theme_ = next_dark;
    reduced_motion_ = next_reduced;
    high_contrast_ = std::move(high_contrast);
    emit appearanceChanged();
}

int ShellUiContract::clientWidth() const noexcept { return window_.client_width_px; }
int ShellUiContract::clientHeight() const noexcept { return window_.client_height_px; }
int ShellUiContract::minimumWidth() const noexcept { return window_.minimum_width_px; }
int ShellUiContract::minimumHeight() const noexcept { return window_.minimum_height_px; }
int ShellUiContract::queuePanelMinimumWidth() const noexcept { return layout_.queue_panel_min_width_px; }
int ShellUiContract::queuePanelMaximumWidth() const noexcept { return layout_.queue_panel_max_width_px; }
int ShellUiContract::controlTargetMinimum() const noexcept { return layout_.control_target_min_px; }
int ShellUiContract::controlTargetPreferred() const noexcept { return layout_.control_target_preferred_px; }
int ShellUiContract::radiusMedium() const noexcept { return layout_.radius_medium_px; }
int ShellUiContract::radiusSmall() const noexcept { return layout_.radius_small_px; }
int ShellUiContract::spacingUnit() const noexcept { return layout_.spacing_unit_px; }
QString ShellUiContract::appName() const { return tr("Player de música"); }
QString ShellUiContract::openAudioText() const { return tr("Abrir áudio"); }
QString ShellUiContract::emptyTitle() const { return tr("Abra um áudio para começar"); }
QString ShellUiContract::emptyHint() const { return tr("ou arraste arquivos para esta janela"); }
QString ShellUiContract::actionPlay() const { return tr("Reproduzir"); }
QString ShellUiContract::actionPause() const { return tr("Pausar"); }
QString ShellUiContract::actionPrevious() const { return tr("Faixa anterior"); }
QString ShellUiContract::actionNext() const { return tr("Próxima faixa"); }
QString ShellUiContract::skipBackwardShort() const { return tr("−{seconds}"); }
QString ShellUiContract::skipForwardShort() const { return tr("+{seconds}"); }
QString ShellUiContract::skipBackwardName() const { return tr("Voltar {seconds} segundos"); }
QString ShellUiContract::skipForwardName() const { return tr("Avançar {seconds} segundos"); }
QString ShellUiContract::progressName() const { return tr("Posição da faixa"); }
QString ShellUiContract::volumeName() const { return tr("Volume"); }
QString ShellUiContract::actionMute() const { return tr("Silenciar"); }
QString ShellUiContract::actionUnmute() const { return tr("Ativar som"); }
QString ShellUiContract::queueTitle() const { return tr("Fila"); }
QString ShellUiContract::detailsTitle() const { return tr("Detalhes da faixa"); }
QString ShellUiContract::notInformed() const { return tr("Não informado"); }
QString ShellUiContract::errorTitle() const { return tr("Não foi possível abrir esta faixa"); }
QString ShellUiContract::errorBody() const { return tr("A faixa de teste está indisponível."); }
QString ShellUiContract::actionSkip() const { return tr("Pular"); }
QString ShellUiContract::actionRetry() const { return tr("Tentar novamente"); }
QString ShellUiContract::preferencesTitle() const { return tr("Preferências"); }
QString ShellUiContract::sectionGeneral() const { return tr("Geral"); }
QString ShellUiContract::sectionPlayback() const { return tr("Reprodução"); }
QString ShellUiContract::sectionAppearance() const { return tr("Aparência"); }
QString ShellUiContract::themeSystem() const { return tr("Sistema"); }
QString ShellUiContract::themeLight() const { return tr("Claro"); }
QString ShellUiContract::themeDark() const { return tr("Escuro"); }
QString ShellUiContract::motionSystem() const { return tr("Sistema"); }
QString ShellUiContract::motionReduced() const { return tr("Reduzido"); }
const FixturePalette& ShellUiContract::palette() const noexcept { return dark_theme_ ? themes_.dark : themes_.light; }
QString ShellUiContract::canvasColor() const {
    return high_contrast_ ? high_contrast_->window.name() : palette().canvas;
}
QString ShellUiContract::surfaceColor() const {
    return high_contrast_ ? high_contrast_->window.name() : palette().surface;
}
QString ShellUiContract::surfaceRaisedColor() const {
    return high_contrast_ ? high_contrast_->window.name() : palette().surface_raised;
}
QString ShellUiContract::textPrimaryColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().text_primary;
}
QString ShellUiContract::textSecondaryColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().text_secondary;
}
QString ShellUiContract::accentColor() const {
    return high_contrast_ ? high_contrast_->highlight.name() : palette().accent;
}
QString ShellUiContract::accentTextColor() const {
    return high_contrast_ ? high_contrast_->highlighted_text.name() : palette().on_accent;
}
QString ShellUiContract::focusColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().focus;
}
QString ShellUiContract::borderColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().border;
}
QString ShellUiContract::errorColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().error;
}
QString ShellUiContract::warningColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().warning;
}
QString ShellUiContract::successColor() const {
    return high_contrast_ ? high_contrast_->window_text.name() : palette().success;
}
QString ShellUiContract::disabledTextColor() const {
    return high_contrast_ ? high_contrast_->disabled_text.name() : textPrimaryColor();
}
bool ShellUiContract::highContrast() const noexcept { return high_contrast_.has_value(); }
bool ShellUiContract::darkTheme() const noexcept { return dark_theme_; }
bool ShellUiContract::reducedMotion() const noexcept { return reduced_motion_; }
int ShellUiContract::panelTransitionMs() const noexcept {
    return reduced_motion_ ? motion_.reduced.panel_transition_ms : motion_.normal.panel_transition_ms;
}
int ShellUiContract::feedbackTransitionMs() const noexcept {
    return reduced_motion_ ? motion_.reduced.feedback_transition_ms : motion_.normal.feedback_transition_ms;
}
QUrl ShellUiContract::placeholderSource() const { return placeholder_source_; }
QUrl ShellUiContract::trackSource() const { return track_source_; }

} // namespace music_player::m0
