include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core)
add_library(rc_music_media_data STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/StrictJson.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/StrictJson.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DirectMediaPolicy.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DirectMediaPolicy.cpp")
target_include_directories(rc_music_media_data PUBLIC
    "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/include")
target_link_libraries(rc_music_media_data PUBLIC Qt6::Core)
rc_music_configure_cpp(rc_music_media_data)
