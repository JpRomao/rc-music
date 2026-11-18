// Compile-time contract of the libdatachannel package behind rc_music_listening_room.
// Only this library sees libdatachannel; the room transport (S6.3) adds its sources here.
#include <rtc/rtc.hpp>
#include <rtc/version.h>

static_assert(RTC_VERSION_MAJOR == 0 && RTC_VERSION_MINOR == 24 && RTC_VERSION_PATCH == 6,
              "rc_music_listening_room is pinned to libdatachannel 0.24.6 (ADR-0022)");
static_assert(RTC_ENABLE_WEBSOCKET == 1, "Loopback signaling needs rtc::WebSocketServer");
static_assert(RTC_ENABLE_MEDIA == 0, "The listening room uses data channels only");
