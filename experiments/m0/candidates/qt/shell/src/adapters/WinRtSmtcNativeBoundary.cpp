#include "adapters/WindowsSmtcAdapter.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <systemmediatransportcontrolsinterop.h>
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/base.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace music_player::m0 {
namespace {

namespace media = winrt::Windows::Media;

constexpr std::int64_t ticks_per_millisecond{10'000};

[[nodiscard]] winrt::hstring asHString(const std::u16string& value) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return winrt::hstring{
        std::wstring_view{reinterpret_cast<const wchar_t*>(value.data()), static_cast<std::size_t>(value.size())}};
}

[[nodiscard]] winrt::Windows::Foundation::TimeSpan asTimeSpan(std::int64_t milliseconds) noexcept {
    constexpr auto max_milliseconds = std::numeric_limits<std::int64_t>::max() / ticks_per_millisecond;
    return winrt::Windows::Foundation::TimeSpan{std::clamp(milliseconds, std::int64_t{0}, max_milliseconds) *
                                                ticks_per_millisecond};
}

[[nodiscard]] media::MediaPlaybackStatus asNativeStatus(SystemMediaPlaybackStatus status) noexcept {
    switch (status) {
    case SystemMediaPlaybackStatus::Closed:
        return media::MediaPlaybackStatus::Closed;
    case SystemMediaPlaybackStatus::Stopped:
        return media::MediaPlaybackStatus::Stopped;
    case SystemMediaPlaybackStatus::Paused:
        return media::MediaPlaybackStatus::Paused;
    case SystemMediaPlaybackStatus::Playing:
        return media::MediaPlaybackStatus::Playing;
    case SystemMediaPlaybackStatus::Changing:
        return media::MediaPlaybackStatus::Changing;
    }
    return media::MediaPlaybackStatus::Closed;
}

struct NativeEventState final {
    std::mutex mutex;
    SystemMediaCommandHandler callback;
    bool seek_enabled{false};
};

void emitNativeCommand(const std::shared_ptr<NativeEventState>& state, SystemMediaCommand command) noexcept {
    SystemMediaCommandHandler callback;
    try {
        const std::scoped_lock lock(state->mutex);
        if (command.kind == SystemMediaCommandKind::Seek && !state->seek_enabled)
            return;
        callback = state->callback;
    } catch (...) {
        return;
    }
    if (!callback)
        return;
    try {
        callback(command);
    } catch (...) {
        // Native events are an external boundary and must never unwind into WinRT.
    }
}

class WinRtSmtcNativeBoundary final : public SmtcNativeBoundary {
  public:
    ~WinRtSmtcNativeBoundary() override { shutdown(); }

    [[nodiscard]] bool bind(std::uintptr_t native_window_handle, CommandCallback callback) noexcept override {
        shutdown();
        if (native_window_handle == 0 || !callback)
            return false;
        try {
            try {
                winrt::init_apartment(winrt::apartment_type::single_threaded);
                apartment_initialized_ = true;
            } catch (const winrt::hresult_error& error) {
                if (error.code() != RPC_E_CHANGED_MODE)
                    throw;
            }

            event_state_ = std::make_shared<NativeEventState>();
            event_state_->callback = std::move(callback);
            const auto interop = winrt::get_activation_factory<media::SystemMediaTransportControls,
                                                               ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(native_window_handle),
                                                       winrt::guid_of<media::SystemMediaTransportControls>(),
                                                       winrt::put_abi(controls_)));
            controls_.IsEnabled(false);
            const auto state = event_state_;
            button_token_ = controls_.ButtonPressed(
                [state](const media::SystemMediaTransportControls&,
                        const media::SystemMediaTransportControlsButtonPressedEventArgs& args) noexcept {
                    try {
                        switch (args.Button()) {
                        case media::SystemMediaTransportControlsButton::Play:
                            emitNativeCommand(state, {SystemMediaCommandKind::Play, 0});
                            break;
                        case media::SystemMediaTransportControlsButton::Pause:
                            emitNativeCommand(state, {SystemMediaCommandKind::Pause, 0});
                            break;
                        case media::SystemMediaTransportControlsButton::Previous:
                            emitNativeCommand(state, {SystemMediaCommandKind::Previous, 0});
                            break;
                        case media::SystemMediaTransportControlsButton::Next:
                            emitNativeCommand(state, {SystemMediaCommandKind::Next, 0});
                            break;
                        default:
                            break;
                        }
                    } catch (...) {
                        // Ignore malformed/stale native events after session loss.
                    }
                });
            button_handler_registered_ = true;
            position_token_ = controls_.PlaybackPositionChangeRequested(
                [state](const media::SystemMediaTransportControls&,
                        const media::PlaybackPositionChangeRequestedEventArgs& args) noexcept {
                    try {
                        const auto ticks = args.RequestedPlaybackPosition().count();
                        emitNativeCommand(state, {SystemMediaCommandKind::Seek,
                                                  std::max<std::int64_t>(0, ticks / ticks_per_millisecond)});
                    } catch (...) {
                        // Ignore malformed/stale native events after session loss.
                    }
                });
            position_handler_registered_ = true;
            return true;
        } catch (...) {
            shutdown();
            return false;
        }
    }

    [[nodiscard]] bool publish(const SystemMediaSnapshot& snapshot) noexcept override {
        if (!controls_)
            return false;
        try {
            {
                const std::scoped_lock lock(event_state_->mutex);
                event_state_->seek_enabled = snapshot.seek_enabled;
            }
            controls_.IsPlayEnabled(snapshot.play_enabled);
            controls_.IsPauseEnabled(snapshot.pause_enabled);
            controls_.IsPreviousEnabled(snapshot.previous_enabled);
            controls_.IsNextEnabled(snapshot.next_enabled);
            controls_.IsStopEnabled(false);
            controls_.IsRecordEnabled(false);
            controls_.IsFastForwardEnabled(false);
            controls_.IsRewindEnabled(false);
            controls_.IsChannelUpEnabled(false);
            controls_.IsChannelDownEnabled(false);

            const auto updater = controls_.DisplayUpdater();
            if (snapshot.status == SystemMediaPlaybackStatus::Closed) {
                updater.ClearAll();
            } else {
                updater.Type(media::MediaPlaybackType::Music);
                const auto properties = updater.MusicProperties();
                properties.Title(asHString(snapshot.title));
                properties.Artist(asHString(snapshot.artist));
                properties.AlbumTitle(asHString(snapshot.album));
            }
            updater.Update();

            const auto duration = std::max<std::int64_t>(0, snapshot.duration_ms);
            const auto position = std::clamp(snapshot.position_ms, std::int64_t{0}, duration);
            media::SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(asTimeSpan(0));
            timeline.MinSeekTime(asTimeSpan(0));
            timeline.Position(asTimeSpan(position));
            timeline.MaxSeekTime(asTimeSpan(snapshot.seek_enabled ? duration : 0));
            timeline.EndTime(asTimeSpan(duration));
            controls_.UpdateTimelineProperties(timeline);
            controls_.PlaybackStatus(asNativeStatus(snapshot.status));
            controls_.IsEnabled(snapshot.status != SystemMediaPlaybackStatus::Closed);
            return true;
        } catch (...) {
            shutdown();
            return false;
        }
    }

    void shutdown() noexcept override {
        if (event_state_) {
            const std::scoped_lock lock(event_state_->mutex);
            event_state_->callback = {};
            event_state_->seek_enabled = false;
        }
        if (controls_) {
            try {
                if (button_handler_registered_)
                    controls_.ButtonPressed(button_token_);
            } catch (...) {
                // Revocation is best effort after Explorer or the media session disappears.
            }
            try {
                if (position_handler_registered_)
                    controls_.PlaybackPositionChangeRequested(position_token_);
            } catch (...) {
                // Continue releasing the remaining native state after a failed revocation.
            }
            try {
                controls_.IsEnabled(false);
                const auto updater = controls_.DisplayUpdater();
                updater.ClearAll();
                updater.Update();
            } catch (...) {
                // Cleanup remains best effort after OS/session loss.
            }
        }
        button_handler_registered_ = false;
        position_handler_registered_ = false;
        controls_ = nullptr;
        event_state_.reset();
        if (apartment_initialized_) {
            winrt::uninit_apartment();
            apartment_initialized_ = false;
        }
    }

  private:
    media::SystemMediaTransportControls controls_{nullptr};
    std::shared_ptr<NativeEventState> event_state_;
    winrt::event_token button_token_{};
    winrt::event_token position_token_{};
    bool button_handler_registered_{false};
    bool position_handler_registered_{false};
    bool apartment_initialized_{false};
};

} // namespace

std::unique_ptr<SmtcNativeBoundary> makeWinRtSmtcNativeBoundary() {
    return std::make_unique<WinRtSmtcNativeBoundary>();
}

} // namespace music_player::m0
