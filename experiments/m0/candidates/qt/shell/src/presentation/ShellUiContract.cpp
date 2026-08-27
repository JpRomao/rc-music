#include "presentation/ShellUiContract.hpp"

#include <stdexcept>

namespace music_player::m0 {
namespace {

const FixtureAsset& assetById(const FixtureBundle& bundle, const QString& asset_id) {
    for (const FixtureAsset& asset : bundle.assets) {
        if (asset.asset_id == asset_id)
            return asset;
    }
    throw std::invalid_argument{"empty shell asset is missing"};
}

} // namespace

ShellUiContract::ShellUiContract(const FixtureBundle& bundle, QObject* parent)
    : QObject(parent), window_(bundle.window), layout_(bundle.layout_tokens), strings_(bundle.strings),
      palette_(bundle.themes.light) {
    if (window_.client_width_px != 960 || window_.client_height_px != 640 || window_.minimum_width_px != 720 ||
        window_.minimum_height_px != 520 || layout_.control_target_min_px != 40 ||
        layout_.control_target_preferred_px != 44 || strings_.app_name != QStringLiteral("Player de música") ||
        strings_.action_open_audio != QStringLiteral("Abrir áudio") ||
        strings_.empty_title != QStringLiteral("Abra um áudio para começar") ||
        strings_.empty_hint != QStringLiteral("ou arraste arquivos para esta janela") ||
        palette_.canvas != QStringLiteral("#F6F7F9") || palette_.accent != QStringLiteral("#4F46E5")) {
        throw std::invalid_argument{"unsupported empty shell fixture"};
    }
    const FixtureAsset& placeholder = assetById(bundle, QStringLiteral("cover-placeholder"));
    const FixtureAsset& track = assetById(bundle, QStringLiteral("cover-track"));
    if (placeholder.relative_path != QStringLiteral("assets/cover-placeholder.svg") ||
        track.relative_path != QStringLiteral("assets/cover-track.svg")) {
        throw std::invalid_argument{"empty shell asset mapping drifted"};
    }
}
int ShellUiContract::clientWidth() const noexcept { return window_.client_width_px; }
int ShellUiContract::clientHeight() const noexcept { return window_.client_height_px; }
int ShellUiContract::minimumWidth() const noexcept { return window_.minimum_width_px; }
int ShellUiContract::minimumHeight() const noexcept { return window_.minimum_height_px; }
int ShellUiContract::controlTargetMinimum() const noexcept { return layout_.control_target_min_px; }
int ShellUiContract::controlTargetPreferred() const noexcept { return layout_.control_target_preferred_px; }
int ShellUiContract::radiusMedium() const noexcept { return layout_.radius_medium_px; }
int ShellUiContract::radiusSmall() const noexcept { return layout_.radius_small_px; }
int ShellUiContract::spacingUnit() const noexcept { return layout_.spacing_unit_px; }
QString ShellUiContract::appName() const { return strings_.app_name; }
QString ShellUiContract::openAudioText() const { return strings_.action_open_audio; }
QString ShellUiContract::emptyTitle() const { return strings_.empty_title; }
QString ShellUiContract::emptyHint() const { return strings_.empty_hint; }
QString ShellUiContract::canvasColor() const { return palette_.canvas; }
QString ShellUiContract::surfaceColor() const { return palette_.surface; }
QString ShellUiContract::textPrimaryColor() const { return palette_.text_primary; }
QString ShellUiContract::textSecondaryColor() const { return palette_.text_secondary; }
QString ShellUiContract::accentColor() const { return palette_.accent; }
QString ShellUiContract::accentTextColor() const { return palette_.on_accent; }
QString ShellUiContract::focusColor() const { return palette_.focus; }
QString ShellUiContract::borderColor() const { return palette_.border; }
QUrl ShellUiContract::placeholderSource() const { return placeholder_source_; }
QUrl ShellUiContract::trackSource() const { return track_source_; }

} // namespace music_player::m0
