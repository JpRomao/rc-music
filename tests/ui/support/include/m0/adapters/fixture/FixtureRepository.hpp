#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace music_player::m0 {

struct FixtureWindow final {
    int client_width_px{0};
    int client_height_px{0};
    int minimum_width_px{0};
    int minimum_height_px{0};
};

struct FixtureLayoutTokens final {
    int queue_panel_min_width_px{0};
    int queue_panel_max_width_px{0};
    int control_target_min_px{0};
    int control_target_preferred_px{0};
    int radius_small_px{0};
    int radius_medium_px{0};
    int spacing_unit_px{0};
};

struct FixtureStrings final {
    QString app_name;
    QString action_open_audio;
    QString empty_title;
    QString empty_hint;
    QString action_play;
    QString action_pause;
    QString action_previous;
    QString action_next;
    QString skip_backward_short;
    QString skip_forward_short;
    QString skip_backward_name;
    QString skip_forward_name;
    QString progress_name;
    QString volume_name;
    QString action_mute;
    QString action_unmute;
    QString queue_title;
    QString preferences_title;
    QString details_title;
    QString section_general;
    QString section_playback;
    QString section_appearance;
    QString theme_system;
    QString theme_light;
    QString theme_dark;
    QString motion_system;
    QString motion_reduced;
    QString error_title;
    QString error_body;
    QString action_skip;
    QString action_retry;
    QString not_informed;
};

struct FixturePalette final {
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
};

struct FixtureThemes final {
    QStringList available_modes;
    FixturePalette light;
    FixturePalette dark;
};

struct FixtureMotionValues final {
    int panel_transition_ms{0};
    int feedback_transition_ms{0};
    bool continuous_animation{false};
};

struct FixtureMotion final {
    QStringList available_modes;
    FixtureMotionValues normal;
    FixtureMotionValues reduced;
};

struct FixtureFakeClock final {
    QString kind;
    std::int64_t initial_monotonic_ms{0};
};

struct FixtureFakeSnapshots final {
    int frequency_hz{0};
    std::int64_t interval_ms{0};
    std::int64_t start_position_ms{0};
    std::int64_t duration_ms{0};
    std::int64_t increment_ms{0};
    QString end_behavior;
};

struct FixtureFakeSeek final {
    std::int64_t target_ms{0};
    bool preview_immediate{false};
    int ack_after_ticks{0};
    std::int64_t ack_position_ms{0};
};

struct FixtureFakeBackend final {
    FixtureFakeClock clock;
    FixtureFakeSnapshots snapshots;
    FixtureFakeSeek seek;
};

struct FixtureState final {
    QString state_id;
    QString playback_state;
    bool queue_loaded{false};
    std::optional<QString> current_occurrence_id;
    std::optional<QString> context_occurrence_id;
    std::optional<std::int64_t> position_ms;
    std::optional<std::int64_t> duration_ms;
    bool seekable{false};
    QString asset_id;
    std::optional<QString> message_key;
    QString primary_action_id;
    std::optional<QString> error_code;
};

struct FixtureControl final {
    QString control_id;
    QString role;
    QString name_key;
};

struct FixtureDefaults final {
    QString theme;
    QString motion;
    int volume_percent{0};
    bool muted{false};
    int skip_backward_seconds{0};
    int skip_forward_seconds{0};
    QString close_behavior;
    bool restore_last_session{false};
    bool start_minimized{false};
    QString queue_end_behavior;
};

struct FixturePreferenceRange final {
    int minimum{0};
    int maximum{0};
};

struct FixturePreferenceConstraints final {
    FixturePreferenceRange volume_percent;
    FixturePreferenceRange skip_seconds;
    QStringList close_behavior;
    QStringList queue_end_behavior;
};

struct FixturePreferenceRules final {
    QString close_without_tray;
    bool start_minimized_requires_tray{false};
    QString restored_playback_state;
    bool unmute_restores_last_nonzero_volume{false};
};

struct FixturePreferences final {
    FixtureDefaults defaults;
    FixturePreferenceConstraints constraints;
    FixturePreferenceRules rules;
};

struct FixtureTrackDetails final {
    QString occurrence_id;
    bool synthetic{false};
    std::int64_t size_bytes{0};
    QString modified_utc;
    QString container;
    QString codec;
    int bitrate_bps{0};
    int sample_rate_hz{0};
    int channels{0};
    std::optional<int> bit_depth;
};

struct FixtureAsset final {
    QString asset_id;
    QString relative_path;
    QString alt_key;
};

struct QueueItem final {
    int order_index{0};
    QString occurrence_id;
    QString semantic_key;
    QString presentation_form;
    QString file_name;
    QString title;
    QString artist;
    QString album;
    std::optional<std::int64_t> duration_ms;
    bool available{false};
    std::optional<QString> error_code;
};

struct FixtureQueueContract final {
    QString relative_path;
    QString queue_id;
    int expected_item_count{0};
    QString load_action_id;
};

struct FixtureBundle final {
    QString schema_id;
    int schema_version{0};
    QString fixture_id;
    QString locale;
    QString default_state_id;
    FixtureWindow window;
    FixtureLayoutTokens layout_tokens;
    FixtureStrings strings;
    QList<FixtureControl> controls;
    FixtureThemes themes;
    FixtureMotion motion;
    FixturePreferences preferences;
    FixtureFakeBackend fake_backend;
    QList<FixtureState> states;
    FixtureQueueContract queue_contract;
    FixtureTrackDetails track_details;
    QList<FixtureAsset> assets;
    QString queue_schema_id;
    QString queue_id;
    QList<QueueItem> queue;
};

class FixtureValidationError final : public std::runtime_error {
  public:
    explicit FixtureValidationError(const QString& message);
};

class FixtureRepository final {
  public:
    [[nodiscard]] FixtureBundle load(const QString& fixture_path, const QString& queue_path) const;
};

} // namespace music_player::m0
