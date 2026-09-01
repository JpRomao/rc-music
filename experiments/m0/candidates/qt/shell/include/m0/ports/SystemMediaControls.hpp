#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace music_player::m0 {

enum class SystemMediaPlaybackStatus {
    Closed,
    Stopped,
    Paused,
    Playing,
    Changing,
};

struct SystemMediaSnapshot final {
    std::u16string title;
    std::u16string artist;
    std::u16string album;
    SystemMediaPlaybackStatus status{SystemMediaPlaybackStatus::Closed};
    bool play_enabled{false};
    bool pause_enabled{false};
    bool previous_enabled{false};
    bool next_enabled{false};
    bool seek_enabled{false};
    std::int64_t position_ms{0};
    std::int64_t duration_ms{0};

    [[nodiscard]] bool operator==(const SystemMediaSnapshot&) const noexcept = default;
};

enum class SystemMediaCommandKind {
    Play,
    Pause,
    Previous,
    Next,
    Seek,
};

struct SystemMediaCommand final {
    SystemMediaCommandKind kind{SystemMediaCommandKind::Play};
    std::int64_t position_ms{0};

    [[nodiscard]] bool operator==(const SystemMediaCommand&) const noexcept = default;
};

using SystemMediaCommandHandler = std::function<void(SystemMediaCommand)>;

class SystemMediaControls {
  public:
    virtual ~SystemMediaControls() = default;

    [[nodiscard]] virtual bool available() const noexcept = 0;
    // Adapters must invoke the handler on the application/UI thread that installed it.
    virtual void setCommandHandler(SystemMediaCommandHandler handler) = 0;
    [[nodiscard]] virtual bool publish(const SystemMediaSnapshot& snapshot) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

} // namespace music_player::m0
