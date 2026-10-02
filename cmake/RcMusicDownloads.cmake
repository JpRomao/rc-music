include_guard(GLOBAL)

add_library(rc_music_downloads STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/application/DownloadService.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/DownloadCheckpoint.cpp")
target_link_libraries(rc_music_downloads PUBLIC rc_music_contracts)
rc_music_configure_cpp(rc_music_downloads)
