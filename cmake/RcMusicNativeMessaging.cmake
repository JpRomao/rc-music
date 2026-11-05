include_guard(GLOBAL)
# Pure identity validation is callable by script-mode contract tests, without
# creating targets or finding Qt. The two ingress profiles remain independent.
function(rc_music_validate_native_messaging_identities main_origin main_endpoint youtube_origin youtube_endpoint)
    foreach(profile IN ITEMS main youtube)
        set(origin "${${profile}_origin}")
        set(endpoint "${${profile}_endpoint}")
        if(NOT origin STREQUAL "")
            if(NOT origin MATCHES "^chrome-extension://[a-p]+/$")
                message(FATAL_ERROR "Invalid trusted Native Messaging ${profile} origin")
            endif()
            string(LENGTH "${origin}" origin_length)
            if(NOT origin_length EQUAL 52)
                message(FATAL_ERROR "Native Messaging ${profile} origin must contain exactly 32 extension-ID letters")
            endif()
        endif()
        if(NOT endpoint STREQUAL "")
            string(LENGTH "${endpoint}" endpoint_length)
            if(NOT endpoint MATCHES "^[A-Za-z0-9_-]+$" OR endpoint_length GREATER 96)
                message(FATAL_ERROR "Invalid trusted Native Messaging ${profile} endpoint")
            endif()
        endif()
        if((origin STREQUAL "" AND NOT endpoint STREQUAL "") OR
           (NOT origin STREQUAL "" AND endpoint STREQUAL ""))
            message(FATAL_ERROR "Native Messaging ${profile} identity must be wholly configured or disabled")
        endif()
    endforeach()
    if(NOT main_origin STREQUAL "" AND NOT youtube_origin STREQUAL "")
        string(TOLOWER "${main_origin}" main_origin_folded)
        string(TOLOWER "${youtube_origin}" youtube_origin_folded)
        string(TOLOWER "${main_endpoint}" main_endpoint_folded)
        string(TOLOWER "${youtube_endpoint}" youtube_endpoint_folded)
        if(main_origin_folded STREQUAL youtube_origin_folded)
            message(FATAL_ERROR "Principal and YouTube Native Messaging origins must be distinct")
        endif()
        # Windows pipe names ignore ASCII case. Do not create a second profile
        # that can contend for the principal owner's existing namespace.
        if(main_endpoint_folded STREQUAL youtube_endpoint_folded)
            message(FATAL_ERROR "Principal and YouTube Native Messaging endpoints must be distinct ignoring case")
        endif()
    endif()
endfunction()
if(CMAKE_SCRIPT_MODE_FILE)
    return()
endif()
if(NOT WIN32)
    message(FATAL_ERROR "Native Messaging transport is currently Windows-only; Linux remains deferred")
endif()
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Network)
add_library(rc_music_native_messaging STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeHostLaunch.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeDownloadPeerImages.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsNativeDownloadPeerCheck.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtNativeDownloadServer.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtNativeDownloadClient.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtNativeYoutubeAudioServer.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtNativeYoutubeAudioClient.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsNativeDownloadLease.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsNativeDownloadPipeReservation.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsNativeMessagingIo.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsNativeHostWatchdog.cpp")
target_include_directories(rc_music_native_messaging PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_native_messaging PUBLIC rc_music_browser_integration Qt6::Network)
target_link_libraries(rc_music_native_messaging PRIVATE advapi32)
set_target_properties(rc_music_native_messaging PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_native_messaging)

# Trusted build composition, not argv/env/page configuration. Empty defaults
# deliberately produce a disabled host until installed identity is decided.
set(RC_MUSIC_NATIVE_MAIN_ORIGIN "" CACHE STRING "Trusted exact origin of the principal extension")
set(RC_MUSIC_NATIVE_ENDPOINT "" CACHE STRING "Trusted dedicated app-owner pipe name (no path)")
set(RC_MUSIC_NATIVE_YOUTUBE_ORIGIN "" CACHE STRING "Trusted exact origin of the separate YouTube extension")
set(RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT "" CACHE STRING "Trusted separate YouTube app-owner pipe name (no path)")
rc_music_validate_native_messaging_identities("${RC_MUSIC_NATIVE_MAIN_ORIGIN}" "${RC_MUSIC_NATIVE_ENDPOINT}"
    "${RC_MUSIC_NATIVE_YOUTUBE_ORIGIN}" "${RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT}")
set(native_config_origin "${RC_MUSIC_NATIVE_MAIN_ORIGIN}")
set(native_config_endpoint "${RC_MUSIC_NATIVE_ENDPOINT}")
set(native_config_owner_images "QStringLiteral(\"RCMusic.exe\")")
set(native_config_host_images "QStringLiteral(\"rc_music_native_host.exe\")")
configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/native-host/NativeHostBuildConfiguration.hpp" @ONLY)
add_executable(rc_music_native_host "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
target_include_directories(rc_music_native_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/native-host")
target_link_libraries(rc_music_native_host PRIVATE rc_music_native_messaging)
rc_music_configure_cpp(rc_music_native_host)

# A separate executable/profile selects the typed YouTube session. It has no
# installation or registration step, and empty trusted defaults disable it.
set(native_config_origin "${RC_MUSIC_NATIVE_YOUTUBE_ORIGIN}")
set(native_config_endpoint "${RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT}")
set(native_config_owner_images "QStringLiteral(\"RCMusic.exe\")")
set(native_config_host_images "QStringLiteral(\"rc_music_youtube_native_host.exe\")")
configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/youtube-native-host/NativeHostBuildConfiguration.hpp" @ONLY)
add_executable(rc_music_youtube_native_host "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
target_compile_definitions(rc_music_youtube_native_host PRIVATE RC_MUSIC_YOUTUBE_NATIVE_HOST)
target_include_directories(rc_music_youtube_native_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/youtube-native-host")
target_link_libraries(rc_music_youtube_native_host PRIVATE rc_music_native_messaging)
rc_music_configure_cpp(rc_music_youtube_native_host)

if(BUILD_TESTING)
    add_test(NAME rc_music.native.build-boundary COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/NativeMessagingBuildBoundaryTests.cmake")
    set_tests_properties(rc_music.native.build-boundary PROPERTIES LABELS "native;boundary" TIMEOUT 15)
    add_test(NAME rc_music.native.youtube-configuration COMMAND "${CMAKE_COMMAND}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/NativeYoutubeHostConfigurationTests.cmake")
    set_tests_properties(rc_music.native.youtube-configuration PROPERTIES LABELS "native;boundary;youtube" TIMEOUT 15)
    foreach(unit IN ITEMS NativeHostLaunch NativeHostInputStep NativeDownloadPeerImages QtNativeDownloadTransport QtNativeDownloadPeerReentrancy QtNativeYoutubeAudioServer QtNativeYoutubeAudioClient WindowsNativeMessagingIo WindowsNativeDownloadLease WindowsNativeDownloadPipeReservation WindowsNativeDownloadPeerCheck)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
        target_link_libraries(${target} PRIVATE rc_music_native_messaging Qt6::Test)
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        add_test(NAME "rc_music.native.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.native.${unit}" PROPERTIES
            LABELS "windows;native;qt;FR-051;FR-053" TIMEOUT 25
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endforeach()
    # Only the separate test executable gets synthetic identity. It uses the
    # same real host main/transport, with no argv/env overrides or prod hooks.
    string(SHA256 native_test_identity "${CMAKE_BINARY_DIR}")
    string(SUBSTRING "${native_test_identity}" 0 16 native_test_identity)
    set(native_config_origin "chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/")
    set(native_config_endpoint "rc-music-native-test-${native_test_identity}")
    set(native_config_owner_images "QStringLiteral(\"rc_music_NativeHostProcess_tests.exe\"), QStringLiteral(\"rc_music_test_shell.exe\")")
    set(native_config_host_images "QStringLiteral(\"rc_music_native_host_test.exe\")")
    set(RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-native-host")
    configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
        "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}/NativeHostBuildConfiguration.hpp" @ONLY)
    add_executable(rc_music_native_host_test "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
    # Synthetic host and its allowed owners share a payload directory just as
    # the future installed pair will. Never authorize peers through CWD or ../.
    set_target_properties(rc_music_native_host_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/process")
    target_include_directories(rc_music_native_host_test PRIVATE "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}")
    target_link_libraries(rc_music_native_host_test PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_native_host_test)
    # Always-empty fixture is independent of the real host's optional configured
    # production identity; configured builds still prove the disabled contract.
    set(native_config_origin "")
    set(native_config_endpoint "")
    set(native_config_owner_images "")
    set(native_config_host_images "")
    configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
        "${CMAKE_CURRENT_BINARY_DIR}/test-native-host-disabled/NativeHostBuildConfiguration.hpp" @ONLY)
    add_executable(rc_music_native_host_disabled_test "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
    target_include_directories(rc_music_native_host_disabled_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/test-native-host-disabled")
    target_link_libraries(rc_music_native_host_disabled_test PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_native_host_disabled_test)
    add_executable(rc_music_native_watchdog_helper "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeHostWatchdogHelper.cpp")
    target_link_libraries(rc_music_native_watchdog_helper PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_native_watchdog_helper)
    add_executable(rc_music_NativeHostProcess_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeHostProcessTests.cpp")
    set_target_properties(rc_music_NativeHostProcess_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/process")
    target_include_directories(rc_music_NativeHostProcess_tests PRIVATE "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}")
    target_link_libraries(rc_music_NativeHostProcess_tests PRIVATE rc_music_native_messaging Qt6::Test)
    set_target_properties(rc_music_NativeHostProcess_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_NativeHostProcess_tests)
    add_dependencies(rc_music_NativeHostProcess_tests rc_music_native_host_test rc_music_native_host_disabled_test rc_music_native_watchdog_helper)
    add_test(NAME rc_music.native.host-process COMMAND rc_music_NativeHostProcess_tests
        "$<TARGET_FILE:rc_music_native_host_test>" "$<TARGET_FILE:rc_music_native_host_disabled_test>" "$<TARGET_FILE:rc_music_native_watchdog_helper>")
    set_tests_properties(rc_music.native.host-process PROPERTIES
        LABELS "windows;native;qt;process;FR-051;FR-053" TIMEOUT 90
        RESOURCE_LOCK "rc_music_native_host_test_endpoint"
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    # Only these two test executables select the typed YouTube host branch.
    # Both use the real main and stdio; production remains disabled/unconfigured.
    set(native_config_origin "chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/")
    set(native_config_endpoint "rc-music-youtube-native-test-${native_test_identity}")
    set(native_config_owner_images "QStringLiteral(\"rc_music_NativeYoutubeHostProcess_tests.exe\")")
    set(native_config_host_images "QStringLiteral(\"rc_music_youtube_native_host_test.exe\")")
    set(native_youtube_test_config "${CMAKE_CURRENT_BINARY_DIR}/test-youtube-native-host")
    configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
        "${native_youtube_test_config}/NativeHostBuildConfiguration.hpp" @ONLY)
    add_executable(rc_music_youtube_native_host_test "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
    target_compile_definitions(rc_music_youtube_native_host_test PRIVATE RC_MUSIC_YOUTUBE_NATIVE_HOST)
    set_target_properties(rc_music_youtube_native_host_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/process")
    target_include_directories(rc_music_youtube_native_host_test PRIVATE "${native_youtube_test_config}")
    target_link_libraries(rc_music_youtube_native_host_test PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_youtube_native_host_test)
    set(native_config_origin "")
    set(native_config_endpoint "")
    set(native_config_owner_images "")
    set(native_config_host_images "")
    set(native_youtube_disabled_config "${CMAKE_CURRENT_BINARY_DIR}/test-youtube-native-host-disabled")
    configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
        "${native_youtube_disabled_config}/NativeHostBuildConfiguration.hpp" @ONLY)
    add_executable(rc_music_youtube_native_host_disabled_test "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
    target_compile_definitions(rc_music_youtube_native_host_disabled_test PRIVATE RC_MUSIC_YOUTUBE_NATIVE_HOST)
    target_include_directories(rc_music_youtube_native_host_disabled_test PRIVATE "${native_youtube_disabled_config}")
    target_link_libraries(rc_music_youtube_native_host_disabled_test PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_youtube_native_host_disabled_test)
    add_executable(rc_music_NativeYoutubeHostProcess_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeYoutubeHostProcessTests.cpp")
    set_target_properties(rc_music_NativeYoutubeHostProcess_tests PROPERTIES
        AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/process")
    target_include_directories(rc_music_NativeYoutubeHostProcess_tests PRIVATE "${native_youtube_test_config}")
    target_link_libraries(rc_music_NativeYoutubeHostProcess_tests PRIVATE rc_music_native_messaging Qt6::Test)
    rc_music_configure_cpp(rc_music_NativeYoutubeHostProcess_tests)
    add_dependencies(rc_music_NativeYoutubeHostProcess_tests rc_music_youtube_native_host_test rc_music_youtube_native_host_disabled_test)
    add_test(NAME rc_music.native.youtube-host-process COMMAND rc_music_NativeYoutubeHostProcess_tests
        "$<TARGET_FILE:rc_music_youtube_native_host_test>" "$<TARGET_FILE:rc_music_youtube_native_host_disabled_test>")
    set_tests_properties(rc_music.native.youtube-host-process PROPERTIES
        LABELS "windows;native;qt;process;youtube;FR-051;FR-053" TIMEOUT 90
        RESOURCE_LOCK "rc_music_youtube_native_host_test_endpoint"
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    add_executable(rc_music_native_owner_helper "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeDownloadOwnerHelper.cpp")
    target_link_libraries(rc_music_native_owner_helper PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_native_owner_helper)
    add_executable(rc_music_NativeDownloadOwnerProcess_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeDownloadOwnerProcessTests.cpp")
    target_link_libraries(rc_music_NativeDownloadOwnerProcess_tests PRIVATE rc_music_native_messaging Qt6::Test)
    set_target_properties(rc_music_NativeDownloadOwnerProcess_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_NativeDownloadOwnerProcess_tests)
    add_dependencies(rc_music_NativeDownloadOwnerProcess_tests rc_music_native_owner_helper)
    add_test(NAME rc_music.native.owner-process COMMAND rc_music_NativeDownloadOwnerProcess_tests
        "$<TARGET_FILE:rc_music_native_owner_helper>")
    set_tests_properties(rc_music.native.owner-process PROPERTIES
        LABELS "windows;native;qt;process;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    add_executable(rc_music_native_peer_helper "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeDownloadPeerHelper.cpp")
    target_link_libraries(rc_music_native_peer_helper PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_native_peer_helper)
    add_executable(rc_music_NativeDownloadPeerProcess_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/NativeDownloadPeerProcessTests.cpp")
    target_link_libraries(rc_music_NativeDownloadPeerProcess_tests PRIVATE rc_music_native_messaging Qt6::Test)
    set_target_properties(rc_music_NativeDownloadPeerProcess_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_NativeDownloadPeerProcess_tests)
    add_dependencies(rc_music_NativeDownloadPeerProcess_tests rc_music_native_peer_helper)
    add_test(NAME rc_music.native.peer-process COMMAND rc_music_NativeDownloadPeerProcess_tests
        "$<TARGET_FILE:rc_music_native_peer_helper>")
    set_tests_properties(rc_music.native.peer-process PROPERTIES
        LABELS "windows;native;qt;process;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
