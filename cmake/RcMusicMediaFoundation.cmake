include_guard(GLOBAL)

# This component belongs only to the opt-in Windows download composition.
# The native worker never loads Qt or a separately distributed converter CLI.
add_library(rc_music_media_foundation_protocol STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/MediaFoundationWorkerProtocol.cpp")
target_include_directories(rc_music_media_foundation_protocol PUBLIC
    "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
rc_music_configure_cpp(rc_music_media_foundation_protocol)

# MF entry points are resolved explicitly from System32 by the codec. Missing
# Windows media components produce a typed result instead of loader failure.
set(rc_music_media_foundation_libraries mfuuid wmcodecdspuuid ole32)
add_library(rc_music_media_foundation_codec STATIC EXCLUDE_FROM_ALL
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationCodec.cpp")
target_link_libraries(rc_music_media_foundation_codec PUBLIC rc_music_media_foundation_protocol
    PRIVATE ${rc_music_media_foundation_libraries})
rc_music_configure_cpp(rc_music_media_foundation_codec)

add_executable(rc_music_media_foundation_worker
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/MediaFoundationWorkerProtocol.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationCodec.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationWorkerMain.cpp")
target_include_directories(rc_music_media_foundation_worker PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
target_link_libraries(rc_music_media_foundation_worker PRIVATE ${rc_music_media_foundation_libraries})
set_target_properties(rc_music_media_foundation_worker PROPERTIES
    OUTPUT_NAME "RCMusicMediaWorker" MSVC_RUNTIME_LIBRARY "MultiThreaded")
rc_music_configure_cpp(rc_music_media_foundation_worker)
target_link_libraries(rc_music_download_network PRIVATE rc_music_media_foundation_protocol)
target_sources(rc_music_download_network PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationLaunch.cpp")

if(BUILD_TESTING)
    foreach(unit IN ITEMS MediaFoundationWorkerProtocol WindowsMediaFoundationCodec WindowsMediaFoundationConversion
                          WindowsMediaFoundationLaunch)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_link_libraries(${target} PRIVATE rc_music_download_network Qt6::Test)
        if(unit STREQUAL "WindowsMediaFoundationCodec")
            target_link_libraries(${target} PRIVATE rc_music_media_foundation_codec)
        endif()
        target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        target_compile_definitions(${target} PRIVATE
            RC_MUSIC_TEST_AUDIO_FIXTURES="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid"
            RC_MUSIC_TEST_MF_WORKER_PATH="$<TARGET_FILE:rc_music_media_foundation_worker>")
        add_dependencies(${target} rc_music_media_foundation_worker)
        add_test(NAME "rc_music.download-network.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.download-network.${unit}" PROPERTIES
            LABELS "download-network;qt;media-foundation;FR-051;FR-053" TIMEOUT 60
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endforeach()
endif()
