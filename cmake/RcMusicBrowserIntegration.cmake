include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core)
include("${CMAKE_CURRENT_LIST_DIR}/RcMusicMediaData.cmake")
add_library(rc_music_browser_integration STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeDownloadCodec.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeMessageFrame.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeDownloadSession.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadJobsModel.cpp"
)
target_include_directories(rc_music_browser_integration PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_browser_integration PUBLIC rc_music_downloads rc_music_media_data Qt6::Core)
set_target_properties(rc_music_browser_integration PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_browser_integration)

if(BUILD_TESTING)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)
    foreach(unit IN ITEMS NativeDownloadCodec NativeMessageFrame NativeDownloadSession NativeDownloadService DownloadJobsModel)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
        target_link_libraries(${target} PRIVATE rc_music_browser_integration Qt6::Test)
        if(unit STREQUAL "NativeDownloadCodec")
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_NATIVE_PROTOCOL_VECTORS_PATH="${RC_MUSIC_SOURCE_ROOT}/tests/extensions/native-protocol-vectors.json")
        endif()
        rc_music_configure_cpp(${target})
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        add_test(NAME "rc_music.browser.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.browser.${unit}" PROPERTIES
            LABELS "browser;qt;FR-051;FR-053" TIMEOUT 15
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endforeach()
endif()
