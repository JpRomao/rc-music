#include "adapters/fixture/FixtureDecoder.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/presentation/QueueListModel.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <concepts>
#include <optional>

namespace music_player::m0::test {

class FixtureRepositoryTests final : public QObject {
    Q_OBJECT

  private slots:
    void exposesTypedPresentationFixtureFields();
    void preservesCompleteFixtureStateNullability();
    void exposesTypedTrackDetailsAndOrderedAssets();
    void preservesQueueItemErrorCodeNullability();
    void exposesTypedPreferencesContract();
    void loadsFrozenFixtureAndQueueReadOnly();
    void preservesHostileFixedCases();
    void queueModelExposesTypedRolesInSourceOrder();
    void rejectsSemanticFixtureDrift_data();
    void rejectsSemanticFixtureDrift();
    void rejectsQueueCountDrift();
    void rejectsHostileCaseDrift();
    void rejectsByteDriftBeforeSemanticDecode_data();
    void rejectsByteDriftBeforeSemanticDecode();
    void rejectsIntegerNarrowingAliases();
    void rejectsIntegerOutsideSigned64BitRange();
};

template <typename Bundle>
concept HasTypedPresentationFixture = requires(Bundle bundle) {
    { bundle.window.client_width_px } -> std::same_as<int&>;
    { bundle.window.client_height_px } -> std::same_as<int&>;
    { bundle.window.minimum_width_px } -> std::same_as<int&>;
    { bundle.window.minimum_height_px } -> std::same_as<int&>;
    { bundle.layout_tokens.queue_panel_min_width_px } -> std::same_as<int&>;
    { bundle.layout_tokens.queue_panel_max_width_px } -> std::same_as<int&>;
    { bundle.layout_tokens.control_target_min_px } -> std::same_as<int&>;
    { bundle.layout_tokens.control_target_preferred_px } -> std::same_as<int&>;
    { bundle.layout_tokens.radius_small_px } -> std::same_as<int&>;
    { bundle.layout_tokens.radius_medium_px } -> std::same_as<int&>;
    { bundle.layout_tokens.spacing_unit_px } -> std::same_as<int&>;
    { bundle.strings.app_name } -> std::same_as<QString&>;
    { bundle.strings.action_open_audio } -> std::same_as<QString&>;
    { bundle.strings.empty_title } -> std::same_as<QString&>;
    { bundle.strings.empty_hint } -> std::same_as<QString&>;
    { bundle.strings.action_play } -> std::same_as<QString&>;
    { bundle.strings.action_pause } -> std::same_as<QString&>;
    { bundle.strings.action_previous } -> std::same_as<QString&>;
    { bundle.strings.action_next } -> std::same_as<QString&>;
    { bundle.strings.skip_backward_short } -> std::same_as<QString&>;
    { bundle.strings.skip_forward_short } -> std::same_as<QString&>;
    { bundle.strings.skip_backward_name } -> std::same_as<QString&>;
    { bundle.strings.skip_forward_name } -> std::same_as<QString&>;
    { bundle.strings.progress_name } -> std::same_as<QString&>;
    { bundle.strings.volume_name } -> std::same_as<QString&>;
    { bundle.strings.action_mute } -> std::same_as<QString&>;
    { bundle.strings.action_unmute } -> std::same_as<QString&>;
    { bundle.strings.queue_title } -> std::same_as<QString&>;
    { bundle.strings.preferences_title } -> std::same_as<QString&>;
    { bundle.strings.details_title } -> std::same_as<QString&>;
    { bundle.strings.section_general } -> std::same_as<QString&>;
    { bundle.strings.section_playback } -> std::same_as<QString&>;
    { bundle.strings.section_appearance } -> std::same_as<QString&>;
    { bundle.strings.theme_system } -> std::same_as<QString&>;
    { bundle.strings.theme_light } -> std::same_as<QString&>;
    { bundle.strings.theme_dark } -> std::same_as<QString&>;
    { bundle.strings.motion_system } -> std::same_as<QString&>;
    { bundle.strings.motion_reduced } -> std::same_as<QString&>;
    { bundle.strings.error_title } -> std::same_as<QString&>;
    { bundle.strings.error_body } -> std::same_as<QString&>;
    { bundle.strings.action_skip } -> std::same_as<QString&>;
    { bundle.strings.action_retry } -> std::same_as<QString&>;
    { bundle.strings.not_informed } -> std::same_as<QString&>;
    { bundle.themes.available_modes } -> std::same_as<QStringList&>;
    { bundle.themes.light.canvas } -> std::same_as<QString&>;
    { bundle.themes.light.surface } -> std::same_as<QString&>;
    { bundle.themes.light.surface_raised } -> std::same_as<QString&>;
    { bundle.themes.light.text_primary } -> std::same_as<QString&>;
    { bundle.themes.light.text_secondary } -> std::same_as<QString&>;
    { bundle.themes.light.accent } -> std::same_as<QString&>;
    { bundle.themes.light.on_accent } -> std::same_as<QString&>;
    { bundle.themes.light.border } -> std::same_as<QString&>;
    { bundle.themes.light.focus } -> std::same_as<QString&>;
    { bundle.themes.light.error } -> std::same_as<QString&>;
    { bundle.themes.light.warning } -> std::same_as<QString&>;
    { bundle.themes.light.success } -> std::same_as<QString&>;
    { bundle.themes.dark.canvas } -> std::same_as<QString&>;
    { bundle.themes.dark.surface } -> std::same_as<QString&>;
    { bundle.themes.dark.surface_raised } -> std::same_as<QString&>;
    { bundle.themes.dark.text_primary } -> std::same_as<QString&>;
    { bundle.themes.dark.text_secondary } -> std::same_as<QString&>;
    { bundle.themes.dark.accent } -> std::same_as<QString&>;
    { bundle.themes.dark.on_accent } -> std::same_as<QString&>;
    { bundle.themes.dark.border } -> std::same_as<QString&>;
    { bundle.themes.dark.focus } -> std::same_as<QString&>;
    { bundle.themes.dark.error } -> std::same_as<QString&>;
    { bundle.themes.dark.warning } -> std::same_as<QString&>;
    { bundle.themes.dark.success } -> std::same_as<QString&>;
    { bundle.motion.available_modes } -> std::same_as<QStringList&>;
    { bundle.motion.normal.panel_transition_ms } -> std::same_as<int&>;
    { bundle.motion.normal.feedback_transition_ms } -> std::same_as<int&>;
    { bundle.motion.normal.continuous_animation } -> std::same_as<bool&>;
    { bundle.motion.reduced.panel_transition_ms } -> std::same_as<int&>;
    { bundle.motion.reduced.feedback_transition_ms } -> std::same_as<int&>;
    { bundle.motion.reduced.continuous_animation } -> std::same_as<bool&>;
    { bundle.fake_backend.clock.kind } -> std::same_as<QString&>;
    { bundle.fake_backend.clock.initial_monotonic_ms } -> std::same_as<std::int64_t&>;
    { bundle.fake_backend.snapshots.frequency_hz } -> std::same_as<int&>;
    { bundle.fake_backend.snapshots.interval_ms } -> std::same_as<std::int64_t&>;
    { bundle.fake_backend.snapshots.start_position_ms } -> std::same_as<std::int64_t&>;
    { bundle.fake_backend.snapshots.duration_ms } -> std::same_as<std::int64_t&>;
    { bundle.fake_backend.snapshots.increment_ms } -> std::same_as<std::int64_t&>;
    { bundle.fake_backend.snapshots.end_behavior } -> std::same_as<QString&>;
    { bundle.fake_backend.seek.target_ms } -> std::same_as<std::int64_t&>;
    { bundle.fake_backend.seek.preview_immediate } -> std::same_as<bool&>;
    { bundle.fake_backend.seek.ack_after_ticks } -> std::same_as<int&>;
    { bundle.fake_backend.seek.ack_position_ms } -> std::same_as<std::int64_t&>;
    { bundle.queue_contract.relative_path } -> std::same_as<QString&>;
    { bundle.queue_contract.queue_id } -> std::same_as<QString&>;
    { bundle.queue_contract.expected_item_count } -> std::same_as<int&>;
    { bundle.queue_contract.load_action_id } -> std::same_as<QString&>;
};

template <typename State>
concept HasCompleteNullableFixtureState = requires(State state) {
    { state.state_id } -> std::same_as<QString&>;
    { state.playback_state } -> std::same_as<QString&>;
    { state.queue_loaded } -> std::same_as<bool&>;
    { state.current_occurrence_id } -> std::same_as<std::optional<QString>&>;
    { state.context_occurrence_id } -> std::same_as<std::optional<QString>&>;
    { state.position_ms } -> std::same_as<std::optional<std::int64_t>&>;
    { state.duration_ms } -> std::same_as<std::optional<std::int64_t>&>;
    { state.seekable } -> std::same_as<bool&>;
    { state.asset_id } -> std::same_as<QString&>;
    { state.message_key } -> std::same_as<std::optional<QString>&>;
    { state.primary_action_id } -> std::same_as<QString&>;
    { state.error_code } -> std::same_as<std::optional<QString>&>;
};

template <typename Bundle>
concept HasTypedTrackDetailsAndAssets = requires(Bundle bundle) {
    { bundle.track_details.occurrence_id } -> std::same_as<QString&>;
    { bundle.track_details.synthetic } -> std::same_as<bool&>;
    { bundle.track_details.size_bytes } -> std::same_as<std::int64_t&>;
    { bundle.track_details.modified_utc } -> std::same_as<QString&>;
    { bundle.track_details.container } -> std::same_as<QString&>;
    { bundle.track_details.codec } -> std::same_as<QString&>;
    { bundle.track_details.bitrate_bps } -> std::same_as<int&>;
    { bundle.track_details.sample_rate_hz } -> std::same_as<int&>;
    { bundle.track_details.channels } -> std::same_as<int&>;
    { bundle.track_details.bit_depth } -> std::same_as<std::optional<int>&>;
    { bundle.assets.size() } -> std::same_as<qsizetype>;
    { bundle.assets.at(0).asset_id } -> std::same_as<const QString&>;
    { bundle.assets.at(0).relative_path } -> std::same_as<const QString&>;
    { bundle.assets.at(0).alt_key } -> std::same_as<const QString&>;
};

template <typename Bundle>
concept HasNullableQueueItemError = requires(Bundle bundle) {
    { bundle.queue.at(0).error_code } -> std::same_as<const std::optional<QString>&>;
};

template <typename Bundle>
concept HasCompleteTypedPreferences = requires(Bundle bundle) {
    { bundle.preferences.defaults.theme } -> std::same_as<QString&>;
    { bundle.preferences.defaults.motion } -> std::same_as<QString&>;
    { bundle.preferences.defaults.volume_percent } -> std::same_as<int&>;
    { bundle.preferences.defaults.muted } -> std::same_as<bool&>;
    { bundle.preferences.defaults.skip_backward_seconds } -> std::same_as<int&>;
    { bundle.preferences.defaults.skip_forward_seconds } -> std::same_as<int&>;
    { bundle.preferences.defaults.close_behavior } -> std::same_as<QString&>;
    { bundle.preferences.defaults.restore_last_session } -> std::same_as<bool&>;
    { bundle.preferences.defaults.start_minimized } -> std::same_as<bool&>;
    { bundle.preferences.defaults.queue_end_behavior } -> std::same_as<QString&>;
    { bundle.preferences.constraints.volume_percent.minimum } -> std::same_as<int&>;
    { bundle.preferences.constraints.volume_percent.maximum } -> std::same_as<int&>;
    { bundle.preferences.constraints.skip_seconds.minimum } -> std::same_as<int&>;
    { bundle.preferences.constraints.skip_seconds.maximum } -> std::same_as<int&>;
    { bundle.preferences.constraints.close_behavior } -> std::same_as<QStringList&>;
    { bundle.preferences.constraints.queue_end_behavior } -> std::same_as<QStringList&>;
    { bundle.preferences.rules.close_without_tray } -> std::same_as<QString&>;
    { bundle.preferences.rules.start_minimized_requires_tray } -> std::same_as<bool&>;
    { bundle.preferences.rules.restored_playback_state } -> std::same_as<QString&>;
    { bundle.preferences.rules.unmute_restores_last_nonzero_volume } -> std::same_as<bool&>;
};

[[nodiscard]] QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void expectSemanticDecodeFailure(const QJsonObject& fixture, const QJsonObject& queue,
                                 const QString& expected_message) {
    try {
        static_cast<void>(detail::decodeAuthenticatedDocuments(fixture, queue));
    } catch (const FixtureValidationError& error) {
        QCOMPARE(QString::fromStdString(error.what()), expected_message);
        return;
    }
    QFAIL("semantic fixture drift unexpectedly decoded");
}

[[nodiscard]] const QueueItem& itemById(const FixtureBundle& bundle, const QString& occurrence_id) {
    const auto found = std::find_if(bundle.queue.cbegin(), bundle.queue.cend(),
                                    [&](const QueueItem& item) { return item.occurrence_id == occurrence_id; });
    if (found == bundle.queue.cend()) {
        throw FixtureValidationError(QStringLiteral("missing test item"));
    }
    return *found;
}

template <typename Bundle> void verifyTypedPresentationFixture() {
    if constexpr (!HasTypedPresentationFixture<Bundle>) {
        QFAIL("FixtureBundle is missing the typed presentation fixture contract");
    } else {
        const Bundle bundle =
            FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));

        QCOMPARE(bundle.window.client_width_px, 960);
        QCOMPARE(bundle.window.client_height_px, 640);
        QCOMPARE(bundle.window.minimum_width_px, 720);
        QCOMPARE(bundle.window.minimum_height_px, 520);

        QCOMPARE(bundle.layout_tokens.queue_panel_min_width_px, 320);
        QCOMPARE(bundle.layout_tokens.queue_panel_max_width_px, 380);
        QCOMPARE(bundle.layout_tokens.control_target_min_px, 40);
        QCOMPARE(bundle.layout_tokens.control_target_preferred_px, 44);
        QCOMPARE(bundle.layout_tokens.radius_small_px, 8);
        QCOMPARE(bundle.layout_tokens.radius_medium_px, 12);
        QCOMPARE(bundle.layout_tokens.spacing_unit_px, 4);

        QCOMPARE(bundle.strings.app_name, QStringLiteral("Player de música"));
        QCOMPARE(bundle.strings.action_open_audio, QStringLiteral("Abrir áudio"));
        QCOMPARE(bundle.strings.empty_title, QStringLiteral("Abra um áudio para começar"));
        QCOMPARE(bundle.strings.empty_hint, QStringLiteral("ou arraste arquivos para esta janela"));
        QCOMPARE(bundle.strings.action_play, QStringLiteral("Reproduzir"));
        QCOMPARE(bundle.strings.action_pause, QStringLiteral("Pausar"));
        QCOMPARE(bundle.strings.action_previous, QStringLiteral("Faixa anterior"));
        QCOMPARE(bundle.strings.action_next, QStringLiteral("Próxima faixa"));
        QCOMPARE(bundle.strings.skip_backward_short, QStringLiteral("−{seconds}"));
        QCOMPARE(bundle.strings.skip_forward_short, QStringLiteral("+{seconds}"));
        QCOMPARE(bundle.strings.skip_backward_name, QStringLiteral("Voltar {seconds} segundos"));
        QCOMPARE(bundle.strings.skip_forward_name, QStringLiteral("Avançar {seconds} segundos"));
        QCOMPARE(bundle.strings.progress_name, QStringLiteral("Posição da faixa"));
        QCOMPARE(bundle.strings.volume_name, QStringLiteral("Volume"));
        QCOMPARE(bundle.strings.action_mute, QStringLiteral("Silenciar"));
        QCOMPARE(bundle.strings.action_unmute, QStringLiteral("Ativar som"));
        QCOMPARE(bundle.strings.queue_title, QStringLiteral("Fila"));
        QCOMPARE(bundle.strings.preferences_title, QStringLiteral("Preferências"));
        QCOMPARE(bundle.strings.details_title, QStringLiteral("Detalhes da faixa"));
        QCOMPARE(bundle.strings.section_general, QStringLiteral("Geral"));
        QCOMPARE(bundle.strings.section_playback, QStringLiteral("Reprodução"));
        QCOMPARE(bundle.strings.section_appearance, QStringLiteral("Aparência"));
        QCOMPARE(bundle.strings.theme_system, QStringLiteral("Sistema"));
        QCOMPARE(bundle.strings.theme_light, QStringLiteral("Claro"));
        QCOMPARE(bundle.strings.theme_dark, QStringLiteral("Escuro"));
        QCOMPARE(bundle.strings.motion_system, QStringLiteral("Sistema"));
        QCOMPARE(bundle.strings.motion_reduced, QStringLiteral("Reduzido"));
        QCOMPARE(bundle.strings.error_title, QStringLiteral("Não foi possível abrir esta faixa"));
        QCOMPARE(bundle.strings.error_body, QStringLiteral("A faixa de teste está indisponível."));
        QCOMPARE(bundle.strings.action_skip, QStringLiteral("Pular"));
        QCOMPARE(bundle.strings.action_retry, QStringLiteral("Tentar novamente"));
        QCOMPARE(bundle.strings.not_informed, QStringLiteral("Não informado"));

        const QStringList expected_theme_modes{QStringLiteral("system"), QStringLiteral("light"),
                                               QStringLiteral("dark")};
        QCOMPARE(bundle.themes.available_modes, expected_theme_modes);
        QCOMPARE(bundle.themes.light.canvas, QStringLiteral("#F6F7F9"));
        QCOMPARE(bundle.themes.light.surface, QStringLiteral("#FFFFFF"));
        QCOMPARE(bundle.themes.light.surface_raised, QStringLiteral("#FFFFFF"));
        QCOMPARE(bundle.themes.light.text_primary, QStringLiteral("#15171A"));
        QCOMPARE(bundle.themes.light.text_secondary, QStringLiteral("#4F5660"));
        QCOMPARE(bundle.themes.light.accent, QStringLiteral("#4F46E5"));
        QCOMPARE(bundle.themes.light.on_accent, QStringLiteral("#FFFFFF"));
        QCOMPARE(bundle.themes.light.border, QStringLiteral("#8A929E"));
        QCOMPARE(bundle.themes.light.focus, QStringLiteral("#2457D6"));
        QCOMPARE(bundle.themes.light.error, QStringLiteral("#B42318"));
        QCOMPARE(bundle.themes.light.warning, QStringLiteral("#8A4B08"));
        QCOMPARE(bundle.themes.light.success, QStringLiteral("#167344"));
        QCOMPARE(bundle.themes.dark.canvas, QStringLiteral("#111318"));
        QCOMPARE(bundle.themes.dark.surface, QStringLiteral("#181B21"));
        QCOMPARE(bundle.themes.dark.surface_raised, QStringLiteral("#21252D"));
        QCOMPARE(bundle.themes.dark.text_primary, QStringLiteral("#F5F7FA"));
        QCOMPARE(bundle.themes.dark.text_secondary, QStringLiteral("#BBC2CC"));
        QCOMPARE(bundle.themes.dark.accent, QStringLiteral("#8B83FF"));
        QCOMPARE(bundle.themes.dark.on_accent, QStringLiteral("#0B0C10"));
        QCOMPARE(bundle.themes.dark.border, QStringLiteral("#69717D"));
        QCOMPARE(bundle.themes.dark.focus, QStringLiteral("#A9C2FF"));
        QCOMPARE(bundle.themes.dark.error, QStringLiteral("#FF8A80"));
        QCOMPARE(bundle.themes.dark.warning, QStringLiteral("#FFC46B"));
        QCOMPARE(bundle.themes.dark.success, QStringLiteral("#6DDA9B"));

        const QStringList expected_motion_modes{QStringLiteral("system"), QStringLiteral("reduced")};
        QCOMPARE(bundle.motion.available_modes, expected_motion_modes);
        QCOMPARE(bundle.motion.normal.panel_transition_ms, 160);
        QCOMPARE(bundle.motion.normal.feedback_transition_ms, 120);
        QCOMPARE(bundle.motion.normal.continuous_animation, false);
        QCOMPARE(bundle.motion.reduced.panel_transition_ms, 0);
        QCOMPARE(bundle.motion.reduced.feedback_transition_ms, 0);
        QCOMPARE(bundle.motion.reduced.continuous_animation, false);

        QCOMPARE(bundle.fake_backend.clock.kind, QStringLiteral("deterministic"));
        QCOMPARE(bundle.fake_backend.clock.initial_monotonic_ms, std::int64_t{0});
        QCOMPARE(bundle.fake_backend.snapshots.frequency_hz, 5);
        QCOMPARE(bundle.fake_backend.snapshots.interval_ms, std::int64_t{200});
        QCOMPARE(bundle.fake_backend.snapshots.start_position_ms, std::int64_t{84'000});
        QCOMPARE(bundle.fake_backend.snapshots.duration_ms, std::int64_t{248'000});
        QCOMPARE(bundle.fake_backend.snapshots.increment_ms, std::int64_t{200});
        QCOMPARE(bundle.fake_backend.snapshots.end_behavior, QStringLiteral("clamp_paused"));
        QCOMPARE(bundle.fake_backend.seek.target_ms, std::int64_t{124'000});
        QCOMPARE(bundle.fake_backend.seek.preview_immediate, true);
        QCOMPARE(bundle.fake_backend.seek.ack_after_ticks, 1);
        QCOMPARE(bundle.fake_backend.seek.ack_position_ms, std::int64_t{124'000});

        QCOMPARE(bundle.queue_contract.relative_path, QStringLiteral("queue.v1.json"));
        QCOMPARE(bundle.queue_contract.queue_id, QStringLiteral("M0-SHELL-Q1000"));
        QCOMPARE(bundle.queue_contract.expected_item_count, 1000);
        QCOMPARE(bundle.queue_contract.load_action_id, QStringLiteral("load-queue-1000"));
    }
}

template <typename State> void verifyCompleteFixtureStateNullability(const FixtureBundle& bundle) {
    if constexpr (!HasCompleteNullableFixtureState<State>) {
        QFAIL("FixtureState is missing fields or collapses JSON null into an empty QString");
    } else {
        QCOMPARE(bundle.states.size(), 3);
        const State& empty = bundle.states.at(0);
        QCOMPARE(empty.state_id, QStringLiteral("empty"));
        QCOMPARE(empty.playback_state, QStringLiteral("unloaded"));
        QCOMPARE(empty.queue_loaded, false);
        QVERIFY(!empty.current_occurrence_id.has_value());
        QVERIFY(!empty.context_occurrence_id.has_value());
        QVERIFY(!empty.position_ms.has_value());
        QVERIFY(!empty.duration_ms.has_value());
        QCOMPARE(empty.seekable, false);
        QCOMPARE(empty.asset_id, QStringLiteral("cover-placeholder"));
        QVERIFY(empty.message_key.has_value());
        QCOMPARE(*empty.message_key, QStringLiteral("empty_title"));
        QCOMPARE(empty.primary_action_id, QStringLiteral("open-audio"));
        QVERIFY(!empty.error_code.has_value());

        const State& playing = bundle.states.at(1);
        QCOMPARE(playing.state_id, QStringLiteral("playing"));
        QCOMPARE(playing.playback_state, QStringLiteral("playing"));
        QCOMPARE(playing.queue_loaded, true);
        QVERIFY(playing.current_occurrence_id.has_value());
        QCOMPARE(*playing.current_occurrence_id, QStringLiteral("queue-0017"));
        QVERIFY(!playing.context_occurrence_id.has_value());
        QVERIFY(playing.position_ms.has_value());
        QCOMPARE(*playing.position_ms, std::int64_t{84'000});
        QVERIFY(playing.duration_ms.has_value());
        QCOMPARE(*playing.duration_ms, std::int64_t{248'000});
        QCOMPARE(playing.seekable, true);
        QCOMPARE(playing.asset_id, QStringLiteral("cover-track"));
        QVERIFY(!playing.message_key.has_value());
        QCOMPARE(playing.primary_action_id, QStringLiteral("play-pause"));
        QVERIFY(!playing.error_code.has_value());

        const State& error = bundle.states.at(2);
        QCOMPARE(error.state_id, QStringLiteral("error"));
        QCOMPARE(error.playback_state, QStringLiteral("error"));
        QCOMPARE(error.queue_loaded, true);
        QVERIFY(error.current_occurrence_id.has_value());
        QCOMPARE(*error.current_occurrence_id, QStringLiteral("queue-0999"));
        QVERIFY(error.context_occurrence_id.has_value());
        QCOMPARE(*error.context_occurrence_id, QStringLiteral("queue-0017"));
        QVERIFY(!error.position_ms.has_value());
        QVERIFY(!error.duration_ms.has_value());
        QCOMPARE(error.seekable, false);
        QCOMPARE(error.asset_id, QStringLiteral("cover-track"));
        QVERIFY(error.message_key.has_value());
        QCOMPARE(*error.message_key, QStringLiteral("error_body"));
        QCOMPARE(error.primary_action_id, QStringLiteral("skip"));
        QVERIFY(error.error_code.has_value());
        QCOMPARE(*error.error_code, QStringLiteral("fixture_source_unavailable"));
    }
}

void FixtureRepositoryTests::exposesTypedPresentationFixtureFields() {
    verifyTypedPresentationFixture<FixtureBundle>();
}

void FixtureRepositoryTests::preservesCompleteFixtureStateNullability() {
    const FixtureBundle bundle =
        FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
    verifyCompleteFixtureStateNullability<FixtureState>(bundle);
}

template <typename Bundle> void verifyTypedTrackDetailsAndAssets() {
    if constexpr (!HasTypedTrackDetailsAndAssets<Bundle>) {
        QFAIL("FixtureBundle is missing typed track details or ordered assets");
    } else {
        const Bundle bundle =
            FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
        QCOMPARE(bundle.track_details.occurrence_id, QStringLiteral("queue-0017"));
        QCOMPARE(bundle.track_details.synthetic, true);
        QCOMPARE(bundle.track_details.size_bytes, std::int64_t{7'483'921});
        QCOMPARE(bundle.track_details.modified_utc, QStringLiteral("2026-08-23T00:00:00Z"));
        QCOMPARE(bundle.track_details.container, QStringLiteral("M4A"));
        QCOMPARE(bundle.track_details.codec, QStringLiteral("AAC-LC"));
        QCOMPARE(bundle.track_details.bitrate_bps, 256'000);
        QCOMPARE(bundle.track_details.sample_rate_hz, 48'000);
        QCOMPARE(bundle.track_details.channels, 2);
        QVERIFY(!bundle.track_details.bit_depth.has_value());

        QCOMPARE(bundle.assets.size(), 2);
        QCOMPARE(bundle.assets.at(0).asset_id, QStringLiteral("cover-placeholder"));
        QCOMPARE(bundle.assets.at(0).relative_path, QStringLiteral("assets/cover-placeholder.svg"));
        QCOMPARE(bundle.assets.at(0).alt_key, QStringLiteral("empty_title"));
        QCOMPARE(bundle.assets.at(1).asset_id, QStringLiteral("cover-track"));
        QCOMPARE(bundle.assets.at(1).relative_path, QStringLiteral("assets/cover-track.svg"));
        QCOMPARE(bundle.assets.at(1).alt_key, QStringLiteral("details_title"));
    }
}

template <typename Bundle> void verifyQueueItemErrorCodeNullability() {
    if constexpr (!HasNullableQueueItemError<Bundle>) {
        QFAIL("QueueItem.error_code collapses JSON null into an empty QString");
    } else {
        const Bundle bundle =
            FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
        QVERIFY(!bundle.queue.at(0).error_code.has_value());
        QVERIFY(bundle.queue.at(998).error_code.has_value());
        QCOMPARE(*bundle.queue.at(998).error_code, QStringLiteral("fixture_source_unavailable"));

        const QueueListModel model(bundle.queue);
        QVERIFY(!model.data(model.index(0, 0), QueueListModel::ErrorCodeRole).isValid());
        QCOMPARE(model.data(model.index(998, 0), QueueListModel::ErrorCodeRole).toString(),
                 QStringLiteral("fixture_source_unavailable"));
    }
}

void FixtureRepositoryTests::exposesTypedTrackDetailsAndOrderedAssets() {
    verifyTypedTrackDetailsAndAssets<FixtureBundle>();
}

void FixtureRepositoryTests::preservesQueueItemErrorCodeNullability() {
    verifyQueueItemErrorCodeNullability<FixtureBundle>();
}

template <typename Bundle> void verifyTypedPreferencesContract() {
    if constexpr (!HasCompleteTypedPreferences<Bundle>) {
        QFAIL("FixtureBundle is missing typed preference constraints or rules");
    } else {
        const Bundle bundle =
            FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
        QCOMPARE(bundle.preferences.defaults.theme, QStringLiteral("system"));
        QCOMPARE(bundle.preferences.defaults.motion, QStringLiteral("system"));
        QCOMPARE(bundle.preferences.defaults.volume_percent, 80);
        QCOMPARE(bundle.preferences.defaults.muted, false);
        QCOMPARE(bundle.preferences.defaults.skip_backward_seconds, 5);
        QCOMPARE(bundle.preferences.defaults.skip_forward_seconds, 5);
        QCOMPARE(bundle.preferences.defaults.close_behavior, QStringLiteral("minimize_to_tray"));
        QCOMPARE(bundle.preferences.defaults.restore_last_session, true);
        QCOMPARE(bundle.preferences.defaults.start_minimized, false);
        QCOMPARE(bundle.preferences.defaults.queue_end_behavior, QStringLiteral("stop_keep_selection"));

        QCOMPARE(bundle.preferences.constraints.volume_percent.minimum, 0);
        QCOMPARE(bundle.preferences.constraints.volume_percent.maximum, 100);
        QCOMPARE(bundle.preferences.constraints.skip_seconds.minimum, 1);
        QCOMPARE(bundle.preferences.constraints.skip_seconds.maximum, 300);
        QCOMPARE(bundle.preferences.constraints.close_behavior,
                 QStringList({QStringLiteral("minimize_to_tray"), QStringLiteral("exit")}));
        QCOMPARE(bundle.preferences.constraints.queue_end_behavior,
                 QStringList({QStringLiteral("stop_keep_selection"), QStringLiteral("stop_clear_selection")}));

        QCOMPARE(bundle.preferences.rules.close_without_tray, QStringLiteral("minimize_window"));
        QCOMPARE(bundle.preferences.rules.start_minimized_requires_tray, true);
        QCOMPARE(bundle.preferences.rules.restored_playback_state, QStringLiteral("paused"));
        QCOMPARE(bundle.preferences.rules.unmute_restores_last_nonzero_volume, true);
    }
}

void FixtureRepositoryTests::exposesTypedPreferencesContract() { verifyTypedPreferencesContract<FixtureBundle>(); }

void FixtureRepositoryTests::loadsFrozenFixtureAndQueueReadOnly() {
    const QString fixture_path = QString::fromUtf8(M0_TEST_FIXTURE_FILE);
    const QString queue_path = QString::fromUtf8(M0_TEST_QUEUE_FILE);
    const QByteArray fixture_before = QCryptographicHash::hash(readBytes(fixture_path), QCryptographicHash::Sha256);
    const QByteArray queue_before = QCryptographicHash::hash(readBytes(queue_path), QCryptographicHash::Sha256);

    const FixtureBundle bundle = FixtureRepository{}.load(fixture_path, queue_path);

    QCOMPARE(bundle.schema_id, QStringLiteral("m0.shell-fixture.v1"));
    QCOMPARE(bundle.schema_version, 1);
    QCOMPARE(bundle.fixture_id, QStringLiteral("M0-SHELL-F1"));
    QCOMPARE(bundle.locale, QStringLiteral("pt-BR"));
    QCOMPARE(bundle.default_state_id, QStringLiteral("empty"));
    QCOMPARE(bundle.queue_schema_id, QStringLiteral("m0.shell-queue.v1"));
    QCOMPARE(bundle.queue_id, QStringLiteral("M0-SHELL-Q1000"));
    QCOMPARE(bundle.states.size(), 3);
    QCOMPARE(bundle.controls.size(), 11);
    QCOMPARE(bundle.queue.size(), 1000);
    QCOMPARE(bundle.preferences.defaults.theme, QStringLiteral("system"));
    QCOMPARE(bundle.preferences.defaults.motion, QStringLiteral("system"));
    QCOMPARE(bundle.preferences.defaults.volume_percent, 80);
    QCOMPARE(bundle.preferences.defaults.muted, false);
    QCOMPARE(bundle.preferences.defaults.skip_backward_seconds, 5);
    QCOMPARE(bundle.preferences.defaults.skip_forward_seconds, 5);
    QCOMPARE(bundle.preferences.defaults.close_behavior, QStringLiteral("minimize_to_tray"));
    QCOMPARE(bundle.preferences.defaults.restore_last_session, true);
    QCOMPARE(bundle.preferences.defaults.start_minimized, false);
    QCOMPARE(bundle.preferences.defaults.queue_end_behavior, QStringLiteral("stop_keep_selection"));
    QCOMPARE(QCryptographicHash::hash(readBytes(fixture_path), QCryptographicHash::Sha256), fixture_before);
    QCOMPARE(QCryptographicHash::hash(readBytes(queue_path), QCryptographicHash::Sha256), queue_before);
}

void FixtureRepositoryTests::preservesHostileFixedCases() {
    const FixtureBundle bundle =
        FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
    QCOMPARE(bundle.queue.size(), 1000);
    const QueueItem& duplicate_a = itemById(bundle, QStringLiteral("queue-0007"));
    const QueueItem& duplicate_b = itemById(bundle, QStringLiteral("queue-0707"));
    QCOMPARE_NE(duplicate_a.occurrence_id, duplicate_b.occurrence_id);
    QCOMPARE(duplicate_a.semantic_key, duplicate_b.semantic_key);
    QCOMPARE(duplicate_a.title, duplicate_b.title);

    const QueueItem& nfc = itemById(bundle, QStringLiteral("queue-0017"));
    const QueueItem& nfd = itemById(bundle, QStringLiteral("queue-0018"));
    QCOMPARE(nfc.presentation_form, QStringLiteral("nfc"));
    QCOMPARE(nfd.presentation_form, QStringLiteral("nfd"));
    QCOMPARE_NE(nfc.title, nfd.title);
    QCOMPARE(nfc.title.normalized(QString::NormalizationForm_C), nfd.title.normalized(QString::NormalizationForm_C));

    const QueueItem& non_bmp = itemById(bundle, QStringLiteral("queue-0023"));
    QVERIFY(non_bmp.title.contains(QString::fromUcs4(U"🎧")));
    QCOMPARE(itemById(bundle, QStringLiteral("queue-0042")).title,
             QStringLiteral("<b>Não interpretar</b> & \"aspas\" 'simples'"));
    QCOMPARE(itemById(bundle, QStringLiteral("queue-0512")).title.toUcs4().size(), 512);

    const QueueItem& unavailable = itemById(bundle, QStringLiteral("queue-0999"));
    QCOMPARE(unavailable.available, false);
    QVERIFY(!unavailable.duration_ms.has_value());
    QVERIFY(unavailable.error_code.has_value());
    QCOMPARE(*unavailable.error_code, QStringLiteral("fixture_source_unavailable"));
}

void FixtureRepositoryTests::queueModelExposesTypedRolesInSourceOrder() {
    const FixtureBundle bundle =
        FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
    const QueueListModel model(bundle.queue);

    QCOMPARE(model.rowCount(), 1000);
    QCOMPARE(model.roleNames().value(QueueListModel::OccurrenceIdRole), QByteArray("occurrenceId"));
    QCOMPARE(model.roleNames().value(QueueListModel::AvailableRole), QByteArray("available"));
    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, QueueListModel::OrderIndexRole).metaType().id(), QMetaType::Int);
    QCOMPARE(model.data(first, QueueListModel::OccurrenceIdRole).metaType().id(), QMetaType::QString);
    QCOMPARE(model.data(first, QueueListModel::AvailableRole).metaType().id(), QMetaType::Bool);
    QCOMPARE(model.data(first, QueueListModel::OrderIndexRole).toInt(), 0);
    QCOMPARE(model.data(first, QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0001"));
    QCOMPARE(model.data(model.index(16, 0), QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0017"));
    QCOMPARE(model.data(model.index(999, 0), QueueListModel::OccurrenceIdRole).toString(),
             QStringLiteral("queue-1000"));
    const QModelIndex unavailable = model.index(998, 0);
    QCOMPARE(model.data(unavailable, QueueListModel::AvailableRole).toBool(), false);
    QVERIFY(!model.data(unavailable, QueueListModel::DurationMsRole).isValid());
}

void FixtureRepositoryTests::rejectsSemanticFixtureDrift_data() {
    QTest::addColumn<QString>("mutation");
    QTest::addColumn<QString>("expected_message");

    QTest::newRow("typed-window-field") << QStringLiteral("window-type")
                                        << QStringLiteral("fixture window.client_width_px must be an integer");
    QTest::newRow("nullable-state-field") << QStringLiteral("state-nullability")
                                          << QStringLiteral("fixture empty state current_occurrence_id must be null");
    QTest::newRow("nested-exact-keys") << QStringLiteral("window-key")
                                       << QStringLiteral("fixture window keys differ from the frozen schema");
    QTest::newRow("canonical-color") << QStringLiteral("palette-color")
                                     << QStringLiteral("fixture light palette contains a non-canonical color");
    QTest::newRow("ordered-theme-modes") << QStringLiteral("theme-mode")
                                         << QStringLiteral("fixture theme modes drifted");
    QTest::newRow("deterministic-timing")
        << QStringLiteral("fake-timing") << QStringLiteral("fixture fake-backend contract drifted");
    QTest::newRow("preference-constraint-keys")
        << QStringLiteral("preference-constraint-key")
        << QStringLiteral("preference constraints keys differ from the frozen schema");
    QTest::newRow("preference-constraint-range")
        << QStringLiteral("preference-range") << QStringLiteral("preference constraint ranges drifted");
    QTest::newRow("preference-invalid-range") << QStringLiteral("preference-invalid-range")
                                              << QStringLiteral("preference numeric constraint ranges are invalid");
    QTest::newRow("preference-ordered-option")
        << QStringLiteral("preference-option") << QStringLiteral("preference constraint options drifted");
    QTest::newRow("preference-default-range") << QStringLiteral("preference-default-range")
                                              << QStringLiteral("preference defaults fall outside numeric constraints");
    QTest::newRow("preference-default-option")
        << QStringLiteral("preference-default-option")
        << QStringLiteral("preference defaults are not allowed by option constraints");
    QTest::newRow("preference-rule") << QStringLiteral("preference-rule") << QStringLiteral("preference rules drifted");
    QTest::newRow("preference-rule-type")
        << QStringLiteral("preference-rule-type")
        << QStringLiteral("preference rules.start_minimized_requires_tray must be a bool");
    QTest::newRow("state-to-queue-relation")
        << QStringLiteral("state-queue-relation")
        << QStringLiteral("fixture state current occurrence does not resolve in queue");
    QTest::newRow("state-to-asset-relation")
        << QStringLiteral("state-asset-relation") << QStringLiteral("fixture state asset does not resolve");
    QTest::newRow("track-to-playing-relation")
        << QStringLiteral("track-playing-relation")
        << QStringLiteral("track details do not describe the playing occurrence");
}

void FixtureRepositoryTests::rejectsSemanticFixtureDrift() {
    QFETCH(QString, mutation);
    QFETCH(QString, expected_message);

    QJsonObject fixture = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE))).object();
    const QJsonObject queue = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE))).object();
    QVERIFY(!fixture.isEmpty());
    QVERIFY(!queue.isEmpty());

    if (mutation == QStringLiteral("window-type") || mutation == QStringLiteral("window-key")) {
        QJsonObject window = fixture.value(QStringLiteral("window")).toObject();
        if (mutation == QStringLiteral("window-type")) {
            window.insert(QStringLiteral("client_width_px"), QStringLiteral("960"));
        } else {
            window.insert(QStringLiteral("unexpected"), true);
        }
        fixture.insert(QStringLiteral("window"), window);
    } else if (mutation == QStringLiteral("state-nullability") || mutation == QStringLiteral("state-queue-relation") ||
               mutation == QStringLiteral("state-asset-relation")) {
        QJsonArray states = fixture.value(QStringLiteral("states")).toArray();
        const qsizetype state_index = mutation == QStringLiteral("state-queue-relation") ? 1 : 0;
        QJsonObject state = states.at(state_index).toObject();
        if (mutation == QStringLiteral("state-nullability")) {
            state.insert(QStringLiteral("current_occurrence_id"), QStringLiteral(""));
        } else if (mutation == QStringLiteral("state-queue-relation")) {
            state.insert(QStringLiteral("current_occurrence_id"), QStringLiteral("queue-1001"));
        } else {
            state.insert(QStringLiteral("asset_id"), QStringLiteral("cover-missing"));
        }
        states.replace(state_index, state);
        fixture.insert(QStringLiteral("states"), states);
    } else if (mutation == QStringLiteral("palette-color")) {
        QJsonObject themes = fixture.value(QStringLiteral("themes")).toObject();
        QJsonObject palettes = themes.value(QStringLiteral("palettes")).toObject();
        QJsonObject light = palettes.value(QStringLiteral("light")).toObject();
        light.insert(QStringLiteral("accent"), QStringLiteral("#4f46e5"));
        palettes.insert(QStringLiteral("light"), light);
        themes.insert(QStringLiteral("palettes"), palettes);
        fixture.insert(QStringLiteral("themes"), themes);
    } else if (mutation == QStringLiteral("theme-mode")) {
        QJsonObject themes = fixture.value(QStringLiteral("themes")).toObject();
        themes.insert(QStringLiteral("available_modes"),
                      QJsonArray{QStringLiteral("light"), QStringLiteral("system"), QStringLiteral("dark")});
        fixture.insert(QStringLiteral("themes"), themes);
    } else if (mutation == QStringLiteral("fake-timing")) {
        QJsonObject fake_backend = fixture.value(QStringLiteral("fake_backend")).toObject();
        QJsonObject snapshots = fake_backend.value(QStringLiteral("snapshots")).toObject();
        snapshots.insert(QStringLiteral("frequency_hz"), 4);
        fake_backend.insert(QStringLiteral("snapshots"), snapshots);
        fixture.insert(QStringLiteral("fake_backend"), fake_backend);
    } else if (mutation.startsWith(QStringLiteral("preference-"))) {
        QJsonObject preferences = fixture.value(QStringLiteral("preferences")).toObject();
        if (mutation == QStringLiteral("preference-constraint-key")) {
            QJsonObject constraints = preferences.value(QStringLiteral("constraints")).toObject();
            constraints.insert(QStringLiteral("unexpected"), true);
            preferences.insert(QStringLiteral("constraints"), constraints);
        } else if (mutation == QStringLiteral("preference-range")) {
            QJsonObject constraints = preferences.value(QStringLiteral("constraints")).toObject();
            QJsonObject range = constraints.value(QStringLiteral("volume_percent")).toObject();
            range.insert(QStringLiteral("maximum"), 101);
            constraints.insert(QStringLiteral("volume_percent"), range);
            preferences.insert(QStringLiteral("constraints"), constraints);
        } else if (mutation == QStringLiteral("preference-invalid-range")) {
            QJsonObject constraints = preferences.value(QStringLiteral("constraints")).toObject();
            QJsonObject range = constraints.value(QStringLiteral("skip_seconds")).toObject();
            range.insert(QStringLiteral("minimum"), 301);
            constraints.insert(QStringLiteral("skip_seconds"), range);
            preferences.insert(QStringLiteral("constraints"), constraints);
        } else if (mutation == QStringLiteral("preference-option")) {
            QJsonObject constraints = preferences.value(QStringLiteral("constraints")).toObject();
            constraints.insert(QStringLiteral("close_behavior"),
                               QJsonArray{QStringLiteral("exit"), QStringLiteral("minimize_to_tray")});
            preferences.insert(QStringLiteral("constraints"), constraints);
        } else if (mutation == QStringLiteral("preference-default-range") ||
                   mutation == QStringLiteral("preference-default-option")) {
            QJsonObject defaults = preferences.value(QStringLiteral("defaults")).toObject();
            if (mutation == QStringLiteral("preference-default-range")) {
                defaults.insert(QStringLiteral("volume_percent"), 101);
            } else {
                defaults.insert(QStringLiteral("close_behavior"), QStringLiteral("close_window"));
            }
            preferences.insert(QStringLiteral("defaults"), defaults);
        } else if (mutation == QStringLiteral("preference-rule")) {
            QJsonObject rules = preferences.value(QStringLiteral("rules")).toObject();
            rules.insert(QStringLiteral("restored_playback_state"), QStringLiteral("playing"));
            preferences.insert(QStringLiteral("rules"), rules);
        } else if (mutation == QStringLiteral("preference-rule-type")) {
            QJsonObject rules = preferences.value(QStringLiteral("rules")).toObject();
            rules.insert(QStringLiteral("start_minimized_requires_tray"), QStringLiteral("true"));
            preferences.insert(QStringLiteral("rules"), rules);
        } else {
            QFAIL("unknown preference mutation");
        }
        fixture.insert(QStringLiteral("preferences"), preferences);
    } else if (mutation == QStringLiteral("track-playing-relation")) {
        QJsonObject track_details = fixture.value(QStringLiteral("track_details")).toObject();
        track_details.insert(QStringLiteral("occurrence_id"), QStringLiteral("queue-1000"));
        fixture.insert(QStringLiteral("track_details"), track_details);
    } else {
        QFAIL("unknown semantic mutation");
    }

    expectSemanticDecodeFailure(fixture, queue, expected_message);
}

void FixtureRepositoryTests::rejectsQueueCountDrift() {
    const QJsonObject fixture = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE))).object();
    QJsonObject queue = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE))).object();
    queue.insert(QStringLiteral("item_count"), 999);

    expectSemanticDecodeFailure(fixture, queue, QStringLiteral("queue identity/count drifted"));
}

void FixtureRepositoryTests::rejectsHostileCaseDrift() {
    const QJsonObject fixture = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE))).object();
    QJsonObject queue = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE))).object();
    QJsonArray items = queue.value(QStringLiteral("items")).toArray();
    QJsonObject item = items.at(41).toObject();
    QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    metadata.insert(QStringLiteral("title"), QStringLiteral("interpreted"));
    item.insert(QStringLiteral("metadata"), metadata);
    items.replace(41, item);
    queue.insert(QStringLiteral("items"), items);

    expectSemanticDecodeFailure(fixture, queue, QStringLiteral("literal-markup hostile case drifted"));
}

void FixtureRepositoryTests::rejectsByteDriftBeforeSemanticDecode_data() {
    QTest::addColumn<QString>("document");

    QTest::newRow("fixture-bytes") << QStringLiteral("fixture");
    QTest::newRow("queue-bytes") << QStringLiteral("queue");
}

void FixtureRepositoryTests::rejectsByteDriftBeforeSemanticDecode() {
    QFETCH(QString, document);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const bool mutate_fixture = document == QStringLiteral("fixture");
    const QString source_path = QString::fromUtf8(mutate_fixture ? M0_TEST_FIXTURE_FILE : M0_TEST_QUEUE_FILE);
    QJsonObject mutated_object = QJsonDocument::fromJson(readBytes(source_path)).object();
    if (mutate_fixture) {
        QJsonObject window = mutated_object.value(QStringLiteral("window")).toObject();
        window.insert(QStringLiteral("client_width_px"), 961);
        mutated_object.insert(QStringLiteral("window"), window);
    } else {
        mutated_object.insert(QStringLiteral("item_count"), 999);
    }

    QFile mutated(
        temporary.filePath(mutate_fixture ? QStringLiteral("fixture.v1.json") : QStringLiteral("queue.v1.json")));
    QVERIFY(mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(mutated.write(QJsonDocument(mutated_object).toJson(QJsonDocument::Compact)) > 0);
    mutated.close();

    try {
        const QString fixture_path = mutate_fixture ? mutated.fileName() : QString::fromUtf8(M0_TEST_FIXTURE_FILE);
        const QString queue_path = mutate_fixture ? QString::fromUtf8(M0_TEST_QUEUE_FILE) : mutated.fileName();
        static_cast<void>(FixtureRepository{}.load(fixture_path, queue_path));
        QFAIL("byte drift unexpectedly crossed the hash boundary");
    } catch (const FixtureValidationError& error) {
        QCOMPARE(QString::fromStdString(error.what()),
                 document + QStringLiteral(" bytes differ from the protocol-anchored fixture manifest"));
    }
}

void FixtureRepositoryTests::rejectsIntegerNarrowingAliases() {
    QJsonObject fixture = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE))).object();
    const QJsonObject canonical_queue =
        QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE))).object();
    fixture.insert(QStringLiteral("schema_version"), 4'294'967'297.0);
    expectSemanticDecodeFailure(fixture, canonical_queue,
                                QStringLiteral("fixture.schema_version is outside int range"));

    const QJsonObject canonical_fixture =
        QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE))).object();
    QJsonObject queue = canonical_queue;
    QJsonArray items = queue.value(QStringLiteral("items")).toArray();
    QJsonObject first = items.at(0).toObject();
    first.insert(QStringLiteral("order_index"), 4'294'967'296.0);
    items.replace(0, first);
    queue.insert(QStringLiteral("items"), items);

    expectSemanticDecodeFailure(canonical_fixture, queue,
                                QStringLiteral("queue item.order_index is outside int range"));
}

void FixtureRepositoryTests::rejectsIntegerOutsideSigned64BitRange() {
    const QJsonObject fixture = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE))).object();
    QJsonObject queue = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE))).object();
    QJsonArray items = queue.value(QStringLiteral("items")).toArray();
    QJsonObject first = items.at(0).toObject();
    first.insert(QStringLiteral("order_index"), 9'223'372'036'854'775'808.0);
    items.replace(0, first);
    queue.insert(QStringLiteral("items"), items);

    expectSemanticDecodeFailure(fixture, queue,
                                QStringLiteral("queue item.order_index is outside the exact JSON integer range"));
}

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::FixtureRepositoryTests)

#include "FixtureRepositoryTests.moc"
