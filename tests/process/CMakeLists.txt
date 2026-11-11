# Process journeys exercise the same entry point through isolated test-only
# compositions. The actual product executable must never acquire these hooks.
if(NOT BUILD_TESTING OR NOT RC_MUSIC_BUILD_DESKTOP OR NOT WIN32)
    message(FATAL_ERROR "Process journeys require the Windows desktop test configuration")
endif()
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 QuickLayouts Svg Widgets Multimedia Test)
find_package(TagLib 2.3.2 EXACT CONFIG REQUIRED)
qt_standard_project_setup(REQUIRES 6.11)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/ui/qml")
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicShell.cmake")

set(M0_WINDOWS_ICON_FILE "${CMAKE_BINARY_DIR}/rc-music.ico")
set(process_resource "${CMAKE_CURRENT_BINARY_DIR}/WindowsResources.rc")
configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/WindowsResources.rc.in" "${process_resource}" @ONLY)
set_source_files_properties("${process_resource}" PROPERTIES OBJECT_DEPENDS "${M0_WINDOWS_ICON_FILE}")

foreach(target IN ITEMS rc_music_test_shell rc_music_fixture_test_shell)
    qt_add_executable(${target}
        "${RC_MUSIC_SOURCE_ROOT}/src/app/main.cpp"
        # The production diagnostic library links the other presenter ABI.
        # Compile the same binding source with this variant, never both UIs.
        "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ShellDiagnosticBinding.cpp"
        "${process_resource}")
    add_dependencies(${target} m0_qt_windows_icon)
    rc_music_link_shell(${target} rc_music_test_ui rc_music_ui_scenario_support)
    target_compile_definitions(${target} PRIVATE M0_QT_SHELL_TEST_HOOKS RC_MUSIC_TEST_UI_VARIANT)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
endforeach()
rc_music_link_shell_audio(rc_music_test_shell)
target_link_libraries(rc_music_fixture_test_shell PRIVATE m0_qt_fixture_data)

function(add_process_driver target source)
    add_executable(${target} "qtest/${source}.cpp")
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Test ${ARGN})
    rc_music_configure_cpp(${target})
    add_dependencies(${target} rc_music_test_shell)
endfunction()

add_process_driver(rc_music_preferences_restart_tests PreferencesRestartTests
    m0_qt_preferences_persistence m0_qt_session_persistence)
add_process_driver(rc_music_session_restart_tests SessionRestartTests
    m0_qt_session_persistence m0_qt_preferences_persistence)
add_process_driver(rc_music_single_instance_executable_tests SingleInstanceExecutableTests
    m0_qt_shell_bootstrap Qt6::Network)
add_process_driver(rc_music_diagnostic_executable_tests DiagnosticExecutableTests m0_qt_preferences_persistence)
add_process_driver(rc_music_shell_executable_smoke_tests ShellExecutableSmokeTests)
if(TARGET rc_music_download_persistence AND TARGET rc_music_browser_integration)
    target_sources(rc_music_test_shell PRIVATE support/DownloadsShellSmoke.cpp support/DownloadsShellSmoke.hpp)
    target_link_libraries(rc_music_test_shell PRIVATE rc_music_download_persistence)
    target_compile_definitions(rc_music_test_shell PRIVATE RC_MUSIC_DOWNLOAD_SMOKE_AVAILABLE)
    add_process_driver(rc_music_downloads_composition_tests DownloadsCompositionTests
        m0_qt_session_persistence rc_music_download_persistence)
    add_test(NAME rc_music.process.downloads-composition COMMAND rc_music_downloads_composition_tests
        "$<TARGET_FILE:rc_music_test_shell>"
        "${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid/mp3-cbr-stereo.mp3")
    if(TARGET rc_music_native_messaging)
        target_link_libraries(rc_music_test_shell PRIVATE rc_music_native_messaging)
        target_include_directories(rc_music_test_shell PRIVATE "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}")
        target_compile_definitions(rc_music_test_shell PRIVATE RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE)
        add_process_driver(rc_music_native_downloads_composition_tests NativeDownloadsCompositionTests
            m0_qt_session_persistence rc_music_download_persistence rc_music_native_messaging)
        target_include_directories(rc_music_native_downloads_composition_tests PRIVATE "${RC_MUSIC_NATIVE_TEST_CONFIG_DIRECTORY}")
        add_dependencies(rc_music_native_downloads_composition_tests rc_music_native_host_test)
        add_test(NAME rc_music.process.native-downloads-composition COMMAND rc_music_native_downloads_composition_tests
            "$<TARGET_FILE:rc_music_test_shell>" "$<TARGET_FILE:rc_music_native_host_test>"
            "${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid/mp3-cbr-stereo.mp3")
    endif()
endif()
add_dependencies(rc_music_shell_executable_smoke_tests rc_music_fixture_test_shell)

if(TARGET rc_music_player_downloads AND TARGET rc_music_native_messaging AND
   TARGET rc_music_download_io_helper AND TARGET rc_music_media_foundation_worker AND
   RC_MUSIC_DOWNLOAD_TEST_CRT_DIR AND RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN)
    # Build-time fixture generation only; Python is never a player dependency.
    find_package(Python3 3.13 REQUIRED COMPONENTS Interpreter)
    add_executable(rc_music_download_component_probe_tests
        qtest/PlayerDownloadComponentProbeTests.cpp
        "${RC_MUSIC_SOURCE_ROOT}/src/app/PlayerDownloadComponentProbe.cpp")
    target_link_libraries(rc_music_download_component_probe_tests PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(rc_music_download_component_probe_tests PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src")
    rc_music_configure_cpp(rc_music_download_component_probe_tests)
    set_target_properties(rc_music_download_component_probe_tests PROPERTIES AUTOMOC ON)
    add_test(NAME rc_music.process.download-component-probe COMMAND rc_music_download_component_probe_tests -o -,txt)
    set_tests_properties(rc_music.process.download-component-probe PROPERTIES TIMEOUT 20
        LABELS "windows;desktop;qt;downloads;bootstrap"
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_configured_downloads_profile_tests
        qtest/ConfiguredDownloadsProfileTests.cpp
        "${RC_MUSIC_SOURCE_ROOT}/src/app/PlayerDownloadsBuildProfile.cpp")
    target_link_libraries(rc_music_configured_downloads_profile_tests PRIVATE rc_music_player_downloads Qt6::Test)
    target_compile_definitions(rc_music_configured_downloads_profile_tests PRIVATE
        RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsContractProfile.hpp"
        RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsContractProfile.hpp")
    rc_music_configure_cpp(rc_music_configured_downloads_profile_tests)
    set_target_properties(rc_music_configured_downloads_profile_tests PROPERTIES AUTOMOC ON)
    add_test(NAME rc_music.process.configured-downloads-profile COMMAND rc_music_configured_downloads_profile_tests -o -,txt)
    set_tests_properties(rc_music.process.configured-downloads-profile PROPERTIES TIMEOUT 15
        LABELS "windows;desktop;qt;downloads;bootstrap"
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_configured_downloads_profile_off_tests
        qtest/ConfiguredDownloadsProfileTests.cpp
        "${RC_MUSIC_SOURCE_ROOT}/src/app/PlayerDownloadsBuildProfile.cpp")
    target_link_libraries(rc_music_configured_downloads_profile_off_tests PRIVATE rc_music_player_downloads Qt6::Test)
    target_compile_definitions(rc_music_configured_downloads_profile_off_tests PRIVATE
        RC_MUSIC_TEST_NATIVE_PROFILE_OFF
        RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsContractProfile.hpp")
    rc_music_configure_cpp(rc_music_configured_downloads_profile_off_tests)
    set_target_properties(rc_music_configured_downloads_profile_off_tests PROPERTIES AUTOMOC ON)
    add_test(NAME rc_music.process.configured-downloads-profile-off COMMAND rc_music_configured_downloads_profile_off_tests -o -,txt)
    set_tests_properties(rc_music.process.configured-downloads-profile-off PROPERTIES TIMEOUT 15
        LABELS "windows;desktop;qt;downloads;bootstrap"
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    # Same main, production presenter ABI and real owner; no legacy smoke
    # composition, command flag or replacement executor enters these targets.
    string(SHA256 configured_downloads_identity "${CMAKE_BINARY_DIR}")
    string(SUBSTRING "${configured_downloads_identity}" 0 16 configured_downloads_identity)
    set(native_main_origin "chrome-extension://dddddddddddddddddddddddddddddddd/")
    set(native_main_endpoint "rc-music-configured-downloads-${configured_downloads_identity}")
    set(native_main_owner_images "QStringLiteral(\"rc_music_configured_downloads_shell.exe\")")
    set(native_main_host_images "${native_main_owner_images}")
    # A direct-only generated package must opt out of the compiled YouTube
    # channel, so its absence cannot block the principal integration.
    set(native_youtube_origin "chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/")
    set(native_youtube_endpoint "rc-music-configured-youtube-${configured_downloads_identity}")
    set(native_youtube_owner_images "${native_main_owner_images}")
    set(native_youtube_host_images "${native_main_host_images}")
    set(configured_downloads_native_profile "${CMAKE_CURRENT_BINARY_DIR}/configured-downloads/NativeHostBuildProfiles.hpp")
    configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildProfiles.hpp.in"
        "${configured_downloads_native_profile}" @ONLY)
    set(configured_downloads_profile "${CMAKE_CURRENT_BINARY_DIR}/configured-downloads/ConfiguredDownloadsFixtureProfile.hpp")
    set(configured_downloads_template "${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsFixtureProfile.hpp.in")
    add_custom_command(OUTPUT "${configured_downloads_profile}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${configured_downloads_profile}" "-DTEMPLATE=${configured_downloads_template}"
            "-DPAYLOAD_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
            "-DPROFILE_PYTHON=${Python3_EXECUTABLE}"
            "-DPROFILE_RECIPE=${RC_MUSIC_SOURCE_ROOT}/tools/download-runtime/build_profile.py"
            "-DBOOTSTRAP=$<TARGET_FILE:rc_music_download_io_helper>"
            "-DPAYLOAD=$<TARGET_FILE:rc_music_download_network_payload>"
            "-DCORE=$<TARGET_FILE:Qt6::Core>" "-DNETWORK=$<TARGET_FILE:Qt6::Network>"
            "-DCRT_DIR=${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}" "-DPLUGIN=${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}"
            "-DMEDIA=$<TARGET_FILE:rc_music_media_foundation_worker>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsProfile.cmake"
        DEPENDS rc_music_download_io_helper rc_music_download_network_payload rc_music_media_foundation_worker
            Qt6::Core Qt6::Network "${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}"
            "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}/msvcp140.dll" "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}/msvcp140_1.dll"
            "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}/vcruntime140.dll" "${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}/vcruntime140_1.dll"
            "${configured_downloads_template}" "${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsProfile.cmake"
            "${RC_MUSIC_SOURCE_ROOT}/tools/download-runtime/build_profile.py"
            "${RC_MUSIC_SOURCE_ROOT}/tools/youtube-resolver/onedir_plan.py"
        BYPRODUCTS
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/installed-v1"
            "${CMAKE_CURRENT_BINARY_DIR}/configured-downloads/PackagedDownloadsFixtureProfile.hpp"
            "${CMAKE_CURRENT_BINARY_DIR}/configured-downloads/package-profile-config.json"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/RCMusicDownloadWorker.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/rcmusic-download-network.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/Qt6Core.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/Qt6Network.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/msvcp140.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/msvcp140_1.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/vcruntime140.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/vcruntime140_1.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/network/qrcmusicschanneloffline.dll"
            "${CMAKE_CURRENT_BINARY_DIR}/download-tools/media/RCMusicMediaWorker.exe"
        VERBATIM)
    set_source_files_properties("${configured_downloads_profile}" PROPERTIES GENERATED TRUE)
    add_custom_target(rc_music_configured_downloads_profile DEPENDS "${configured_downloads_profile}")
    foreach(mode IN ITEMS configured none off)
        set(target rc_music_${mode}_downloads_shell)
        qt_add_executable(${target}
            "${RC_MUSIC_SOURCE_ROOT}/src/app/main.cpp"
            support/ConfiguredDownloadsObservation.cpp "${process_resource}")
        add_dependencies(${target} m0_qt_windows_icon)
        rc_music_link_shell(${target} m0_qt_shell_ui m0_qt_shell_support m0_qt_diagnostic_binding)
        rc_music_link_shell_audio(${target})
        target_link_libraries(${target} PRIVATE rc_music_player_downloads rc_music_native_messaging)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
        target_compile_definitions(${target} PRIVATE M0_QT_SHELL_TEST_HOOKS
            RC_MUSIC_CONFIGURED_DOWNLOADS_EXPECT_BINDING=$<IF:$<STREQUAL:${mode},configured>,1,0>)
        if(mode STREQUAL "configured")
            add_dependencies(${target} rc_music_configured_downloads_profile)
            rc_music_configure_player_downloads(${target} "${configured_downloads_profile}"
                NATIVE_PROFILE_HEADER "${configured_downloads_native_profile}")
        elseif(mode STREQUAL "none")
            rc_music_configure_player_downloads(${target}
                "${CMAKE_CURRENT_SOURCE_DIR}/support/ConfiguredDownloadsNoneProfile.hpp"
                NATIVE_PROFILE_HEADER "${configured_downloads_native_profile}")
        endif()
    endforeach()
    add_executable(rc_music_configured_downloads_bootstrap_tests qtest/ConfiguredDownloadsBootstrapTests.cpp)
    target_link_libraries(rc_music_configured_downloads_bootstrap_tests PRIVATE
        m0_qt_preferences_persistence m0_qt_session_persistence rc_music_download_persistence
        rc_music_download_network Qt6::Test Advapi32)
    rc_music_configure_cpp(rc_music_configured_downloads_bootstrap_tests)
    set_target_properties(rc_music_configured_downloads_bootstrap_tests PROPERTIES AUTOMOC ON)
    add_dependencies(rc_music_configured_downloads_bootstrap_tests
        rc_music_configured_downloads_shell rc_music_none_downloads_shell rc_music_off_downloads_shell)
    add_test(NAME rc_music.process.configured-downloads-bootstrap COMMAND rc_music_configured_downloads_bootstrap_tests
        "$<TARGET_FILE:rc_music_configured_downloads_shell>"
        "$<TARGET_FILE:rc_music_none_downloads_shell>" "$<TARGET_FILE:rc_music_off_downloads_shell>")
endif()

# The corpus was authenticated by RcMusicDesktopTests before this directory.
set(process_wav "${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid/wav-pcm-mono.wav")
add_test(NAME rc_music.process.preferences-restart COMMAND rc_music_preferences_restart_tests
    "$<TARGET_FILE:rc_music_test_shell>")
add_test(NAME rc_music.process.session-restart COMMAND rc_music_session_restart_tests
    "$<TARGET_FILE:rc_music_test_shell>" "${process_wav}")
add_test(NAME rc_music.process.single-instance COMMAND rc_music_single_instance_executable_tests
    "$<TARGET_FILE:rc_music_test_shell>" "${process_wav}")
add_test(NAME rc_music.process.diagnostics COMMAND rc_music_diagnostic_executable_tests
    "$<TARGET_FILE:rc_music_test_shell>")
add_test(NAME rc_music.process.combined-startup COMMAND rc_music_shell_executable_smoke_tests
    "$<TARGET_FILE:rc_music_test_shell>" combined-startup empty)
add_test(NAME rc_music.process.error-recovery COMMAND rc_music_shell_executable_smoke_tests
    "$<TARGET_FILE:rc_music_fixture_test_shell>" error-recovery unavailable)
add_test(NAME rc_music.process.tray-lifecycle COMMAND rc_music_shell_executable_smoke_tests
    "$<TARGET_FILE:rc_music_fixture_test_shell>" tray-lifecycle empty)

set(process_environment "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1")
if(IS_DIRECTORY "$ENV{WINDIR}/Fonts")
    cmake_path(CONVERT "$ENV{WINDIR}/Fonts" TO_CMAKE_PATH_LIST process_font_dir NORMALIZE)
    list(APPEND process_environment "QT_QPA_FONTDIR=${process_font_dir}")
endif()
get_target_property(process_audio_runtime Qt6::Multimedia IMPORTED_LOCATION_RELEASE)
get_filename_component(process_audio_runtime_dir "${process_audio_runtime}" DIRECTORY)
get_filename_component(process_audio_prefix "${process_audio_runtime_dir}" DIRECTORY)

set(process_combined_tests
    rc_music.process.preferences-restart rc_music.process.session-restart
    rc_music.process.single-instance rc_music.process.diagnostics rc_music.process.combined-startup)
if(TARGET rc_music_downloads_composition_tests)
    list(APPEND process_combined_tests rc_music.process.downloads-composition)
endif()
if(TARGET rc_music_native_downloads_composition_tests)
    list(APPEND process_combined_tests rc_music.process.native-downloads-composition)
endif()
if(TARGET rc_music_configured_downloads_bootstrap_tests)
    list(APPEND process_combined_tests rc_music.process.configured-downloads-bootstrap)
endif()
set(process_fixture_tests rc_music.process.error-recovery rc_music.process.tray-lifecycle)
set_tests_properties(${process_combined_tests} ${process_fixture_tests} PROPERTIES
    LABELS "windows;desktop;qt;process" TIMEOUT 25)
set_tests_properties(${process_combined_tests} PROPERTIES
    ENVIRONMENT "${process_environment};QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${process_audio_prefix}/plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${process_audio_runtime_dir};PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
set_tests_properties(${process_fixture_tests} PROPERTIES
    ENVIRONMENT "${process_environment}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
set_tests_properties(rc_music.process.preferences-restart PROPERTIES TIMEOUT 40)
set_tests_properties(rc_music.process.session-restart PROPERTIES TIMEOUT 120)
if(TARGET rc_music_downloads_composition_tests)
    set_tests_properties(rc_music.process.downloads-composition PROPERTIES TIMEOUT 60)
    set_property(TEST rc_music.process.downloads-composition APPEND PROPERTY LABELS "downloads;session;FR-051;FR-053")
endif()
if(TARGET rc_music_native_downloads_composition_tests)
    set_tests_properties(rc_music.process.native-downloads-composition PROPERTIES TIMEOUT 150
        RESOURCE_LOCK "rc_music_native_host_test_endpoint")
    set_property(TEST rc_music.process.native-downloads-composition APPEND PROPERTY LABELS "native;downloads;session;FR-051;FR-053")
endif()
if(TARGET rc_music_configured_downloads_bootstrap_tests)
    set_tests_properties(rc_music.process.configured-downloads-bootstrap PROPERTIES TIMEOUT 120
        RESOURCE_LOCK "rc_music_configured_downloads_endpoint")
    set_property(TEST rc_music.process.configured-downloads-bootstrap APPEND PROPERTY LABELS
        "native;downloads;session;bootstrap;FR-051;FR-053")
endif()
set_tests_properties(rc_music.process.single-instance rc_music.process.diagnostics PROPERTIES TIMEOUT 45)
set_property(TEST rc_music.process.preferences-restart APPEND PROPERTY LABELS "preferences;restart;FR-042")
set_property(TEST rc_music.process.session-restart APPEND PROPERTY LABELS
    "audio;session;restart;forced-termination;corruption;replace-failure;FR-043;FR-044;NFR-REL-003")
set_property(TEST rc_music.process.single-instance APPEND PROPERTY LABELS
    "audio;single-instance;ipc;FR-004;FR-033;NFR-LIMIT-004;NFR-REL-004")
set_property(TEST rc_music.process.diagnostics APPEND PROPERTY LABELS "audio;diagnostics;NFR-PRIV-003")
set_property(TEST rc_music.process.combined-startup APPEND PROPERTY LABELS "audio;startup;lifecycle")
set_property(TEST rc_music.process.error-recovery APPEND PROPERTY LABELS "error-recovery")
set_property(TEST rc_music.process.tray-lifecycle APPEND PROPERTY LABELS "tray;lifecycle")

add_test(NAME rc_music.process.build-boundary COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
    -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/ProcessBuildBoundaryTests.cmake")
set_tests_properties(rc_music.process.build-boundary PROPERTIES LABELS "process;boundary" TIMEOUT 15)
