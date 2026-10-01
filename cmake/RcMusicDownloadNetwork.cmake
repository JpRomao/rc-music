include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Network)
add_library(rc_music_download_network STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadNetworkPolicy.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadHttpResponse.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/PinnedDownloadStream.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DirectDownloadSession.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtDownloadIo.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/FfmpegDownloadCodec.cpp"
)
target_link_libraries(rc_music_download_network PUBLIC rc_music_browser_integration Qt6::Network)
target_include_directories(rc_music_download_network PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
set_target_properties(rc_music_download_network PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_download_network)

if(WIN32)
    target_sources(rc_music_download_network PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadWorkspace.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadProcess.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsLocalMediaConverter.cpp")
    target_link_libraries(rc_music_download_network PRIVATE Advapi32)
endif()

if(BUILD_TESTING)
    set(rc_music_download_network_test_units DownloadNetworkPolicy DownloadHttpResponse PinnedDownloadStream
        DirectDownloadSession FfmpegDownloadCodec)
    if(WIN32)
        list(APPEND rc_music_download_network_test_units WindowsDownloadWorkspace WindowsDownloadProcess)
        add_executable(rc_music_download_process_helper
            "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsDownloadProcessHelper.cpp")
        target_link_libraries(rc_music_download_process_helper PRIVATE Qt6::Core)
        rc_music_configure_cpp(rc_music_download_process_helper)
        set(RC_MUSIC_DOWNLOAD_TEST_FFMPEG "" CACHE FILEPATH "Reviewed local FFmpeg for synthetic converter tests only")
        set(RC_MUSIC_DOWNLOAD_TEST_FFPROBE "" CACHE FILEPATH "Reviewed local ffprobe for synthetic converter tests only")
        if(RC_MUSIC_DOWNLOAD_TEST_FFMPEG AND RC_MUSIC_DOWNLOAD_TEST_FFPROBE)
            foreach(tool IN ITEMS RC_MUSIC_DOWNLOAD_TEST_FFMPEG RC_MUSIC_DOWNLOAD_TEST_FFPROBE)
                if(NOT IS_ABSOLUTE "${${tool}}" OR NOT EXISTS "${${tool}}")
                    message(FATAL_ERROR "${tool} requires an existing absolute executable path")
                endif()
            endforeach()
            list(APPEND rc_music_download_network_test_units WindowsLocalMediaConverter)
            add_executable(rc_music_download_converter_helper
                "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsLocalMediaConverterHelper.cpp")
            target_link_libraries(rc_music_download_converter_helper PRIVATE Qt6::Core)
            target_compile_definitions(rc_music_download_converter_helper PRIVATE
                RC_MUSIC_TEST_MP3_FIXTURE="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid/mp3-unicode-tags.mp3")
            rc_music_configure_cpp(rc_music_download_converter_helper)
        endif()
    endif()
    foreach(unit IN LISTS rc_music_download_network_test_units)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_link_libraries(${target} PRIVATE rc_music_download_network Qt6::Test)
        target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        if(unit STREQUAL "WindowsDownloadProcess")
            add_dependencies(${target} rc_music_download_process_helper)
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_DOWNLOAD_PROCESS_HELPER_PATH="$<TARGET_FILE:rc_music_download_process_helper>")
        elseif(unit STREQUAL "WindowsLocalMediaConverter")
            add_dependencies(${target} rc_music_download_converter_helper)
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_TEST_FFMPEG_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFMPEG}"
                RC_MUSIC_TEST_FFPROBE_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFPROBE}"
                RC_MUSIC_TEST_AUDIO_FIXTURES="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid"
                RC_MUSIC_TEST_CONVERTER_HELPER_PATH="$<TARGET_FILE:rc_music_download_converter_helper>")
        endif()
        add_test(NAME "rc_music.download-network.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.download-network.${unit}" PROPERTIES
            LABELS "download-network;qt;FR-051;FR-053" TIMEOUT 30
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endforeach()
endif()
