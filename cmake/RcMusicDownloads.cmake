include_guard(GLOBAL)

add_library(rc_music_downloads STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/application/DownloadService.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/DownloadCheckpoint.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/DownloadAdmissionController.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SingleWorkerDownloadCheckpointIo.cpp")
target_link_libraries(rc_music_downloads PUBLIC rc_music_contracts PRIVATE Threads::Threads)
rc_music_configure_cpp(rc_music_downloads)
