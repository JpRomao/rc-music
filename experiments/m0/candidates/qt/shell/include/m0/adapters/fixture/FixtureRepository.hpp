#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace music_player::m0 {

struct FixtureState final {
    QString state_id;
    QString playback_state;
    bool queue_loaded{false};
    QString current_occurrence_id;
    std::optional<std::int64_t> position_ms;
    std::optional<std::int64_t> duration_ms;
    bool seekable{false};
    QString error_code;
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
    QString error_code;
};

struct FixtureBundle final {
    QString schema_id;
    int schema_version{0};
    QString fixture_id;
    QString locale;
    QString default_state_id;
    QList<FixtureState> states;
    QList<FixtureControl> controls;
    FixtureDefaults defaults;
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
