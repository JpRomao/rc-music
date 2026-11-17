include_guard(GLOBAL)

# Share the executable's real wiring, not its instrumentation. The caller must
# select exactly one UI ABI and the matching diagnostic binding composition.
function(rc_music_link_shell target ui support)
    target_link_libraries(${target} PRIVATE
        m0_qt_preferences_persistence
        m0_qt_session_persistence
        m0_qt_shell_platform
        m0_qt_system_appearance
        m0_qt_shell_bootstrap
        rc_music_profile
        ${support}
        ${ui}
        ${ui}plugin
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
        Qt6::Widgets
    )
    if(WIN32)
        target_link_libraries(${target} PRIVATE
            m0_qt_windows_power_events m0_qt_windows_smtc
            m0_qt_diagnostic_privacy m0_qt_diagnostic_store ${ARGN})
    endif()
    target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src")
    rc_music_configure_cpp(${target})
endfunction()

function(rc_music_link_shell_audio target)
    target_link_libraries(${target} PRIVATE
        m0_qt_audio_adapters m0_qt_audio_file_locator m0_taglib_metadata_extractor)
    target_compile_definitions(${target} PRIVATE M0_QT_COMBINED_AUDIO)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:TagLib::tag>" "$<TARGET_FILE_DIR:${target}>"
    )
endfunction()
