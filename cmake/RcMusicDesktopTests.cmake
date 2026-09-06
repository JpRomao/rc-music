include_guard(GLOBAL)

function(_rc_music_add_desktop_test target test_name source)
    add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${source}")
    target_include_directories(${target} PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/include"
        "${RC_MUSIC_SOURCE_ROOT}/src"
        "${RC_MUSIC_SOURCE_ROOT}/src/app"
    )
    target_link_libraries(${target} PRIVATE ${ARGN} Qt6::Test)
    rc_music_configure_cpp(${target})
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    add_test(NAME ${test_name} COMMAND ${target} -o -,txt)
    set_tests_properties(${test_name} PROPERTIES
        LABELS "desktop;qt"
        TIMEOUT 30
        ENVIRONMENT "${rc_music_desktop_test_environment}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endfunction()

function(rc_music_add_desktop_tests)
    if(NOT BUILD_TESTING)
        return()
    endif()
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test Network Widgets)

    set(rc_music_desktop_test_environment
        "QT_QPA_PLATFORM=offscreen"
        "QT_LOGGING_TO_CONSOLE=1"
        "QT_FORCE_STDERR_LOGGING=1"
    )
    if(WIN32 AND IS_DIRECTORY "$ENV{WINDIR}/Fonts")
        cmake_path(CONVERT "$ENV{WINDIR}/Fonts" TO_CMAKE_PATH_LIST rc_music_windows_font_dir NORMALIZE)
        list(APPEND rc_music_desktop_test_environment "QT_QPA_FONTDIR=${rc_music_windows_font_dir}")
    endif()

    _rc_music_add_desktop_test(rc_music_shell_launch_tests
        rc_music.desktop.shell-launch-options ShellLaunchOptionsTests.cpp m0_qt_shell_bootstrap)
    _rc_music_add_desktop_test(rc_music_open_sources_command_tests
        rc_music.desktop.open-sources.command OpenSourcesCommandTests.cpp m0_qt_shell_bootstrap)
    _rc_music_add_desktop_test(rc_music_open_sources_dispatcher_tests
        rc_music.desktop.open-sources.dispatcher OpenSourcesDispatcherTests.cpp m0_qt_shell_bootstrap)
    _rc_music_add_desktop_test(rc_music_single_instance_coordinator_tests
        rc_music.desktop.single-instance.coordinator QtSingleInstanceCoordinatorTests.cpp
        m0_qt_shell_bootstrap Qt6::Network)
    _rc_music_add_desktop_test(rc_music_preferences_json_codec_tests
        rc_music.desktop.preferences.json-codec PreferencesJsonCodecTests.cpp m0_qt_preferences_persistence)
    _rc_music_add_desktop_test(rc_music_session_json_codec_tests
        rc_music.desktop.session.json-codec SessionJsonCodecTests.cpp m0_qt_session_persistence)
    _rc_music_add_desktop_test(rc_music_audio_file_locator_tests
        rc_music.desktop.audio-file-locator QtAudioFileLocatorTests.cpp m0_qt_audio_file_locator Qt6::Widgets)
    _rc_music_add_desktop_test(rc_music_audio_files_picker_tests
        rc_music.desktop.audio-files-picker QtAudioFilesPickerTests.cpp m0_qt_audio_file_locator Qt6::Widgets)

    # The fake QFileDialog ignores one expected offscreen capability warning;
    # QtTest still rejects every other warning without a process-level abort.
    set_property(TEST rc_music.desktop.audio-file-locator rc_music.desktop.audio-files-picker
        APPEND PROPERTY ENVIRONMENT "QT_FATAL_WARNINGS=0")
    set_tests_properties(rc_music.desktop.single-instance.coordinator PROPERTIES
        LABELS "desktop;qt;ipc;single-instance;FR-004;FR-033;NFR-LIMIT-004;NFR-REL-004")
    set_tests_properties(rc_music.desktop.audio-file-locator PROPERTIES
        LABELS "desktop;qt;files;locator;UX-008")
    set_tests_properties(rc_music.desktop.audio-files-picker PROPERTIES
        LABELS "desktop;qt;files;picker;FR-001")
endfunction()
