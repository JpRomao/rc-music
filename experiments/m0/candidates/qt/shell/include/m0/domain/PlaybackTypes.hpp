#pragma once

#include <cstdint>

namespace music_player::m0 {

enum class PlaybackStatus {
    Empty,
    Loading,
    Ready,
    Paused,
    Playing,
    Error,
};

enum class ThemeMode {
    System,
    Light,
    Dark,
};

enum class MotionMode {
    System,
    Reduced,
};

enum class CloseBehavior {
    MinimizeToTray,
    Exit,
};

enum class CloseDisposition {
    HiddenToTray,
    Minimized,
    ExitRequested,
};

enum class QueueEndBehavior {
    StopKeepSelection,
    StopClearSelection,
};

struct PlaybackSnapshot final {
    PlaybackStatus status{PlaybackStatus::Empty};
    std::int64_t position_ms{0};
    std::int64_t duration_ms{0};
    bool seekable{false};
};

struct Preferences final {
    ThemeMode theme{ThemeMode::System};
    MotionMode motion{MotionMode::System};
    double volume{0.8};
    bool muted{false};
    int skip_backward_seconds{5};
    int skip_forward_seconds{5};
    CloseBehavior close_behavior{CloseBehavior::MinimizeToTray};
    bool restore_last_session{true};
    bool start_minimized{false};
    QueueEndBehavior queue_end_behavior{QueueEndBehavior::StopKeepSelection};
};

} // namespace music_player::m0
