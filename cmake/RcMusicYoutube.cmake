include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core)
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicMediaData.cmake")
add_library(rc_music_youtube_codec STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeAudioCodec.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeAudioCodec.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeLinks.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeLinks.cpp")
target_include_directories(rc_music_youtube_codec PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_youtube_codec PUBLIC rc_music_contracts Qt6::Core PRIVATE rc_music_media_data)
rc_music_configure_cpp(rc_music_youtube_codec)

add_library(rc_music_youtube_resolution STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeAudioResolution.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeBrokerProtocol.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeResolverSession.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeSearch.cpp")
target_include_directories(rc_music_youtube_resolution PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_youtube_resolution PUBLIC rc_music_youtube_codec Qt6::Core PRIVATE rc_music_media_data)
rc_music_configure_cpp(rc_music_youtube_resolution)
