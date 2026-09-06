#pragma once

#include "m0/domain/WindowPlacement.hpp"

#include <cstdint>
#include <optional>

namespace music_player::m0 {

enum class PlaybackStatus {
    Empty,
    Loading,
    Ready,
    Paused,
    Playing,
    Error,
};

enum class PlaybackErrorCode {
    None,
    SourceUnavailable,
    InvalidMedia,
    AccessDenied,
    NetworkFailure,
    BackendFailure,
    AudioOutputUnavailable,
    AudioOutputChanged,
};

enum class PlaybackCommandResult {
    Accepted,
    InvalidArgument,
    NotReady,
    NotSeekable,
    Busy,
    Shutdown,
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
    std::uint64_t generation{0};
    PlaybackErrorCode error{PlaybackErrorCode::None};

    [[nodiscard]] bool operator==(const PlaybackSnapshot&) const noexcept = default;
};

enum class PlaybackEventType {
    SnapshotChanged,
    Ended,
};

struct PlaybackEvent final {
    PlaybackEventType type{PlaybackEventType::SnapshotChanged};
    PlaybackSnapshot snapshot{};
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
    std::optional<WindowPlacement> window_placement{std::nullopt};
    bool repeat_queue_enabled{false};
    bool shuffle_queue_enabled{false};

    [[nodiscard]] bool operator==(const Preferences&) const noexcept = default;
};

} // namespace music_player::m0
