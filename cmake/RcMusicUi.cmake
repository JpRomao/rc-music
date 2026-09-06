include_guard(GLOBAL)

# Reuse exactly the same presentation sources for production and the isolated
# scenario-test variant. Each caller owns its plugin and QML output directory.
function(rc_music_add_ui target support qml_output)
    set(qml_files)
    foreach(qml_file IN ITEMS
            Main.qml
            EmptyState.qml
            AccessibleActionButton.qml
            AccessibleTransportButton.qml
            NowPlayingState.qml
            ErrorState.qml
            ErrorText.qml
            PlaybackAccessibilityStatus.qml
            PlaybackAccessibilityText.qml
            TransportBar.qml
            TransportText.qml
            TransportSlider.qml
            QueuePanel.qml
            QueueText.qml
            QueueActionButton.qml
            QueueClearConfirmation.qml
            SourceImportStatus.qml
            SourceImportText.qml
            PreferencesPanel.qml
            PreferencePersistenceNotice.qml
            WindowNotice.qml
            PreferencesText.qml
            PreferenceChoice.qml
            PreferenceToggle.qml
            PreferenceSecondsField.qml
            TrackDetailsField.qml
            TrackDetailsPanel.qml
            TrackDetailsText.qml)
        set(qml_source "${RC_MUSIC_SOURCE_ROOT}/src/ui/qml/${qml_file}")
        set_source_files_properties("${qml_source}" PROPERTIES QT_RESOURCE_ALIAS "${qml_file}")
        list(APPEND qml_files "${qml_source}")
    endforeach()

    qt_add_library(${target} STATIC)
    qt_add_qml_module(${target}
        URI MusicPlayer.M0
        VERSION 1.0
        OUTPUT_DIRECTORY "${qml_output}/MusicPlayer/M0"
        SOURCES
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ShellUiContract.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ShellUiContract.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ArtworkImageProvider.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ArtworkImageProvider.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ApplicationText.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PlayerPresenter.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PlayerPresenter.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/SystemMediaControlsBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/SystemMediaControlsBinding.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/SystemPowerEventsBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/SystemPowerEventsBinding.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LifecycleText.hpp"
        QML_FILES ${qml_files}
    )
    qt_add_translations(${target}
        SOURCE_TARGETS ${target}
        TS_FILES "${RC_MUSIC_SOURCE_ROOT}/src/ui/translations/music-player_pt_BR.ts"
        RESOURCE_PREFIX "/i18n"
        LUPDATE_TARGET ${target}_lupdate
        LRELEASE_TARGET ${target}_lrelease
    )
    target_include_directories(${target}
        PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src"
        PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/presentation"
    )
    target_link_libraries(${target} PRIVATE
        ${support}
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickLayouts
    )
    rc_music_configure_cpp(${target})
    rc_music_configure_cpp(${target}plugin)
    if(MSVC)
        target_compile_options(${target} PRIVATE /wd4702)
    endif()

    set(RC_MUSIC_DESKTOP_PLACEHOLDER_ASSET_FILE "${RC_MUSIC_SOURCE_ROOT}/assets/cover-placeholder.svg")
    set(RC_MUSIC_DESKTOP_TRACK_ASSET_FILE "${RC_MUSIC_SOURCE_ROOT}/assets/cover-track.svg")
    set_source_files_properties("${RC_MUSIC_DESKTOP_PLACEHOLDER_ASSET_FILE}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/cover-placeholder.svg")
    set_source_files_properties("${RC_MUSIC_DESKTOP_TRACK_ASSET_FILE}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/cover-track.svg")
    qt_add_resources(${target} m0_qt_shell_assets
        PREFIX "/m0"
        FILES "${RC_MUSIC_DESKTOP_PLACEHOLDER_ASSET_FILE}" "${RC_MUSIC_DESKTOP_TRACK_ASSET_FILE}"
    )
endfunction()
