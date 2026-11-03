include_guard(GLOBAL)
if(NOT WIN32)
    message(FATAL_ERROR "Native Messaging transport is currently Windows-only; Linux remains deferred")
endif()
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Network)
add_library(rc_music_native_messaging STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeHostLaunch.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtNativeDownloadServer.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtNativeDownloadClient.cpp"
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
if(NOT RC_MUSIC_NATIVE_MAIN_ORIGIN STREQUAL "")
    if(NOT RC_MUSIC_NATIVE_MAIN_ORIGIN MATCHES "^chrome-extension://[a-p]+/$")
        message(FATAL_ERROR "Invalid trusted Native Messaging origin")
    endif()
    string(LENGTH "${RC_MUSIC_NATIVE_MAIN_ORIGIN}" native_origin_length)
    if(NOT native_origin_length EQUAL 52)
        message(FATAL_ERROR "Native Messaging origin must contain exactly 32 extension-ID letters")
    endif()
endif()
if(NOT RC_MUSIC_NATIVE_ENDPOINT STREQUAL "")
    string(LENGTH "${RC_MUSIC_NATIVE_ENDPOINT}" native_endpoint_length)
    if(NOT RC_MUSIC_NATIVE_ENDPOINT MATCHES "^[A-Za-z0-9_-]+$" OR native_endpoint_length GREATER 96)
        message(FATAL_ERROR "Invalid trusted Native Messaging endpoint")
    endif()
endif()
if((RC_MUSIC_NATIVE_MAIN_ORIGIN STREQUAL "" AND NOT RC_MUSIC_NATIVE_ENDPOINT STREQUAL "") OR
   (NOT RC_MUSIC_NATIVE_MAIN_ORIGIN STREQUAL "" AND RC_MUSIC_NATIVE_ENDPOINT STREQUAL ""))
    message(FATAL_ERROR "Native Messaging trusted identity must be wholly configured or disabled")
endif()
set(native_config_origin "${RC_MUSIC_NATIVE_MAIN_ORIGIN}")
set(native_config_endpoint "${RC_MUSIC_NATIVE_ENDPOINT}")
configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/native-host/NativeHostBuildConfiguration.hpp" @ONLY)
add_executable(rc_music_native_host "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
target_include_directories(rc_music_native_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/native-host")
target_link_libraries(rc_music_native_host PRIVATE rc_music_native_messaging)
rc_music_configure_cpp(rc_music_native_host)

if(BUILD_TESTING)
    add_test(NAME rc_music.native.build-boundary COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/NativeMessagingBuildBoundaryTests.cmake")
    set_tests_properties(rc_music.native.build-boundary PROPERTIES LABELS "native;boundary" TIMEOUT 15)
    foreach(unit IN ITEMS NativeHostLaunch NativeHostInputStep QtNativeDownloadTransport WindowsNativeMessagingIo WindowsNativeDownloadLease WindowsNativeDownloadPipeReservation)
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
    set(RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-native-host")
    configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
        "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}/NativeHostBuildConfiguration.hpp" @ONLY)
    add_executable(rc_music_native_host_test "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
    target_include_directories(rc_music_native_host_test PRIVATE "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}")
    target_link_libraries(rc_music_native_host_test PRIVATE rc_music_native_messaging)
    rc_music_configure_cpp(rc_music_native_host_test)
    # Always-empty fixture is independent of the real host's optional configured
    # production identity; configured builds still prove the disabled contract.
    set(native_config_origin "")
    set(native_config_endpoint "")
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
endif()
