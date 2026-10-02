include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core)
add_library(rc_music_download_persistence STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadCheckpointJson.cpp")
target_include_directories(rc_music_download_persistence PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_download_persistence PUBLIC rc_music_downloads Qt6::Core)
rc_music_configure_cpp(rc_music_download_persistence)

if(WIN32)
    target_sources(rc_music_download_persistence PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadCheckpointStore.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadArtifactPins.cpp")
    target_link_libraries(rc_music_download_persistence PRIVATE advapi32)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)
    set(rc_music_checkpoint_units DownloadCheckpointJson)
    if(WIN32)
        list(APPEND rc_music_checkpoint_units WindowsDownloadCheckpointStore)
    endif()
    foreach(unit IN LISTS rc_music_checkpoint_units)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_link_libraries(${target} PRIVATE rc_music_download_persistence Qt6::Test)
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        add_test(NAME "rc_music.download-checkpoint.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.download-checkpoint.${unit}" PROPERTIES
            LABELS "downloads;persistence;qt;FR-051;FR-053" TIMEOUT 30
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endforeach()
endif()
