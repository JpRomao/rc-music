#include "presentation/ShellUiContract.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/ApplicationIdentity.hpp"

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

ShellUiContract::ShellUiContract(QObject* parent) : QObject(parent) {}

ShellUiContract::ShellUiContract(const FixtureBundle& bundle, QObject* parent) : ShellUiContract(parent) {
    const FixtureWindow& window = bundle.window;
    const FixtureLayoutTokens& layout = bundle.layout_tokens;
    const FixtureStrings& strings = bundle.strings;
    const FixtureThemes& themes = bundle.themes;
    if (window.client_width_px != 960 || window.client_height_px != 640 || window.minimum_width_px != 720 ||
        window.minimum_height_px != 520 || layout.queue_panel_min_width_px != 320 ||
        layout.queue_panel_max_width_px != 380 || layout.control_target_min_px != 40 ||
        layout.control_target_preferred_px != 44 || strings.app_name != QStringLiteral("Player de música") ||
        strings.action_open_audio != QStringLiteral("Abrir áudio") ||
        strings.empty_title != QStringLiteral("Abra um áudio para começar") ||
        strings.empty_hint != QStringLiteral("ou arraste arquivos para esta janela") ||
        strings.action_play != QStringLiteral("Reproduzir") || strings.action_pause != QStringLiteral("Pausar") ||
        strings.action_previous != QStringLiteral("Faixa anterior") ||
        strings.action_next != QStringLiteral("Próxima faixa") ||
        strings.skip_backward_short != QStringLiteral("−{seconds}") ||
        strings.skip_forward_short != QStringLiteral("+{seconds}") ||
        strings.skip_backward_name != QStringLiteral("Voltar {seconds} segundos") ||
        strings.skip_forward_name != QStringLiteral("Avançar {seconds} segundos") ||
        strings.progress_name != QStringLiteral("Posição da faixa") ||
        strings.volume_name != QStringLiteral("Volume") || strings.action_mute != QStringLiteral("Silenciar") ||
        strings.action_unmute != QStringLiteral("Ativar som") || strings.queue_title != QStringLiteral("Fila") ||
        strings.not_informed != QStringLiteral("Não informado") ||
        strings.error_title != QStringLiteral("Não foi possível abrir esta faixa") ||
        strings.error_body != QStringLiteral("A faixa de teste está indisponível.") ||
        strings.action_skip != QStringLiteral("Pular") || strings.action_retry != QStringLiteral("Tentar novamente") ||
        strings.preferences_title != QStringLiteral("Preferências") ||
        strings.section_general != QStringLiteral("Geral") ||
        strings.section_playback != QStringLiteral("Reprodução") ||
        strings.section_appearance != QStringLiteral("Aparência") ||
        strings.theme_system != QStringLiteral("Sistema") || strings.theme_light != QStringLiteral("Claro") ||
        strings.theme_dark != QStringLiteral("Escuro") || strings.motion_system != QStringLiteral("Sistema") ||
        strings.motion_reduced != QStringLiteral("Reduzido") || themes.light.canvas != QStringLiteral("#F6F7F9") ||
        themes.light.accent != QStringLiteral("#4F46E5")) {
        throw std::invalid_argument{"unsupported empty shell fixture"};
    }
    const FixtureAsset& placeholder = assetById(bundle, QStringLiteral("cover-placeholder"));
    const FixtureAsset& track = assetById(bundle, QStringLiteral("cover-track"));
    if (placeholder.relative_path != QStringLiteral("assets/cover-placeholder.svg") ||
        track.relative_path != QStringLiteral("assets/cover-track.svg")) {
        throw std::invalid_argument{"empty shell asset mapping drifted"};
    }

    window_ = {window.client_width_px, window.client_height_px, window.minimum_width_px, window.minimum_height_px};
    layout_ = {layout.queue_panel_min_width_px, layout.queue_panel_max_width_px,
               layout.control_target_min_px,    layout.control_target_preferred_px,
               layout.radius_small_px,          layout.radius_medium_px,
               layout.spacing_unit_px};
    const auto convert_palette = [](const FixturePalette& source) {
        return Palette{source.canvas,         source.surface, source.surface_raised, source.text_primary,
                       source.text_secondary, source.accent,  source.on_accent,      source.border,
                       source.focus,          source.error,   source.warning,        source.success};
    };
    light_palette_ = convert_palette(themes.light);
    dark_palette_ = convert_palette(themes.dark);
    normal_motion_ = {bundle.motion.normal.panel_transition_ms, bundle.motion.normal.feedback_transition_ms};
    reduced_motion_timings_ = {bundle.motion.reduced.panel_transition_ms, bundle.motion.reduced.feedback_transition_ms};
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
QString ShellUiContract::appName() const { return ApplicationIdentity::displayName(); }
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
const ShellUiContract::Palette& ShellUiContract::palette() const noexcept {
    return dark_theme_ ? dark_palette_ : light_palette_;
}
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
    return reduced_motion_ ? reduced_motion_timings_.panel_transition_ms : normal_motion_.panel_transition_ms;
}
int ShellUiContract::feedbackTransitionMs() const noexcept {
    return reduced_motion_ ? reduced_motion_timings_.feedback_transition_ms : normal_motion_.feedback_transition_ms;
}
QUrl ShellUiContract::placeholderSource() const { return placeholder_source_; }
QUrl ShellUiContract::trackSource() const { return track_source_; }

} // namespace music_player::m0
