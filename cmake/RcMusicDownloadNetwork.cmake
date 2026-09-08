include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Network)
add_library(rc_music_download_network STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadNetworkPolicy.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadHttpResponse.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/PinnedDownloadStream.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DirectDownloadSession.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtDownloadIo.cpp"
)
target_link_libraries(rc_music_download_network PUBLIC rc_music_browser_integration Qt6::Network)
target_include_directories(rc_music_download_network PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
set_target_properties(rc_music_download_network PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_download_network)

if(BUILD_TESTING)
    foreach(unit IN ITEMS DownloadNetworkPolicy DownloadHttpResponse PinnedDownloadStream DirectDownloadSession)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_link_libraries(${target} PRIVATE rc_music_download_network Qt6::Test)
        target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        add_test(NAME "rc_music.download-network.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.download-network.${unit}" PROPERTIES
            LABELS "download-network;qt;FR-051;FR-053" TIMEOUT 30
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endforeach()
endif()
