# Process journeys exercise the same entry point through isolated test-only
# compositions. The actual product executable must never acquire these hooks.
if(NOT BUILD_TESTING OR NOT RC_MUSIC_BUILD_DESKTOP OR NOT WIN32)
    message(FATAL_ERROR "Process journeys require the Windows desktop test configuration")
endif()
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 QuickLayouts Svg Widgets Multimedia Test)
find_package(TagLib 2.3.1 EXACT CONFIG REQUIRED)
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
endif()
add_dependencies(rc_music_shell_executable_smoke_tests rc_music_fixture_test_shell)

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
