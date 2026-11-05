include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Network)
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicYoutube.cmake")
add_library(rc_music_download_network STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadNetworkPolicy.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadHttpResponse.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/PinnedDownloadStream.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DirectDownloadSession.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtDownloadIo.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/FfmpegDownloadCodec.cpp"
)
target_link_libraries(rc_music_download_network PUBLIC rc_music_browser_integration rc_music_download_documents
    rc_music_youtube_resolution Qt6::Network)
target_include_directories(rc_music_download_network PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
set_target_properties(rc_music_download_network PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_download_network)

if(WIN32)
    target_sources(rc_music_download_network PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadWorkerProtocol.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsProcessDownloadIo.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadWorkspace.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadProcess.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsYoutubeResolverProcess.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsYoutubeBrokerProcess.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsYoutubeRuntime.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadTlsRuntime.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadNetworkLaunch.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsLocalMediaConverter.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsLocalMp3Validator.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRecoveredMediaValidator.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadRecoveryValidation.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadExecution.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/YoutubeDownloadExecution.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadJobSession.cpp")
    target_link_libraries(rc_music_download_network PRIVATE Advapi32)
    # Legacy directly-linked executable is an explicit historical/test seam,
    # never the authenticated runtime's entry point or a default OFF artifact.
    add_executable(rc_music_download_network_worker EXCLUDE_FROM_ALL
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadNetworkWorkerMain.cpp")
    target_link_libraries(rc_music_download_network_worker PRIVATE rc_music_download_network)
    rc_music_configure_cpp(rc_music_download_network_worker)
    add_executable(rc_music_download_network_bootstrap
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadNetworkBootstrapMain.cpp")
    set_target_properties(rc_music_download_network_bootstrap PROPERTIES
        OUTPUT_NAME "RCMusicDownloadWorker" MSVC_RUNTIME_LIBRARY "MultiThreaded")
    rc_music_configure_cpp(rc_music_download_network_bootstrap)
    add_library(rc_music_download_network_payload SHARED
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadNetworkPayloadEntry.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadNetworkWorkerMain.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadNetworkPayload.qrc")
    set_target_properties(rc_music_download_network_payload PROPERTIES
        OUTPUT_NAME "rcmusic-download-network" PREFIX "" AUTORCC ON
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_definitions(rc_music_download_network_payload PRIVATE RC_MUSIC_DOWNLOAD_NETWORK_PAYLOAD)
    target_link_libraries(rc_music_download_network_payload PRIVATE rc_music_download_network)
    rc_music_configure_cpp(rc_music_download_network_payload)
    include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicMediaFoundation.cmake")
endif()

if(BUILD_TESTING)
    set(rc_music_download_network_test_units DownloadNetworkPolicy DownloadHttpResponse PinnedDownloadStream
        DirectDownloadSession FfmpegDownloadCodec)
    if(WIN32)
        list(APPEND rc_music_download_network_test_units
            WindowsDownloadWorkspace WindowsDownloadProcess WindowsDownloadInteractiveInput WindowsYoutubeResolverProcess WindowsYoutubeBrokerProcess WindowsYoutubeRuntime WindowsDownloadTlsRuntime DownloadWorkerProtocol
            WindowsDownloadNetworkWorker WindowsProcessDownloadIo WindowsDownloadExecution WindowsDownloadRecoveryValidation
            WindowsDownloadNetworkLaunch WindowsPreparedDownloadTask WindowsPreparedDownloadExecution
            WindowsPreparedMediaDownloadTask)
        add_executable(rc_music_download_io_helper
            "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsProcessDownloadIoHelper.cpp")
        target_link_libraries(rc_music_download_io_helper PRIVATE rc_music_download_network)
        target_compile_definitions(rc_music_download_io_helper PRIVATE
            RC_MUSIC_TEST_MP3_FIXTURE="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid/mp3-unicode-tags.mp3"
            RC_MUSIC_TEST_MP4_FIXTURE="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid/m4a-aac-lc.m4a")
        rc_music_configure_cpp(rc_music_download_io_helper)
        add_executable(rc_music_download_process_helper
            "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsDownloadProcessHelper.cpp")
        target_link_libraries(rc_music_download_process_helper PRIVATE Qt6::Core advapi32)
        rc_music_configure_cpp(rc_music_download_process_helper)
        add_executable(rc_music_youtube_broker_helper
            "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsYoutubeBrokerHelper.cpp")
        target_link_libraries(rc_music_youtube_broker_helper PRIVATE Qt6::Core)
        rc_music_configure_cpp(rc_music_youtube_broker_helper)
        add_executable(rc_music_download_tls_runtime_helper
            "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsDownloadTlsRuntimeHelper.cpp")
        target_link_libraries(rc_music_download_tls_runtime_helper PRIVATE rc_music_download_network)
        rc_music_configure_cpp(rc_music_download_tls_runtime_helper)
        get_target_property(rc_music_test_tls_core Qt6::Core IMPORTED_LOCATION_RELEASE)
        get_target_property(rc_music_test_tls_network Qt6::Network IMPORTED_LOCATION_RELEASE)
        file(SHA256 "${rc_music_test_tls_core}" rc_music_test_tls_core_sha256)
        file(SHA256 "${rc_music_test_tls_network}" rc_music_test_tls_network_sha256)
        set(RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN "" CACHE FILEPATH
            "Reviewed restricted TLS plugin for isolated synthetic tests only")
        set(rc_music_test_tls_plugin_sha256 "")
        if(RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN)
            if(NOT IS_ABSOLUTE "${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}" OR
               NOT EXISTS "${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}")
                message(FATAL_ERROR "RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN requires an existing absolute DLL path")
            endif()
            file(SHA256 "${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}" rc_music_test_tls_plugin_sha256)
        endif()
        set(RC_MUSIC_DOWNLOAD_TEST_CRT_DIR "" CACHE PATH
            "Reviewed matching MSVC CRT source directory for private network-launch tests only")
        if(RC_MUSIC_DOWNLOAD_TEST_CRT_DIR)
            if(NOT IS_ABSOLUTE "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}" OR
               NOT IS_DIRECTORY "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}")
                message(FATAL_ERROR "RC_MUSIC_DOWNLOAD_TEST_CRT_DIR requires an existing absolute directory")
            endif()
            foreach(dll IN ITEMS msvcp140.dll msvcp140_1.dll vcruntime140.dll vcruntime140_1.dll)
                if(NOT EXISTS "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}/${dll}" OR
                   IS_DIRECTORY "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}/${dll}")
                    message(FATAL_ERROR "Reviewed launch CRT fixture is missing ${dll}")
                endif()
            endforeach()
        endif()
        set(RC_MUSIC_DOWNLOAD_TEST_FFMPEG "" CACHE FILEPATH "Reviewed local FFmpeg for synthetic converter tests only")
        set(RC_MUSIC_DOWNLOAD_TEST_FFPROBE "" CACHE FILEPATH "Reviewed local ffprobe for synthetic converter tests only")
        if(RC_MUSIC_DOWNLOAD_TEST_FFMPEG AND RC_MUSIC_DOWNLOAD_TEST_FFPROBE)
            foreach(tool IN ITEMS RC_MUSIC_DOWNLOAD_TEST_FFMPEG RC_MUSIC_DOWNLOAD_TEST_FFPROBE)
                if(NOT IS_ABSOLUTE "${${tool}}" OR NOT EXISTS "${${tool}}")
                    message(FATAL_ERROR "${tool} requires an existing absolute executable path")
                endif()
            endforeach()
            list(APPEND rc_music_download_network_test_units WindowsLocalMediaConverter WindowsLocalMp3Validator
                WindowsRecoveredMediaValidator WindowsDownloadJobSession WindowsProcessDownloadJob)
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
        if(unit STREQUAL "WindowsDownloadProcess" OR unit STREQUAL "WindowsYoutubeResolverProcess" OR
           unit STREQUAL "WindowsDownloadInteractiveInput")
            add_dependencies(${target} rc_music_download_process_helper)
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_DOWNLOAD_PROCESS_HELPER_PATH="$<TARGET_FILE:rc_music_download_process_helper>")
        elseif(unit STREQUAL "WindowsYoutubeBrokerProcess")
            add_dependencies(${target} rc_music_youtube_broker_helper)
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_YOUTUBE_BROKER_HELPER_PATH="$<TARGET_FILE:rc_music_youtube_broker_helper>")
        elseif(unit STREQUAL "WindowsDownloadNetworkLaunch" OR unit STREQUAL "WindowsPreparedDownloadExecution")
            add_dependencies(${target} rc_music_download_network_bootstrap rc_music_download_network_payload)
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_TEST_BOOTSTRAP_PATH="$<TARGET_FILE:rc_music_download_network_bootstrap>"
                RC_MUSIC_TEST_PAYLOAD_PATH="$<TARGET_FILE:rc_music_download_network_payload>"
                RC_MUSIC_TEST_CRT_DIR="${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}"
                RC_MUSIC_TEST_TLS_CORE_PATH="${rc_music_test_tls_core}"
                RC_MUSIC_TEST_TLS_NETWORK_PATH="${rc_music_test_tls_network}"
                RC_MUSIC_TEST_TLS_PLUGIN_PATH="${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}")
        elseif(unit STREQUAL "WindowsDownloadTlsRuntime" OR unit STREQUAL "WindowsDownloadNetworkWorker" OR
               unit STREQUAL "WindowsProcessDownloadIo" OR unit STREQUAL "WindowsProcessDownloadJob")
            add_dependencies(${target} rc_music_download_tls_runtime_helper rc_music_download_network_worker)
            if(unit STREQUAL "WindowsProcessDownloadIo" OR unit STREQUAL "WindowsProcessDownloadJob")
                add_dependencies(${target} rc_music_download_io_helper)
                target_compile_definitions(${target} PRIVATE
                    RC_MUSIC_TEST_IO_HELPER_PATH="$<TARGET_FILE:rc_music_download_io_helper>")
                if(unit STREQUAL "WindowsProcessDownloadJob")
                    add_dependencies(${target} rc_music_media_foundation_worker)
                    target_compile_definitions(${target} PRIVATE
                        RC_MUSIC_TEST_FFMPEG_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFMPEG}"
                        RC_MUSIC_TEST_FFPROBE_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFPROBE}"
                        RC_MUSIC_TEST_MF_WORKER_PATH="$<TARGET_FILE:rc_music_media_foundation_worker>")
                endif()
            endif()
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_TEST_NETWORK_WORKER_PATH="$<TARGET_FILE:rc_music_download_network_worker>"
                RC_MUSIC_TEST_TLS_HELPER_PATH="$<TARGET_FILE:rc_music_download_tls_runtime_helper>"
                RC_MUSIC_TEST_TLS_CORE_PATH="${rc_music_test_tls_core}"
                RC_MUSIC_TEST_TLS_CORE_SHA256="${rc_music_test_tls_core_sha256}"
                RC_MUSIC_TEST_TLS_NETWORK_PATH="${rc_music_test_tls_network}"
                RC_MUSIC_TEST_TLS_NETWORK_SHA256="${rc_music_test_tls_network_sha256}"
                RC_MUSIC_TEST_TLS_PLUGIN_PATH="${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}"
                RC_MUSIC_TEST_TLS_PLUGIN_SHA256="${rc_music_test_tls_plugin_sha256}")
        elseif(unit STREQUAL "WindowsDownloadRecoveryValidation")
            if(RC_MUSIC_DOWNLOAD_TEST_FFMPEG AND RC_MUSIC_DOWNLOAD_TEST_FFPROBE)
                target_compile_definitions(${target} PRIVATE
                    RC_MUSIC_TEST_FFMPEG_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFMPEG}"
                    RC_MUSIC_TEST_FFPROBE_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFPROBE}"
                    RC_MUSIC_TEST_AUDIO_FIXTURES="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid")
            endif()
        elseif(unit STREQUAL "WindowsLocalMediaConverter" OR unit STREQUAL "WindowsLocalMp3Validator" OR
               unit STREQUAL "WindowsRecoveredMediaValidator" OR unit STREQUAL "WindowsDownloadJobSession")
            add_dependencies(${target} rc_music_download_converter_helper)
            if(unit STREQUAL "WindowsRecoveredMediaValidator")
                add_dependencies(${target} rc_music_media_foundation_worker)
                target_compile_definitions(${target} PRIVATE
                    RC_MUSIC_TEST_MF_WORKER_PATH="$<TARGET_FILE:rc_music_media_foundation_worker>")
            endif()
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
        if(unit STREQUAL "WindowsYoutubeRuntime")
            set_tests_properties("rc_music.download-network.${unit}" PROPERTIES
                LABELS "download-network;qt;FR-050;FR-052")
        endif()
        if(unit STREQUAL "WindowsYoutubeBrokerProcess")
            set_tests_properties("rc_music.download-network.${unit}" PROPERTIES
                LABELS "download-network;qt;FR-050" TIMEOUT 120)
        endif()
    endforeach()
endif()
