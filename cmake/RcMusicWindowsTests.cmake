include_guard(GLOBAL)

# Daily Windows coverage uses offscreen/fake OS boundaries, temporary stores and
# read-only PE inspection. Interactive/native shell smokes are separate gates.
function(rc_music_add_windows_tests)
    if(NOT WIN32 OR NOT BUILD_TESTING OR NOT RC_MUSIC_BUILD_DESKTOP)
        message(FATAL_ERROR "Windows integration tests require the product desktop test configuration")
    endif()

    _rc_music_add_desktop_test(rc_music_windows_shell_platform_tests
        rc_music.windows.shell-platform QtShellPlatformTests.cpp m0_qt_shell_platform Qt6::Widgets)
    _rc_music_add_desktop_test(rc_music_windows_system_appearance_tests
        rc_music.windows.system-appearance QtSystemAppearanceTests.cpp m0_qt_system_appearance)
    _rc_music_add_desktop_test(rc_music_windows_smtc_tests
        rc_music.windows.smtc-adapter WindowsSmtcAdapterTests.cpp m0_qt_windows_smtc)
    _rc_music_add_desktop_test(rc_music_windows_preferences_store_tests
        rc_music.windows.preferences-store QtJsonWindowsPreferencesStoreTests.cpp m0_qt_preferences_persistence)
    _rc_music_add_desktop_test(rc_music_windows_session_store_tests
        rc_music.windows.session-store QtJsonWindowsSessionStoreTests.cpp m0_qt_session_persistence)
    _rc_music_add_desktop_test(rc_music_windows_diagnostic_privacy_tests
        rc_music.windows.diagnostic-privacy QtDiagnosticPrivacyTests.cpp m0_qt_diagnostic_privacy)
    _rc_music_add_desktop_test(rc_music_windows_diagnostic_store_tests
        rc_music.windows.diagnostic-store QtWindowsDiagnosticLogStoreTests.cpp m0_qt_diagnostic_store)

    set_property(TEST rc_music.windows.shell-platform rc_music.windows.system-appearance
        rc_music.windows.smtc-adapter APPEND PROPERTY ENVIRONMENT
        "QSG_RHI_BACKEND=software" "QT_QUICK_CONTROLS_STYLE=Basic" "QT_FATAL_WARNINGS=1")
    set_tests_properties(rc_music.windows.shell-platform PROPERTIES
        LABELS "windows;desktop;qt;tray;FR-030;FR-031;FR-035;FR-038" TIMEOUT 15)
    set_tests_properties(rc_music.windows.system-appearance PROPERTIES
        LABELS "windows;desktop;qt;appearance;FR-040" TIMEOUT 10)
    set_tests_properties(rc_music.windows.smtc-adapter PROPERTIES
        LABELS "windows;desktop;qt;smtc;FR-037" TIMEOUT 15)
    set_tests_properties(rc_music.windows.preferences-store PROPERTIES
        LABELS "windows;desktop;qt;preferences;atomic-store;FR-042;FR-044")
    set_tests_properties(rc_music.windows.session-store PROPERTIES
        LABELS "windows;desktop;qt;session;atomic-store;multiprocess;FR-043;FR-044;NFR-REL-003")
    set_tests_properties(rc_music.windows.diagnostic-privacy rc_music.windows.diagnostic-store PROPERTIES
        LABELS "windows;desktop;qt;diagnostics;NFR-PRIV-003")

    add_executable(rc_music_windows_executable_identity_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsExecutableIdentityTests.cpp")
    target_link_libraries(rc_music_windows_executable_identity_tests PRIVATE Qt6::Core Qt6::Test user32 version)
    rc_music_configure_cpp(rc_music_windows_executable_identity_tests)
    set_target_properties(rc_music_windows_executable_identity_tests PROPERTIES AUTOMOC ON)
    add_dependencies(rc_music_windows_executable_identity_tests m0_qt_shell)
    add_test(NAME rc_music.windows.executable-identity COMMAND rc_music_windows_executable_identity_tests
        "$<TARGET_FILE:m0_qt_shell>" -o -,txt)
    set_tests_properties(rc_music.windows.executable-identity PROPERTIES
        LABELS "windows;desktop;qt;identity" TIMEOUT 20
        ENVIRONMENT "${rc_music_desktop_test_environment}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()
