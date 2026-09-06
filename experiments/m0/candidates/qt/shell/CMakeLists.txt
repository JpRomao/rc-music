cmake_minimum_required(VERSION 3.31)

project(m0_qt_shell_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/RcMusicCore.cmake")

option(M0_QT_SHELL_SUPPORT "Build the Qt fixture repository and queue model" OFF)
option(M0_QT_AUDIO_SUPPORT "Build the Qt Multimedia playback adapter" OFF)

set(m0_fixture_scenarios_enabled OFF)
if(BUILD_TESTING OR (M0_QT_SHELL_SUPPORT AND NOT M0_QT_AUDIO_SUPPORT))
    set(m0_fixture_scenarios_enabled ON)
endif()

set(RC_MUSIC_DESKTOP_SHELL_SUPPORT "${M0_QT_SHELL_SUPPORT}")
set(RC_MUSIC_DESKTOP_AUDIO_SUPPORT "${M0_QT_AUDIO_SUPPORT}")
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicDesktop.cmake")

# Historical tests/scenarios consume the same promoted implementation.
include_directories(
    "${RC_MUSIC_SOURCE_ROOT}/src"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation"
    "${RC_MUSIC_SOURCE_ROOT}/src/app"
    include src app
)
if(m0_fixture_scenarios_enabled)
    rc_music_add_test_support()
    target_link_libraries(m0_qt_core INTERFACE rc_music_test_support)
endif()

set(M0_CATCH2_SOURCE_DIR "" CACHE PATH "Path to the authenticated Catch2 source tree")

if(M0_QT_SHELL_SUPPORT)
    set(M0_PROTOCOL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../protocol/protocol.v1.json")
    set(M0_FIXTURE_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/manifest.v1.json")
    set(M0_FIXTURE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/fixture.v1.json")
    set(M0_QUEUE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/queue.v1.json")
    set(M0_PLACEHOLDER_ASSET_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/assets/cover-placeholder.svg")
    set(M0_TRACK_ASSET_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/assets/cover-track.svg")
    foreach(input_path IN ITEMS
            M0_PROTOCOL_FILE
            M0_FIXTURE_MANIFEST_FILE
            M0_FIXTURE_FILE
            M0_QUEUE_FILE
            M0_PLACEHOLDER_ASSET_FILE
            M0_TRACK_ASSET_FILE)
        cmake_path(NORMAL_PATH ${input_path})
        if(NOT EXISTS "${${input_path}}")
            message(FATAL_ERROR "${input_path} does not exist")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${M0_PROTOCOL_FILE}"
        "${M0_FIXTURE_MANIFEST_FILE}"
        "${M0_FIXTURE_FILE}"
        "${M0_QUEUE_FILE}"
        "${M0_PLACEHOLDER_ASSET_FILE}"
        "${M0_TRACK_ASSET_FILE}"
    )

    file(READ "${M0_PROTOCOL_FILE}" m0_protocol_json)
    string(JSON m0_expected_manifest_sha256 GET "${m0_protocol_json}" fixture_manifest_sha256)
    string(TOLOWER "${m0_expected_manifest_sha256}" m0_expected_manifest_sha256)
    file(SHA256 "${M0_FIXTURE_MANIFEST_FILE}" m0_actual_manifest_sha256)
    if(NOT m0_actual_manifest_sha256 STREQUAL m0_expected_manifest_sha256)
        message(FATAL_ERROR "The frozen fixture manifest is not anchored by protocol.v1.json")
    endif()

    file(READ "${M0_FIXTURE_MANIFEST_FILE}" m0_fixture_manifest_json)
    string(JSON m0_fixture_manifest_file_count LENGTH "${m0_fixture_manifest_json}" files)
    math(EXPR m0_fixture_manifest_last_index "${m0_fixture_manifest_file_count} - 1")
    function(m0_read_fixture_manifest_entry relative_path output_sha256 output_size)
        foreach(index RANGE 0 ${m0_fixture_manifest_last_index})
            string(JSON candidate_relative_path GET "${m0_fixture_manifest_json}" files ${index} relative_path)
            if(candidate_relative_path STREQUAL relative_path)
                string(JSON expected_sha256 GET "${m0_fixture_manifest_json}" files ${index} sha256)
                string(JSON expected_size GET "${m0_fixture_manifest_json}" files ${index} size_bytes)
                set(${output_sha256} "${expected_sha256}" PARENT_SCOPE)
                set(${output_size} "${expected_size}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
        message(FATAL_ERROR "The frozen fixture manifest does not contain ${relative_path}")
    endfunction()

    m0_read_fixture_manifest_entry("fixture.v1.json" m0_expected_fixture_sha256 m0_expected_fixture_size)
    m0_read_fixture_manifest_entry("queue.v1.json" m0_expected_queue_sha256 m0_expected_queue_size)
    m0_read_fixture_manifest_entry("assets/cover-placeholder.svg" m0_expected_placeholder_sha256 m0_expected_placeholder_size)
    m0_read_fixture_manifest_entry("assets/cover-track.svg" m0_expected_track_sha256 m0_expected_track_size)
    foreach(input_id IN ITEMS fixture queue)
        string(TOUPPER "${input_id}" input_id_upper)
        file(SHA256 "${M0_${input_id_upper}_FILE}" actual_sha256)
        file(SIZE "${M0_${input_id_upper}_FILE}" actual_size)
        if(NOT actual_sha256 STREQUAL m0_expected_${input_id}_sha256 OR
           NOT actual_size EQUAL m0_expected_${input_id}_size)
            message(FATAL_ERROR "${input_id}.v1.json does not match the protocol-anchored fixture manifest")
        endif()
    endforeach()
    foreach(asset_id IN ITEMS placeholder track)
        string(TOUPPER "${asset_id}" asset_id_upper)
        if(NOT EXISTS "${M0_${asset_id_upper}_ASSET_FILE}" OR IS_DIRECTORY "${M0_${asset_id_upper}_ASSET_FILE}")
            message(FATAL_ERROR "${asset_id} SVG is not a regular file")
        endif()
        file(SHA256 "${M0_${asset_id_upper}_ASSET_FILE}" actual_sha256)
        file(SIZE "${M0_${asset_id_upper}_ASSET_FILE}" actual_size)
        if(NOT actual_sha256 STREQUAL m0_expected_${asset_id}_sha256 OR
           NOT actual_size EQUAL m0_expected_${asset_id}_size)
            message(FATAL_ERROR "${asset_id} SVG does not match the authenticated manifest")
        endif()
    endforeach()

    # Authenticate the copies actually embedded by the shared product target.
    foreach(asset_id IN ITEMS placeholder track)
        set(promoted_asset "${RC_MUSIC_SOURCE_ROOT}/assets/cover-${asset_id}.svg")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${promoted_asset}")
        file(SHA256 "${promoted_asset}" actual_sha256)
        file(SIZE "${promoted_asset}" actual_size)
        if(NOT actual_sha256 STREQUAL m0_expected_${asset_id}_sha256 OR
           NOT actual_size EQUAL m0_expected_${asset_id}_size)
            message(FATAL_ERROR "Promoted ${asset_id} SVG does not match the authenticated manifest")
        endif()
    endforeach()

    if(m0_fixture_scenarios_enabled)
        target_sources(m0_qt_shell_support PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/include/m0/adapters/fixture/FixtureRepository.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/include/m0/presentation/QueueListModel.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/adapters/fixture/FixtureDecoder.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/adapters/fixture/FixtureRepository.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/presentation/QueueListModel.cpp"
        )
        target_compile_definitions(m0_qt_shell_support
            PUBLIC RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
            PRIVATE
                M0_FROZEN_FIXTURE_SHA256="${m0_expected_fixture_sha256}"
                M0_FROZEN_QUEUE_SHA256="${m0_expected_queue_sha256}"
        )
    endif()
    if(m0_fixture_scenarios_enabled)
        target_include_directories(m0_qt_shell_support PUBLIC
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/include"
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support")
        target_include_directories(m0_qt_shell_ui PRIVATE src src/presentation)
        target_compile_definitions(m0_qt_shell_ui PUBLIC RC_MUSIC_ENABLE_FIXTURE_SCENARIOS)
        target_sources(m0_qt_shell_ui PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/support/presentation/PlayerPresenterScenarioControl.hpp")
    endif()
    if(m0_fixture_scenarios_enabled)
        add_library(m0_qt_fixture_data STATIC EXCLUDE_FROM_ALL)
        target_link_libraries(m0_qt_fixture_data PRIVATE Qt6::Core)
        set_target_properties(m0_qt_fixture_data PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        set_source_files_properties("${M0_FIXTURE_FILE}" PROPERTIES QT_RESOURCE_ALIAS "data/fixture.v1.json")
        set_source_files_properties("${M0_QUEUE_FILE}" PROPERTIES QT_RESOURCE_ALIAS "data/queue.v1.json")
        qt_add_resources(m0_qt_fixture_data m0_qt_fixture_data
            PREFIX "/m0"
            FILES
                "${M0_FIXTURE_FILE}"
                "${M0_QUEUE_FILE}"
        )
    endif()
    if(NOT M0_QT_AUDIO_SUPPORT)
        target_link_libraries(m0_qt_shell PRIVATE m0_qt_fixture_data)
    endif()
    if(BUILD_TESTING)
        find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test QuickTest)
    endif()

    if(BUILD_TESTING)
        target_compile_definitions(m0_qt_shell PRIVATE M0_QT_SHELL_TEST_HOOKS)
        add_executable(m0_qt_diagnostic_binding_tests tests/qtest/ShellDiagnosticBindingTests.cpp)
        target_link_libraries(m0_qt_diagnostic_binding_tests PRIVATE m0_qt_diagnostic_binding Qt6::Test)
        target_compile_definitions(m0_qt_diagnostic_binding_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}" M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}")
        target_compile_features(m0_qt_diagnostic_binding_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_diagnostic_binding_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_diagnostic_binding_tests PRIVATE /W4 /WX /permissive-)
        endif()
        add_test(NAME m0.qt.diagnostics.binding COMMAND m0_qt_diagnostic_binding_tests)
        set_tests_properties(m0.qt.diagnostics.binding PROPERTIES TIMEOUT 30)
        add_executable(m0_qt_shell_platform_tests tests/qtest/QtShellPlatformTests.cpp)
        target_link_libraries(m0_qt_shell_platform_tests PRIVATE m0_qt_shell_platform Qt6::Test Qt6::Widgets)
        target_compile_features(m0_qt_shell_platform_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_shell_platform_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_platform_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_platform_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.shell-platform COMMAND m0_qt_shell_platform_tests)
        add_executable(m0_qt_audio_file_locator_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtAudioFileLocatorTests.cpp")
        target_link_libraries(m0_qt_audio_file_locator_tests PRIVATE m0_qt_audio_file_locator Qt6::Test Qt6::Widgets)
        target_compile_features(m0_qt_audio_file_locator_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_audio_file_locator_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_audio_file_locator_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_audio_file_locator_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.audio-file-locator COMMAND m0_qt_audio_file_locator_tests -o -,txt)
        add_executable(m0_qt_audio_files_picker_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtAudioFilesPickerTests.cpp")
        target_link_libraries(m0_qt_audio_files_picker_tests PRIVATE m0_qt_audio_file_locator Qt6::Test Qt6::Widgets)
        target_compile_features(m0_qt_audio_files_picker_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_audio_files_picker_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_audio_files_picker_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_audio_files_picker_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.audio-files-picker COMMAND m0_qt_audio_files_picker_tests -o -,txt)
        if(WIN32)
            add_executable(m0_qt_windows_smtc_tests tests/qtest/WindowsSmtcAdapterTests.cpp)
            target_link_libraries(m0_qt_windows_smtc_tests PRIVATE m0_qt_windows_smtc Qt6::Test)
            target_compile_features(m0_qt_windows_smtc_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_windows_smtc_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_windows_smtc_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_windows_smtc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
            endif()
            add_test(NAME m0.qt.windows.smtc COMMAND m0_qt_windows_smtc_tests)

            add_executable(m0_qt_windows_smtc_native_tests
                tests/qtest/WindowsSmtcNativeSmokeTests.cpp
            )
            target_link_libraries(m0_qt_windows_smtc_native_tests PRIVATE
                m0_qt_windows_smtc Qt6::Test windowsapp
            )
            target_compile_features(m0_qt_windows_smtc_native_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_windows_smtc_native_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_windows_smtc_native_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_windows_smtc_native_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror
                )
            endif()
            add_test(NAME m0.qt.windows.smtc-native COMMAND m0_qt_windows_smtc_native_tests)

            add_executable(m0_qt_windows_power_events_tests
                tests/qtest/WindowsPowerEventsTests.cpp
            )
            target_link_libraries(m0_qt_windows_power_events_tests PRIVATE
                m0_qt_windows_power_events Qt6::Test
            )
            target_compile_features(m0_qt_windows_power_events_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_windows_power_events_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_windows_power_events_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_windows_power_events_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror
                )
            endif()
            add_test(NAME m0.qt.windows.power-events COMMAND m0_qt_windows_power_events_tests
                -o "${CMAKE_CURRENT_BINARY_DIR}/windows-power-events-test.txt,txt"
                -o -,txt
            )
            add_executable(m0_qt_windows_taskbar_events_tests tests/qtest/WindowsTaskbarEventsTests.cpp)
            target_link_libraries(m0_qt_windows_taskbar_events_tests PRIVATE m0_qt_shell_platform Qt6::Test)
            target_compile_features(m0_qt_windows_taskbar_events_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_windows_taskbar_events_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_windows_taskbar_events_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_windows_taskbar_events_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
            endif()
            add_test(NAME m0.qt.windows.taskbar-events COMMAND m0_qt_windows_taskbar_events_tests -o -,txt)
            add_executable(m0_qt_windows_tray_recovery_tests tests/qtest/WindowsTrayRecoveryTests.cpp)
            target_link_libraries(m0_qt_windows_tray_recovery_tests PRIVATE Qt6::Widgets Qt6::Test user32 shell32)
            target_compile_features(m0_qt_windows_tray_recovery_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_windows_tray_recovery_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_windows_tray_recovery_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_windows_tray_recovery_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
            endif()
            add_test(NAME m0.qt.windows.tray-recovery COMMAND m0_qt_windows_tray_recovery_tests -o -,txt)
        endif()
        add_executable(m0_qt_shell_platform_binding_tests tests/qtest/ShellPlatformBindingTests.cpp)
        target_link_libraries(m0_qt_shell_platform_binding_tests PRIVATE
            m0_qt_shell_platform m0_qt_shell_support m0_qt_shell_ui Qt6::Test Qt6::Widgets)
        target_include_directories(m0_qt_shell_platform_binding_tests PRIVATE src)
        target_compile_features(m0_qt_shell_platform_binding_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_shell_platform_binding_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_shell_platform_binding_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_platform_binding_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_platform_binding_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.shell-platform-binding COMMAND m0_qt_shell_platform_binding_tests)
        add_executable(m0_qt_system_media_controls_binding_tests
            tests/qtest/SystemMediaControlsBindingTests.cpp
        )
        target_link_libraries(m0_qt_system_media_controls_binding_tests PRIVATE
            m0_qt_shell_support m0_qt_shell_ui Qt6::Test
        )
        target_include_directories(m0_qt_system_media_controls_binding_tests PRIVATE src)
        target_compile_features(m0_qt_system_media_controls_binding_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_system_media_controls_binding_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_system_media_controls_binding_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_system_media_controls_binding_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_system_media_controls_binding_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror
            )
        endif()
        add_test(NAME m0.qt.system-media-controls.binding COMMAND m0_qt_system_media_controls_binding_tests)
        add_executable(m0_qt_system_power_events_binding_tests
            tests/qtest/SystemPowerEventsBindingTests.cpp
        )
        target_link_libraries(m0_qt_system_power_events_binding_tests PRIVATE
            m0_qt_shell_support m0_qt_shell_ui Qt6::Test
        )
        target_include_directories(m0_qt_system_power_events_binding_tests PRIVATE src)
        target_compile_features(m0_qt_system_power_events_binding_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_system_power_events_binding_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_system_power_events_binding_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_system_power_events_binding_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_system_power_events_binding_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror
            )
        endif()
        add_test(NAME m0.qt.system-power-events.binding COMMAND m0_qt_system_power_events_binding_tests)
        add_executable(m0_qt_system_appearance_tests tests/qtest/QtSystemAppearanceTests.cpp)
        target_link_libraries(m0_qt_system_appearance_tests PRIVATE m0_qt_system_appearance Qt6::Test)
        target_compile_features(m0_qt_system_appearance_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_system_appearance_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_system_appearance_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_system_appearance_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.system-appearance COMMAND m0_qt_system_appearance_tests
            -o "${CMAKE_CURRENT_BINARY_DIR}/system-appearance-test.txt,txt"
            -o -,txt
        )
        add_executable(m0_qt_preferences_json_codec_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/PreferencesJsonCodecTests.cpp")
        target_link_libraries(m0_qt_preferences_json_codec_tests PRIVATE m0_qt_preferences_persistence Qt6::Test)
        target_include_directories(m0_qt_preferences_json_codec_tests PRIVATE src)
        target_compile_features(m0_qt_preferences_json_codec_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_preferences_json_codec_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_preferences_json_codec_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_preferences_json_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.preferences.json-codec COMMAND m0_qt_preferences_json_codec_tests)
        add_executable(m0_qt_session_json_codec_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/SessionJsonCodecTests.cpp")
        target_link_libraries(m0_qt_session_json_codec_tests PRIVATE m0_qt_session_persistence Qt6::Test)
        target_include_directories(m0_qt_session_json_codec_tests PRIVATE src)
        target_compile_features(m0_qt_session_json_codec_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_session_json_codec_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_session_json_codec_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_session_json_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.session.json-codec COMMAND m0_qt_session_json_codec_tests)
        if(WIN32)
            add_executable(m0_qt_preferences_windows_store_tests
                tests/qtest/QtJsonWindowsPreferencesStoreTests.cpp
            )
            target_link_libraries(m0_qt_preferences_windows_store_tests PRIVATE
                m0_qt_preferences_persistence
                Qt6::Test
            )
            target_include_directories(m0_qt_preferences_windows_store_tests PRIVATE src)
            target_compile_features(m0_qt_preferences_windows_store_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_preferences_windows_store_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_preferences_windows_store_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_preferences_windows_store_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror
                )
            endif()
            add_test(NAME m0.qt.preferences.windows-store COMMAND m0_qt_preferences_windows_store_tests)
            add_executable(m0_qt_session_windows_store_tests
                tests/qtest/QtJsonWindowsSessionStoreTests.cpp
            )
            target_link_libraries(m0_qt_session_windows_store_tests PRIVATE
                m0_qt_session_persistence
                Qt6::Test
            )
            target_include_directories(m0_qt_session_windows_store_tests PRIVATE src)
            target_compile_features(m0_qt_session_windows_store_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_session_windows_store_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_session_windows_store_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_session_windows_store_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror
                )
            endif()
            add_test(NAME m0.qt.session.windows-store COMMAND m0_qt_session_windows_store_tests)
            add_executable(m0_qt_diagnostic_privacy_tests tests/qtest/QtDiagnosticPrivacyTests.cpp)
            target_link_libraries(m0_qt_diagnostic_privacy_tests PRIVATE m0_qt_diagnostic_privacy Qt6::Test)
            target_compile_features(m0_qt_diagnostic_privacy_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_diagnostic_privacy_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_diagnostic_privacy_tests PRIVATE /W4 /WX /permissive-)
            endif()
            add_test(NAME m0.qt.diagnostics.privacy COMMAND m0_qt_diagnostic_privacy_tests)
            set_tests_properties(m0.qt.diagnostics.privacy PROPERTIES TIMEOUT 30)
            add_executable(m0_qt_diagnostic_store_tests tests/qtest/QtWindowsDiagnosticLogStoreTests.cpp)
            target_link_libraries(m0_qt_diagnostic_store_tests PRIVATE m0_qt_diagnostic_store Qt6::Test)
            target_compile_features(m0_qt_diagnostic_store_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_diagnostic_store_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_diagnostic_store_tests PRIVATE /W4 /WX /permissive-)
            endif()
            add_test(NAME m0.qt.diagnostics.windows-store COMMAND m0_qt_diagnostic_store_tests)
            set_tests_properties(m0.qt.diagnostics.windows-store PROPERTIES TIMEOUT 30)
        endif()
        add_executable(m0_qt_shell_launch_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/ShellLaunchOptionsTests.cpp")
        target_link_libraries(m0_qt_shell_launch_tests PRIVATE m0_qt_shell_bootstrap Qt6::Test)
        target_compile_features(m0_qt_shell_launch_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_shell_launch_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_launch_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_launch_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.shell-launch-options COMMAND m0_qt_shell_launch_tests)
        add_executable(m0_qt_open_sources_command_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/OpenSourcesCommandTests.cpp")
        target_link_libraries(m0_qt_open_sources_command_tests PRIVATE m0_qt_shell_bootstrap Qt6::Test)
        target_compile_features(m0_qt_open_sources_command_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_open_sources_command_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_open_sources_command_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_open_sources_command_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.open-sources.command COMMAND m0_qt_open_sources_command_tests)
        add_executable(m0_qt_open_sources_dispatcher_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/OpenSourcesDispatcherTests.cpp")
        target_link_libraries(m0_qt_open_sources_dispatcher_tests PRIVATE m0_qt_shell_bootstrap Qt6::Test)
        target_compile_features(m0_qt_open_sources_dispatcher_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_open_sources_dispatcher_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_open_sources_dispatcher_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_open_sources_dispatcher_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.open-sources.dispatcher COMMAND m0_qt_open_sources_dispatcher_tests)
        add_executable(m0_qt_single_instance_coordinator_tests
            "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtSingleInstanceCoordinatorTests.cpp"
        )
        target_link_libraries(m0_qt_single_instance_coordinator_tests PRIVATE
            m0_qt_shell_bootstrap
            Qt6::Network
            Qt6::Test
        )
        target_compile_features(m0_qt_single_instance_coordinator_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_single_instance_coordinator_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_single_instance_coordinator_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_single_instance_coordinator_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror
            )
        endif()
        add_test(NAME m0.qt.single-instance.coordinator COMMAND m0_qt_single_instance_coordinator_tests
            -o -,txt
        )
        set_tests_properties(m0.qt.single-instance.coordinator PROPERTIES
            LABELS "m0;qt;ipc;single-instance;FR-004;FR-033;NFR-LIMIT-004;NFR-REL-004"
            TIMEOUT 30
        )
        if(WIN32)
            add_executable(m0_qt_preferences_restart_tests tests/qtest/PreferencesRestartTests.cpp)
            target_link_libraries(m0_qt_preferences_restart_tests PRIVATE
                m0_qt_preferences_persistence
                m0_qt_session_persistence
                Qt6::Core
                Qt6::Test
            )
            target_include_directories(m0_qt_preferences_restart_tests PRIVATE src)
            target_compile_features(m0_qt_preferences_restart_tests PRIVATE cxx_std_20)
            set_target_properties(m0_qt_preferences_restart_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_preferences_restart_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_preferences_restart_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror
                )
            endif()
            add_dependencies(m0_qt_preferences_restart_tests m0_qt_shell)
            if(M0_QT_AUDIO_SUPPORT)
                add_executable(m0_qt_diagnostic_executable_tests tests/qtest/DiagnosticExecutableTests.cpp)
                target_link_libraries(m0_qt_diagnostic_executable_tests PRIVATE m0_qt_preferences_persistence Qt6::Test)
                target_compile_features(m0_qt_diagnostic_executable_tests PRIVATE cxx_std_20)
                set_target_properties(m0_qt_diagnostic_executable_tests PROPERTIES AUTOMOC ON)
                if(MSVC)
                    target_compile_options(m0_qt_diagnostic_executable_tests PRIVATE /W4 /WX /permissive-)
                endif()
                add_dependencies(m0_qt_diagnostic_executable_tests m0_qt_shell)
                add_test(NAME m0.qt.diagnostics.executable COMMAND m0_qt_diagnostic_executable_tests
                    $<TARGET_FILE:m0_qt_shell>)
                set_tests_properties(m0.qt.diagnostics.executable PROPERTIES TIMEOUT 45)
            endif()
            add_test(NAME m0.qt.preferences.restart COMMAND m0_qt_preferences_restart_tests
                $<TARGET_FILE:m0_qt_shell>
            )
        endif()
        add_executable(m0_qt_shell_executable_smoke_tests tests/qtest/ShellExecutableSmokeTests.cpp)
        target_link_libraries(m0_qt_shell_executable_smoke_tests PRIVATE Qt6::Core Qt6::Test)
        target_compile_features(m0_qt_shell_executable_smoke_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_shell_executable_smoke_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_executable_smoke_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_executable_smoke_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror
            )
        endif()
        add_dependencies(m0_qt_shell_executable_smoke_tests m0_qt_shell)
        add_executable(m0_qt_fixture_tests
            "${RC_MUSIC_SOURCE_ROOT}/tests/ui/qtest/FixtureRepositoryTests.cpp"
        )
        target_link_libraries(m0_qt_fixture_tests PRIVATE m0_qt_shell_support Qt6::Gui Qt6::Test)
        target_include_directories(m0_qt_fixture_tests PRIVATE src)
        target_compile_features(m0_qt_fixture_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_fixture_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_fixture_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_fixture_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_fixture_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.fixture.repository-and-model COMMAND m0_qt_fixture_tests)
        add_executable(m0_qt_presenter_tests "${RC_MUSIC_SOURCE_ROOT}/tests/ui/qtest/PlayerPresenterTests.cpp")
        target_link_libraries(m0_qt_presenter_tests PRIVATE
            m0_qt_shell_support m0_qt_shell_ui Qt6::Gui Qt6::Quick Qt6::Test)
        target_include_directories(m0_qt_presenter_tests PRIVATE src)
        target_compile_features(m0_qt_presenter_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_presenter_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_presenter_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_presenter_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_presenter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.presenter.initial-load COMMAND m0_qt_presenter_tests)
        add_executable(m0_qt_shell_contract_tests "${RC_MUSIC_SOURCE_ROOT}/tests/ui/qtest/ShellUiContractTests.cpp")
        target_link_libraries(m0_qt_shell_contract_tests PRIVATE
            m0_qt_shell_support
            m0_qt_shell_ui
            m0_qt_shell_uiplugin
            m0_qt_fixture_data
            Qt6::Gui
            Qt6::Test
        )
        target_include_directories(m0_qt_shell_contract_tests PRIVATE src)
        target_compile_definitions(m0_qt_shell_contract_tests PRIVATE
            M0_FIXTURE_SHA256="${m0_expected_fixture_sha256}"
            M0_FIXTURE_SIZE=${m0_expected_fixture_size}
            M0_QUEUE_SHA256="${m0_expected_queue_sha256}"
            M0_QUEUE_SIZE=${m0_expected_queue_size}
            M0_PLACEHOLDER_SHA256="${m0_expected_placeholder_sha256}"
            M0_PLACEHOLDER_SIZE=${m0_expected_placeholder_size}
            M0_TRACK_SHA256="${m0_expected_track_sha256}"
            M0_TRACK_SIZE=${m0_expected_track_size}
            M0_QML_SOURCE_DIR="${RC_MUSIC_SOURCE_ROOT}/src/ui/qml"
        )
        set_target_properties(m0_qt_shell_contract_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_contract_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.shell-ui-contract COMMAND m0_qt_shell_contract_tests)
        if(WIN32)
            add_executable(m0_qt_windows_executable_identity_tests tests/qtest/WindowsExecutableIdentityTests.cpp)
            target_link_libraries(m0_qt_windows_executable_identity_tests PRIVATE
                Qt6::Core Qt6::Test user32 version)
            set_target_properties(m0_qt_windows_executable_identity_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_windows_executable_identity_tests PRIVATE /W4 /WX /permissive-)
            endif()
            add_dependencies(m0_qt_windows_executable_identity_tests m0_qt_shell)
            add_test(NAME m0.qt.windows.executable-identity
                COMMAND m0_qt_windows_executable_identity_tests "$<TARGET_FILE:m0_qt_shell>")
            set_tests_properties(m0.qt.windows.executable-identity PROPERTIES
                LABELS "m0;qt;windows;identity"
                TIMEOUT 20
                ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        endif()
        add_executable(m0_qt_quick_tests "${RC_MUSIC_SOURCE_ROOT}/tests/ui/quick/quick_main.cpp")
        target_link_libraries(m0_qt_quick_tests PRIVATE
            m0_qt_system_appearance
            m0_qt_shell_support
            m0_qt_shell_ui
            m0_qt_shell_uiplugin
            m0_qt_fixture_data
            Qt6::QuickControls2
            Qt6::QuickTest
        )
        target_include_directories(m0_qt_quick_tests PRIVATE src)
        target_compile_features(m0_qt_quick_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_quick_tests PRIVATE
            QUICK_TEST_SOURCE_DIR="${RC_MUSIC_SOURCE_ROOT}/tests/ui/quick"
        )
        set_target_properties(m0_qt_quick_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_quick_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_quick_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.quick.shell-states COMMAND m0_qt_quick_tests
            -input "${RC_MUSIC_SOURCE_ROOT}/tests/ui/quick"
            -import "${QT_QML_OUTPUT_DIRECTORY}"
        )
        get_target_property(m0_qt_core_runtime Qt6::Core IMPORTED_LOCATION_RELEASE)
        get_filename_component(m0_qt_runtime_dir "${m0_qt_core_runtime}" DIRECTORY)
        set_tests_properties(m0.qt.diagnostics.binding PROPERTIES
            LABELS "m0;qt;diagnostics;NFR-PRIV-003"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}")
        if(WIN32)
            set_tests_properties(m0.qt.diagnostics.privacy m0.qt.diagnostics.windows-store PROPERTIES
                LABELS "m0;qt;diagnostics;NFR-PRIV-003"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}")
        endif()
        set_tests_properties(m0.qt.fixture.repository-and-model PROPERTIES
            LABELS "m0;qt;fixture;model"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.shell-launch-options PROPERTIES
            LABELS "m0;qt;shell-launch"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.open-sources.command PROPERTIES
            LABELS "m0;qt;ipc;open-sources;FR-004;FR-033;NFR-LIMIT-004;NFR-REL-004"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.open-sources.dispatcher PROPERTIES
            LABELS "m0;qt;ipc;open-sources;dispatcher;FR-004;FR-033;NFR-LIMIT-004;NFR-REL-004"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.single-instance.coordinator PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.preferences.json-codec PROPERTIES
            LABELS "m0;qt;preferences;json"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.session.json-codec PROPERTIES
            LABELS "m0;qt;session;json"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        if(WIN32)
            set_tests_properties(m0.qt.preferences.windows-store PROPERTIES
                LABELS "m0;qt;preferences;windows;atomic-store"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            )
            set_tests_properties(m0.qt.session.windows-store PROPERTIES
                LABELS "m0;qt;session;windows;atomic-store;multiprocess;FR-044;NFR-REL-003"
                TIMEOUT 30
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            )
        endif()
        set_tests_properties(m0.qt.presenter.initial-load PROPERTIES
            LABELS "m0;qt;presenter;FR-038;M4.7"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.system-power-events.binding PROPERTIES
            LABELS "m0;qt;power;suspend;resume;binding;FR-038;M4.7"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set(m0_qt_quick_test_environment
            "QT_QPA_PLATFORM=offscreen"
            "QSG_RHI_BACKEND=software"
            "QT_QUICK_CONTROLS_STYLE=Basic"
            "QT_LOGGING_TO_CONSOLE=1"
            "QT_FORCE_STDERR_LOGGING=1"
            "QT_FATAL_WARNINGS=1"
        )
        if(WIN32 AND IS_DIRECTORY "$ENV{WINDIR}/Fonts")
            cmake_path(CONVERT "$ENV{WINDIR}/Fonts" TO_CMAKE_PATH_LIST m0_windows_font_dir NORMALIZE)
            list(APPEND m0_qt_quick_test_environment "QT_QPA_FONTDIR=${m0_windows_font_dir}")
        endif()
        set_tests_properties(m0.qt.audio-file-locator m0.qt.audio-files-picker PROPERTIES
            LABELS "m0;qt;files;locator;UX-008;M4.7"
            TIMEOUT 30
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            # QFileDialog's offscreen widget has one expected capability warning. QtTest fails
            # every other warning; fatal warnings would abort even the explicitly ignored one.
            ENVIRONMENT "${m0_qt_quick_test_environment};QT_FATAL_WARNINGS=0"
        )
        set_tests_properties(m0.qt.audio-files-picker PROPERTIES LABELS "m0;qt;files;picker;FR-001;M2.1")
        if(WIN32)
            set_tests_properties(m0.qt.windows.smtc PROPERTIES
                LABELS "m0;qt;windows;smtc;media-keys;FR-037"
                TIMEOUT 15
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT "${m0_qt_quick_test_environment}"
            )
            set_tests_properties(m0.qt.windows.smtc-native PROPERTIES
                LABELS "m0;qt;windows;smtc;native;FR-037"
                TIMEOUT 15
                RUN_SERIAL TRUE
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT
                    "QT_QPA_PLATFORM=windows;QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
            )
            set_tests_properties(m0.qt.windows.power-events PROPERTIES
                LABELS "m0;qt;windows;power;suspend;resume;FR-038;M4.7"
                TIMEOUT 15
                RUN_SERIAL TRUE
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT
                    "QT_QPA_PLATFORM=windows;QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
            )
            set_tests_properties(m0.qt.windows.taskbar-events PROPERTIES
                LABELS "m0;qt;windows;tray;native;FR-038;M4.7"
                TIMEOUT 15
                RUN_SERIAL TRUE
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT
                    "QT_QPA_PLATFORM=windows;QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
            )
            set_tests_properties(m0.qt.windows.tray-recovery PROPERTIES
                LABELS "m0;qt;windows;tray;native;FR-038;M4.7"
                TIMEOUT 30
                RUN_SERIAL TRUE
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT
                    "QT_QPA_PLATFORM=windows;QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
            )
            set_tests_properties(m0.qt.preferences.restart PROPERTIES
                LABELS "m0;qt;preferences;restart;shell-executable"
                TIMEOUT 40
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT "${m0_qt_quick_test_environment}"
            )
        endif()
        set_tests_properties(m0.qt.system-appearance PROPERTIES
            LABELS "m0;qt;system-appearance"
            TIMEOUT 10
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.shell-platform PROPERTIES
            LABELS "m0;qt;shell-platform;tray"
            TIMEOUT 15
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.shell-platform-binding PROPERTIES
            LABELS "m0;qt;shell-platform;tray;lifecycle"
            TIMEOUT 10
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.system-media-controls.binding PROPERTIES
            LABELS "m0;qt;system-media-controls;binding;FR-037"
            TIMEOUT 15
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.system-power-events.binding PROPERTIES
            TIMEOUT 15
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.fixture.repository-and-model m0.qt.presenter.initial-load PROPERTIES
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        if(NOT M0_QT_AUDIO_SUPPORT)
            add_test(NAME m0.qt.shell-executable.error-recovery COMMAND m0_qt_shell_executable_smoke_tests
                $<TARGET_FILE:m0_qt_shell>
                error-recovery
                unavailable
            )
            set_tests_properties(m0.qt.shell-executable.error-recovery PROPERTIES
                LABELS "m0;qt;shell-executable;error-recovery"
                TIMEOUT 25
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT "${m0_qt_quick_test_environment}"
            )
            add_test(NAME m0.qt.shell-executable.tray-lifecycle COMMAND m0_qt_shell_executable_smoke_tests
                $<TARGET_FILE:m0_qt_shell>
                tray-lifecycle
                empty
            )
            set_tests_properties(m0.qt.shell-executable.tray-lifecycle PROPERTIES
                LABELS "m0;qt;shell-executable;tray;lifecycle"
                TIMEOUT 25
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
                ENVIRONMENT "${m0_qt_quick_test_environment}"
            )
        endif()
        set_tests_properties(m0.qt.quick.shell-states PROPERTIES
            LABELS "m0;qt;quick;shell-states"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.shell-ui-contract PROPERTIES
            LABELS "m0;qt;shell-ui-contract"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
    endif()
endif()

if(M0_QT_AUDIO_SUPPORT)
    set(M0_AUDIO_CORPUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/audio-corpus")
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_ROOT)
    set(M0_AUDIO_CORPUS_WAV_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/wav-pcm-mono.wav")
    set(M0_AUDIO_CORPUS_MP3_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/mp3-cbr-stereo.mp3")
    set(M0_AUDIO_CORPUS_MP3_VBR_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/mp3-vbr-stereo.mp3")
    set(M0_AUDIO_CORPUS_MP3_UNICODE_FILE
        "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/mp3-unicode-tags.mp3")
    set(M0_AUDIO_CORPUS_M4A_AAC_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/m4a-aac-lc.m4a")
    set(M0_AUDIO_CORPUS_FLAC_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/flac-16-stereo.flac")
    set(M0_AUDIO_CORPUS_OGG_VORBIS_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/valid/ogg-vorbis.ogg")
    set(M0_AUDIO_CORPUS_OPUS_CANDIDATE_FILE
        "${M0_AUDIO_CORPUS_ROOT}/fixtures/candidate/opus-candidate.opus")
    set(M0_AUDIO_CORPUS_ALAC_CANDIDATE_FILE
        "${M0_AUDIO_CORPUS_ROOT}/fixtures/candidate/m4a-alac-candidate.m4a")
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_WAV_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_MP3_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_MP3_VBR_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_MP3_UNICODE_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_M4A_AAC_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_FLAC_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_OGG_VORBIS_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_OPUS_CANDIDATE_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_ALAC_CANDIDATE_FILE)
    set(M0_AUDIO_CORPUS_EMPTY_FILE "${M0_AUDIO_CORPUS_ROOT}/fixtures/hostile/empty.bin")
    set(M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE
        "${M0_AUDIO_CORPUS_ROOT}/fixtures/hostile/truncated-wav-pcm-mono.wav")
    set(M0_AUDIO_CORPUS_WRONG_EXTENSION_FILE
        "${M0_AUDIO_CORPUS_ROOT}/fixtures/edge/wrong-extension-wav-as-mp3.mp3")
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_EMPTY_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE)
    cmake_path(NORMAL_PATH M0_AUDIO_CORPUS_WRONG_EXTENSION_FILE)
    foreach(m0_audio_fixture IN ITEMS
            M0_AUDIO_CORPUS_WAV_FILE
            M0_AUDIO_CORPUS_MP3_FILE
            M0_AUDIO_CORPUS_MP3_VBR_FILE
            M0_AUDIO_CORPUS_MP3_UNICODE_FILE
            M0_AUDIO_CORPUS_M4A_AAC_FILE
            M0_AUDIO_CORPUS_FLAC_FILE
            M0_AUDIO_CORPUS_OGG_VORBIS_FILE
            M0_AUDIO_CORPUS_OPUS_CANDIDATE_FILE
            M0_AUDIO_CORPUS_ALAC_CANDIDATE_FILE
            M0_AUDIO_CORPUS_EMPTY_FILE
            M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE
            M0_AUDIO_CORPUS_WRONG_EXTENSION_FILE)
        if(NOT EXISTS "${${m0_audio_fixture}}" OR IS_DIRECTORY "${${m0_audio_fixture}}")
            message(FATAL_ERROR "The committed audio corpus fixture ${m0_audio_fixture} does not exist")
        endif()
    endforeach()
    file(SHA256 "${M0_AUDIO_CORPUS_WAV_FILE}" m0_audio_corpus_wav_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_WAV_FILE}" m0_audio_corpus_wav_size)
    if(NOT m0_audio_corpus_wav_sha256 STREQUAL
            "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f" OR
       NOT m0_audio_corpus_wav_size EQUAL 96044)
        message(FATAL_ERROR "The committed WAV audio corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_MP3_FILE}" m0_audio_corpus_mp3_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_MP3_FILE}" m0_audio_corpus_mp3_size)
    if(NOT m0_audio_corpus_mp3_sha256 STREQUAL
            "b7d0f99cf733c7458dbec3ab62ad397cd98cc6b069db5c12cbb9834885e66b62" OR
       NOT m0_audio_corpus_mp3_size EQUAL 33318)
        message(FATAL_ERROR "The committed MP3 audio corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_MP3_VBR_FILE}" m0_audio_corpus_mp3_vbr_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_MP3_VBR_FILE}" m0_audio_corpus_mp3_vbr_size)
    if(NOT m0_audio_corpus_mp3_vbr_sha256 STREQUAL
            "74a5c55e50a8a73198525878f1a0463de918b0490910a5f7681bb933c31b29c9" OR
       NOT m0_audio_corpus_mp3_vbr_size EQUAL 14406)
        message(FATAL_ERROR "The committed VBR MP3 corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_MP3_UNICODE_FILE}" m0_audio_corpus_mp3_unicode_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_MP3_UNICODE_FILE}" m0_audio_corpus_mp3_unicode_size)
    if(NOT m0_audio_corpus_mp3_unicode_sha256 STREQUAL
            "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000" OR
       NOT m0_audio_corpus_mp3_unicode_size EQUAL 24966)
        message(FATAL_ERROR "The committed Unicode MP3 corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_M4A_AAC_FILE}" m0_audio_corpus_m4a_aac_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_M4A_AAC_FILE}" m0_audio_corpus_m4a_aac_size)
    if(NOT m0_audio_corpus_m4a_aac_sha256 STREQUAL
            "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558" OR
       NOT m0_audio_corpus_m4a_aac_size EQUAL 33400)
        message(FATAL_ERROR "The committed M4A AAC corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_FLAC_FILE}" m0_audio_corpus_flac_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_FLAC_FILE}" m0_audio_corpus_flac_size)
    if(NOT m0_audio_corpus_flac_sha256 STREQUAL
            "103577f9c6b37eea391b86f9a085dc7fb9b0a26571aa907b9d37e47d0b543a1c" OR
       NOT m0_audio_corpus_flac_size EQUAL 60531)
        message(FATAL_ERROR "The committed FLAC corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_OGG_VORBIS_FILE}" m0_audio_corpus_ogg_vorbis_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_OGG_VORBIS_FILE}" m0_audio_corpus_ogg_vorbis_size)
    if(NOT m0_audio_corpus_ogg_vorbis_sha256 STREQUAL
            "abab4db40eb8380974e1c6ea69eb4c24bfd8eb2803b67b79e2a1a49523a7096f" OR
       NOT m0_audio_corpus_ogg_vorbis_size EQUAL 12605)
        message(FATAL_ERROR "The committed Ogg Vorbis corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_OPUS_CANDIDATE_FILE}" m0_audio_corpus_opus_candidate_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_OPUS_CANDIDATE_FILE}" m0_audio_corpus_opus_candidate_size)
    if(NOT m0_audio_corpus_opus_candidate_sha256 STREQUAL
            "341e25361371f851d85d34af950fb10279db36fbd67d91b324a9c08e40791e15" OR
       NOT m0_audio_corpus_opus_candidate_size EQUAL 35706)
        message(FATAL_ERROR "The committed Opus candidate fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_ALAC_CANDIDATE_FILE}" m0_audio_corpus_alac_candidate_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_ALAC_CANDIDATE_FILE}" m0_audio_corpus_alac_candidate_size)
    if(NOT m0_audio_corpus_alac_candidate_sha256 STREQUAL
            "5d729fbe90a0d98a081d1fe590b24021a8001871f516529d0f8d8392109ad339" OR
       NOT m0_audio_corpus_alac_candidate_size EQUAL 78938)
        message(FATAL_ERROR "The committed ALAC candidate fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_EMPTY_FILE}" m0_audio_corpus_empty_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_EMPTY_FILE}" m0_audio_corpus_empty_size)
    if(NOT m0_audio_corpus_empty_sha256 STREQUAL
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" OR
       NOT m0_audio_corpus_empty_size EQUAL 0)
        message(FATAL_ERROR "The committed empty audio corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE}" m0_audio_corpus_truncated_wav_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE}" m0_audio_corpus_truncated_wav_size)
    if(NOT m0_audio_corpus_truncated_wav_sha256 STREQUAL
            "58751e20c4e8403194736fd0f1be0769a09441e73c4a2e0858265465a23e7bff" OR
       NOT m0_audio_corpus_truncated_wav_size EQUAL 40)
        message(FATAL_ERROR
            "The committed truncated WAV audio corpus fixture does not match its authenticated identity")
    endif()
    file(SHA256 "${M0_AUDIO_CORPUS_WRONG_EXTENSION_FILE}" m0_audio_corpus_wrong_extension_sha256)
    file(SIZE "${M0_AUDIO_CORPUS_WRONG_EXTENSION_FILE}" m0_audio_corpus_wrong_extension_size)
    if(NOT m0_audio_corpus_wrong_extension_sha256 STREQUAL
            "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f" OR
       NOT m0_audio_corpus_wrong_extension_size EQUAL 96044)
        message(FATAL_ERROR
            "The committed wrong-extension audio corpus fixture does not match its authenticated identity")
    endif()

    if(BUILD_TESTING)
        find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)
        add_executable(m0_qt_audio_adapter_tests "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtMultimediaPlaybackBackendTests.cpp")
        target_link_libraries(m0_qt_audio_adapter_tests PRIVATE m0_qt_audio_adapters Qt6::Test)
        target_compile_features(m0_qt_audio_adapter_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_audio_adapter_tests PRIVATE
            M0_TEST_AUDIO_WAV_FILE="${M0_AUDIO_CORPUS_WAV_FILE}"
            M0_TEST_AUDIO_MP3_FILE="${M0_AUDIO_CORPUS_MP3_FILE}"
            M0_TEST_AUDIO_MP3_VBR_FILE="${M0_AUDIO_CORPUS_MP3_VBR_FILE}"
            M0_TEST_AUDIO_MP3_UNICODE_FILE="${M0_AUDIO_CORPUS_MP3_UNICODE_FILE}"
            M0_TEST_AUDIO_M4A_AAC_FILE="${M0_AUDIO_CORPUS_M4A_AAC_FILE}"
            M0_TEST_AUDIO_FLAC_FILE="${M0_AUDIO_CORPUS_FLAC_FILE}"
            M0_TEST_AUDIO_OGG_VORBIS_FILE="${M0_AUDIO_CORPUS_OGG_VORBIS_FILE}"
            M0_TEST_AUDIO_EMPTY_FILE="${M0_AUDIO_CORPUS_EMPTY_FILE}"
            M0_TEST_AUDIO_TRUNCATED_WAV_FILE="${M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE}"
            M0_TEST_AUDIO_WRONG_EXTENSION_FILE="${M0_AUDIO_CORPUS_WRONG_EXTENSION_FILE}"
        )
        set_target_properties(m0_qt_audio_adapter_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_audio_adapter_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_audio_adapter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.audio.playback-lifecycle COMMAND m0_qt_audio_adapter_tests)
        get_target_property(m0_qt_audio_runtime Qt6::Multimedia IMPORTED_LOCATION_RELEASE)
        get_target_property(m0_qt_audio_core_runtime Qt6::Core IMPORTED_LOCATION_RELEASE)
        get_target_property(m0_qt_audio_gui_runtime Qt6::Gui IMPORTED_LOCATION_RELEASE)
        get_filename_component(m0_qt_audio_runtime_dir "${m0_qt_audio_runtime}" DIRECTORY)
        get_filename_component(m0_qt_audio_core_runtime_dir "${m0_qt_audio_core_runtime}" DIRECTORY)
        get_filename_component(m0_qt_audio_gui_runtime_dir "${m0_qt_audio_gui_runtime}" DIRECTORY)
        get_filename_component(m0_qt_audio_prefix "${m0_qt_audio_runtime_dir}" DIRECTORY)
        set_tests_properties(m0.qt.audio.playback-lifecycle PROPERTIES
            LABELS "m0;qt;audio;PB-001;PB-002;PB-003;PB-004;PB-006;PB-007;PB-008;PB-009;PB-010;PB-011;PB-012;PB-101;PB-102;PB-103"
            TIMEOUT 120
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${m0_qt_audio_runtime_dir};PATH=path_list_prepend:${m0_qt_audio_core_runtime_dir}"
            ENVIRONMENT
                "QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${m0_qt_audio_prefix}/plugins"
        )

        if(M0_QT_SHELL_SUPPORT)
            if(WIN32)
                set_tests_properties(m0.qt.preferences.restart m0.qt.diagnostics.executable PROPERTIES
                    ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:${m0_qt_audio_runtime_dir};PATH=path_list_prepend:${m0_qt_audio_core_runtime_dir}"
                    ENVIRONMENT
                        "${m0_qt_quick_test_environment};QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${m0_qt_audio_prefix}/plugins"
                )
                add_executable(m0_qt_session_restart_tests tests/qtest/SessionRestartTests.cpp)
                target_link_libraries(m0_qt_session_restart_tests PRIVATE
                    m0_qt_session_persistence
                    Qt6::Core
                    Qt6::Test
                )
                target_include_directories(m0_qt_session_restart_tests PRIVATE src)
                target_compile_features(m0_qt_session_restart_tests PRIVATE cxx_std_20)
                set_target_properties(m0_qt_session_restart_tests PROPERTIES AUTOMOC ON)
                if(MSVC)
                    target_compile_options(m0_qt_session_restart_tests PRIVATE /W4 /WX /permissive-)
                else()
                    target_compile_options(m0_qt_session_restart_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
                endif()
                add_dependencies(m0_qt_session_restart_tests m0_qt_shell)
                add_test(NAME m0.qt.session.restart COMMAND m0_qt_session_restart_tests
                    $<TARGET_FILE:m0_qt_shell>
                    "${M0_AUDIO_CORPUS_WAV_FILE}"
                )
                set_tests_properties(m0.qt.session.restart PROPERTIES
                    LABELS "m0;qt;audio;session;restart;forced-termination;corruption;replace-failure;FR-043;FR-044;NFR-REL-003"
                    TIMEOUT 120
                    ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:${m0_qt_audio_runtime_dir};PATH=path_list_prepend:${m0_qt_audio_core_runtime_dir}"
                    ENVIRONMENT
                        "${m0_qt_quick_test_environment};QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${m0_qt_audio_prefix}/plugins"
                )
                add_executable(m0_qt_single_instance_executable_tests
                    tests/qtest/SingleInstanceExecutableTests.cpp
                )
                target_link_libraries(m0_qt_single_instance_executable_tests PRIVATE
                    m0_qt_shell_bootstrap
                    Qt6::Network
                    Qt6::Test
                )
                target_compile_features(m0_qt_single_instance_executable_tests PRIVATE cxx_std_20)
                set_target_properties(m0_qt_single_instance_executable_tests PROPERTIES AUTOMOC ON)
                if(MSVC)
                    target_compile_options(m0_qt_single_instance_executable_tests PRIVATE /W4 /WX /permissive-)
                else()
                    target_compile_options(m0_qt_single_instance_executable_tests PRIVATE
                        -Wall -Wextra -Wpedantic -Werror
                    )
                endif()
                add_dependencies(m0_qt_single_instance_executable_tests m0_qt_shell)
                add_test(NAME m0.qt.single-instance.executable COMMAND m0_qt_single_instance_executable_tests
                    $<TARGET_FILE:m0_qt_shell>
                    "${M0_AUDIO_CORPUS_WAV_FILE}"
                )
                set_tests_properties(m0.qt.single-instance.executable PROPERTIES
                    LABELS "m0;qt;audio;single-instance;ipc;FR-004;FR-033;NFR-LIMIT-004;NFR-REL-004"
                    TIMEOUT 45
                    ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:${m0_qt_audio_runtime_dir};PATH=path_list_prepend:${m0_qt_audio_core_runtime_dir}"
                    ENVIRONMENT
                        "${m0_qt_quick_test_environment};QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${m0_qt_audio_prefix}/plugins"
                )
            endif()
            add_test(NAME m0.qt.shell-executable.combined-startup COMMAND m0_qt_shell_executable_smoke_tests
                $<TARGET_FILE:m0_qt_shell>
                combined-startup
                empty
            )
            set_tests_properties(m0.qt.shell-executable.combined-startup PROPERTIES
                LABELS "m0;qt;shell-executable;audio;startup;lifecycle"
                TIMEOUT 25
                ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:${m0_qt_audio_runtime_dir};PATH=path_list_prepend:${m0_qt_audio_core_runtime_dir}"
                ENVIRONMENT
                    "${m0_qt_quick_test_environment};QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${m0_qt_audio_prefix}/plugins"
            )
            add_executable(m0_qt_local_audio_integration_tests
                "${RC_MUSIC_SOURCE_ROOT}/tests/qt/LocalAudioIntegrationTests.cpp"
            )
            target_link_libraries(m0_qt_local_audio_integration_tests PRIVATE
                m0_qt_shell_support
                m0_qt_shell_ui
                m0_qt_audio_adapters
                m0_taglib_metadata_extractor
                Qt6::Gui
                Qt6::Test
                Qt6::Widgets
                TagLib::TagLib
            )
            target_include_directories(m0_qt_local_audio_integration_tests PRIVATE src)
            target_compile_features(m0_qt_local_audio_integration_tests PRIVATE cxx_std_20)
            target_compile_definitions(m0_qt_local_audio_integration_tests PRIVATE
                M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
                M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
                M0_TEST_AUDIO_WAV_FILE="${M0_AUDIO_CORPUS_WAV_FILE}"
                M0_TEST_AUDIO_MP3_FILE="${M0_AUDIO_CORPUS_MP3_FILE}"
                M0_TEST_AUDIO_MP3_VBR_FILE="${M0_AUDIO_CORPUS_MP3_VBR_FILE}"
                M0_TEST_AUDIO_MP3_UNICODE_FILE="${M0_AUDIO_CORPUS_MP3_UNICODE_FILE}"
                M0_TEST_AUDIO_M4A_AAC_FILE="${M0_AUDIO_CORPUS_M4A_AAC_FILE}"
                M0_TEST_AUDIO_FLAC_FILE="${M0_AUDIO_CORPUS_FLAC_FILE}"
                M0_TEST_AUDIO_OGG_VORBIS_FILE="${M0_AUDIO_CORPUS_OGG_VORBIS_FILE}"
                M0_TEST_AUDIO_TRUNCATED_WAV_FILE="${M0_AUDIO_CORPUS_TRUNCATED_WAV_FILE}"
            )
            set_target_properties(m0_qt_local_audio_integration_tests PROPERTIES AUTOMOC ON)
            if(MSVC)
                target_compile_options(m0_qt_local_audio_integration_tests PRIVATE /W4 /WX /permissive-)
            else()
                target_compile_options(m0_qt_local_audio_integration_tests PRIVATE
                    -Wall -Wextra -Wpedantic -Werror
                )
            endif()
            add_custom_command(TARGET m0_qt_local_audio_integration_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:TagLib::tag>"
                    "$<TARGET_FILE_DIR:m0_qt_local_audio_integration_tests>"
            )
            add_test(NAME m0.qt.audio.local-integration COMMAND m0_qt_local_audio_integration_tests)
            set_tests_properties(m0.qt.audio.local-integration PROPERTIES
                LABELS "m0;qt;audio;integration;local"
                TIMEOUT 180
                ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:${m0_qt_audio_runtime_dir};PATH=path_list_prepend:${m0_qt_audio_core_runtime_dir}"
                ENVIRONMENT
                    "QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${m0_qt_audio_prefix}/plugins;QT_QPA_PLATFORM=offscreen"
            )
        endif()
    endif()
endif()
if(BUILD_TESTING)
    include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicTests.cmake")
    rc_music_prepare_catch2("${M0_CATCH2_SOURCE_DIR}")
    rc_music_add_core_tests(m0_qt_core_tests "m0.qt.core." "m0;qt;core")

    if(M0_QT_AUDIO_SUPPORT)
        add_executable(m0_taglib_metadata_extractor_tests
            "${RC_MUSIC_SOURCE_ROOT}/tests/audio/TagLibMetadataExtractorTests.cpp"
        )
        target_link_libraries(m0_taglib_metadata_extractor_tests PRIVATE
            m0_qt_core
            m0_taglib_metadata_extractor
            TagLib::TagLib
            Catch2::Catch2WithMain
            Qt6::Gui
        )
        target_compile_features(m0_taglib_metadata_extractor_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_taglib_metadata_extractor_tests PRIVATE
            M0_TEST_AUDIO_CORPUS_ROOT="${M0_AUDIO_CORPUS_ROOT}"
        )
        if(MSVC)
            target_compile_options(m0_taglib_metadata_extractor_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_taglib_metadata_extractor_tests PRIVATE
                -Wall -Wextra -Wpedantic -Werror
            )
        endif()
        add_custom_command(TARGET m0_taglib_metadata_extractor_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:TagLib::tag>"
                "$<TARGET_FILE_DIR:m0_taglib_metadata_extractor_tests>"
        )
        catch_discover_tests(
            m0_taglib_metadata_extractor_tests
            TEST_PREFIX "m0.qt.metadata."
            PROPERTIES LABELS "m0;qt;audio;metadata;taglib"
            DL_PATHS "${m0_qt_audio_runtime_dir}" "${m0_qt_audio_gui_runtime_dir}"
        )
    endif()
endif()
