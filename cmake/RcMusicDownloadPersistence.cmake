include_guard(GLOBAL)

if(WIN32)
    add_library(rc_music_download_persistence STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadCheckpointStore.cpp")
    target_link_libraries(rc_music_download_persistence PUBLIC rc_music_download_documents)
    rc_music_configure_cpp(rc_music_download_persistence)
else()
    add_library(rc_music_download_persistence INTERFACE)
    target_link_libraries(rc_music_download_persistence INTERFACE rc_music_download_documents)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)
    set(rc_music_checkpoint_units)
    if(WIN32)
        list(APPEND rc_music_checkpoint_units WindowsDownloadCheckpointStore WindowsDownloadStorage)
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
