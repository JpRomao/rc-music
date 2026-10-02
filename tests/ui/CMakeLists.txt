# This directory owns a separate QML module with the same public URI. Never
# link the scenario variant into the real application (including testing ON).
if(NOT BUILD_TESTING OR NOT RC_MUSIC_BUILD_DESKTOP)
    message(FATAL_ERROR "UI scenarios require the product desktop test configuration")
endif()
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 QuickLayouts QuickTest Test LinguistTools)
qt_standard_project_setup(REQUIRES 6.11)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")

include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicUiFixtures.cmake")
set(ui_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rc_music_validate_ui_fixtures("${ui_fixtures}")

add_library(rc_music_ui_scenario_support STATIC
    support/include/m0/adapters/fixture/FixtureRepository.hpp
    support/include/m0/presentation/QueueListModel.hpp
    support/adapters/fixture/FixtureDecoder.hpp
    support/adapters/fixture/FixtureRepository.cpp
    support/presentation/QueueListModel.cpp
    support/presentation/PlayerPresenterScenarioControl.hpp
    support/presentation/PlayerPresenterTestAccess.hpp)
target_include_directories(rc_music_ui_scenario_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/support/include" "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(rc_music_ui_scenario_support PUBLIC
    m0_qt_shell_support rc_music_test_support Qt6::Core Qt6::Gui)
target_compile_definitions(rc_music_ui_scenario_support
    PUBLIC RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    PRIVATE M0_FROZEN_FIXTURE_SHA256="${RC_MUSIC_UI_FIXTURE_SHA256}"
        M0_FROZEN_QUEUE_SHA256="${RC_MUSIC_UI_QUEUE_SHA256}")
rc_music_configure_cpp(rc_music_ui_scenario_support)

include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicUi.cmake")
rc_music_add_ui(rc_music_test_ui rc_music_ui_scenario_support "${QT_QML_OUTPUT_DIRECTORY}")
# PlayerPresenter's layout changes with this macro: propagate it to every
# consumer of this variant, but never to the shared core or real UI target.
target_compile_definitions(rc_music_test_ui PUBLIC RC_MUSIC_ENABLE_FIXTURE_SCENARIOS)

add_library(m0_qt_fixture_data STATIC EXCLUDE_FROM_ALL)
target_link_libraries(m0_qt_fixture_data PRIVATE Qt6::Core)
set_target_properties(m0_qt_fixture_data PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_source_files_properties("${ui_fixtures}/fixture.v1.json" PROPERTIES QT_RESOURCE_ALIAS "data/fixture.v1.json")
set_source_files_properties("${ui_fixtures}/queue.v1.json" PROPERTIES QT_RESOURCE_ALIAS "data/queue.v1.json")
qt_add_resources(m0_qt_fixture_data m0_qt_fixture_data PREFIX "/m0"
    FILES "${ui_fixtures}/fixture.v1.json" "${ui_fixtures}/queue.v1.json")

set(ui_environment "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1;QT_FATAL_WARNINGS=1")
if(WIN32 AND IS_DIRECTORY "$ENV{WINDIR}/Fonts")
    cmake_path(CONVERT "$ENV{WINDIR}/Fonts" TO_CMAKE_PATH_LIST ui_font_dir NORMALIZE)
    list(APPEND ui_environment "QT_QPA_FONTDIR=${ui_font_dir}")
endif()

function(add_ui_test target name source)
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE rc_music_ui_scenario_support Qt6::Test ${ARGN})
    rc_music_configure_cpp(${target})
    add_test(NAME ${name} COMMAND ${target} -o -,txt)
    set_tests_properties(${name} PROPERTIES LABELS "ui;qt" TIMEOUT 60
        ENVIRONMENT "${ui_environment}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()

add_ui_test(rc_music_ui_fixture_tests rc_music.ui.fixture-repository
    qtest/FixtureRepositoryTests.cpp Qt6::Gui)
add_ui_test(rc_music_ui_presenter_tests rc_music.ui.presenter
    qtest/PlayerPresenterTests.cpp rc_music_test_ui Qt6::Gui Qt6::Quick)
if(TARGET rc_music_downloads)
    add_ui_test(rc_music_ui_download_import_tests rc_music.ui.download-import
        qtest/PlayerDownloadImportTests.cpp rc_music_test_ui rc_music_downloads m0_qt_session_persistence Qt6::Gui Qt6::Quick)
    target_compile_definitions(rc_music_ui_download_import_tests PRIVATE
        M0_TEST_FIXTURE_FILE="${ui_fixtures}/fixture.v1.json"
        M0_TEST_QUEUE_FILE="${ui_fixtures}/queue.v1.json")
    set_tests_properties(rc_music.ui.download-import PROPERTIES LABELS "ui;qt;downloads;FR-051;FR-053")
endif()
if(TARGET rc_music_browser_integration AND TARGET rc_music_downloads)
    add_ui_test(rc_music_ui_download_binding_tests rc_music.ui.download-binding
        qtest/DownloadApplicationBindingTests.cpp rc_music_test_ui rc_music_browser_integration rc_music_downloads)
    target_compile_definitions(rc_music_ui_download_binding_tests PRIVATE
        M0_TEST_FIXTURE_FILE="${ui_fixtures}/fixture.v1.json"
        M0_TEST_QUEUE_FILE="${ui_fixtures}/queue.v1.json")
    set_tests_properties(rc_music.ui.download-binding PROPERTIES LABELS "ui;qt;downloads;lifecycle;FR-051;FR-053")
endif()
if(WIN32 AND TARGET rc_music_download_documents AND TARGET rc_music_download_persistence AND TARGET rc_music_browser_integration)
    add_ui_test(rc_music_ui_download_recovery_review_tests rc_music.ui.download-recovery-review
        qtest/DownloadRecoveryReviewCompositionTests.cpp rc_music_test_ui rc_music_test_uiplugin
        rc_music_download_documents rc_music_download_persistence Qt6::Qml Qt6::Quick)
    # Seed owned synthetic files through the real workspace, without making
    # network/conversion execution a prerequisite of this read-only review.
    target_sources(rc_music_ui_download_recovery_review_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsDownloadWorkspace.cpp")
    target_link_libraries(rc_music_ui_download_recovery_review_tests PRIVATE advapi32)
    set_tests_properties(rc_music.ui.download-recovery-review PROPERTIES
        LABELS "ui;qt;downloads;persistence;windows;FR-051;FR-053")
endif()
foreach(target IN ITEMS rc_music_ui_fixture_tests rc_music_ui_presenter_tests)
    target_compile_definitions(${target} PRIVATE
        M0_TEST_FIXTURE_FILE="${ui_fixtures}/fixture.v1.json"
        M0_TEST_QUEUE_FILE="${ui_fixtures}/queue.v1.json")
endforeach()

add_ui_test(rc_music_ui_contract_tests rc_music.ui.contract
    qtest/ShellUiContractTests.cpp rc_music_test_ui rc_music_test_uiplugin m0_qt_fixture_data Qt6::Gui)
foreach(input IN ITEMS FIXTURE QUEUE PLACEHOLDER TRACK)
    target_compile_definitions(rc_music_ui_contract_tests PRIVATE
        M0_${input}_SHA256="${RC_MUSIC_UI_${input}_SHA256}"
        M0_${input}_SIZE=${RC_MUSIC_UI_${input}_SIZE})
endforeach()
target_compile_definitions(rc_music_ui_contract_tests PRIVATE
    M0_QML_SOURCE_DIR="${RC_MUSIC_SOURCE_ROOT}/src/ui/qml")

add_executable(rc_music_ui_quick_tests quick/quick_main.cpp)
target_link_libraries(rc_music_ui_quick_tests PRIVATE
    rc_music_ui_scenario_support rc_music_test_ui rc_music_test_uiplugin
    m0_qt_fixture_data m0_qt_system_appearance Qt6::QuickControls2 Qt6::QuickTest)
target_compile_definitions(rc_music_ui_quick_tests PRIVATE RC_MUSIC_TEST_UI_VARIANT
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/quick")
rc_music_configure_cpp(rc_music_ui_quick_tests)
add_test(NAME rc_music.ui.quick COMMAND rc_music_ui_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/quick" -import "${QT_QML_OUTPUT_DIRECTORY}" -o -,txt)
set_tests_properties(rc_music.ui.quick PROPERTIES LABELS "ui;qt;quick" TIMEOUT 90
    ENVIRONMENT "${ui_environment}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_ui_test(rc_music_ui_shell_platform_binding_tests rc_music.ui.shell-platform-binding
    qtest/ShellPlatformBindingTests.cpp rc_music_test_ui m0_qt_shell_platform Qt6::Widgets)
add_ui_test(rc_music_ui_system_media_controls_binding_tests rc_music.ui.system-media-controls-binding
    qtest/SystemMediaControlsBindingTests.cpp rc_music_test_ui)
add_ui_test(rc_music_ui_system_power_events_binding_tests rc_music.ui.system-power-events-binding
    qtest/SystemPowerEventsBindingTests.cpp rc_music_test_ui)
add_ui_test(rc_music_ui_diagnostic_binding_tests rc_music.ui.diagnostic-binding
    qtest/ShellDiagnosticBindingTests.cpp rc_music_test_ui)
# The production diagnostic library brings the production presenter ABI with it.
# Compile its same source into this test consumer under the scenario ABI instead.
target_sources(rc_music_ui_diagnostic_binding_tests PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ShellDiagnosticBinding.cpp")
foreach(target IN ITEMS rc_music_ui_shell_platform_binding_tests
        rc_music_ui_system_media_controls_binding_tests rc_music_ui_system_power_events_binding_tests
        rc_music_ui_diagnostic_binding_tests)
    target_compile_definitions(${target} PRIVATE
        M0_TEST_FIXTURE_FILE="${ui_fixtures}/fixture.v1.json"
        M0_TEST_QUEUE_FILE="${ui_fixtures}/queue.v1.json")
endforeach()
set_tests_properties(rc_music.ui.shell-platform-binding PROPERTIES
    LABELS "windows;ui;qt;binding;tray;lifecycle;FR-030;FR-031;FR-038" TIMEOUT 10)
set_tests_properties(rc_music.ui.system-media-controls-binding PROPERTIES
    LABELS "windows;ui;qt;binding;system-media-controls;FR-037" TIMEOUT 15)
set_tests_properties(rc_music.ui.system-power-events-binding PROPERTIES
    LABELS "windows;ui;qt;binding;power;FR-038" TIMEOUT 15)
set_tests_properties(rc_music.ui.diagnostic-binding PROPERTIES
    LABELS "windows;ui;qt;binding;diagnostics;NFR-PRIV-003" TIMEOUT 30)

add_test(NAME rc_music.ui.build-boundary COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
    -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/UiBuildBoundaryTests.cmake")
set_tests_properties(rc_music.ui.build-boundary PROPERTIES LABELS "ui;boundary" TIMEOUT 15)
