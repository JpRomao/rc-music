include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Network Concurrent)
add_library(rc_music_lyrics STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLyricsService.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLyricsService.cpp")
target_include_directories(rc_music_lyrics PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_lyrics PUBLIC rc_music_core Qt6::Core Qt6::Network PRIVATE Qt6::Concurrent)
set_target_properties(rc_music_lyrics PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_lyrics)

add_library(rc_music_lidarr STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLidarrService.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLidarrService.cpp")
target_include_directories(rc_music_lidarr PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_lidarr PUBLIC Qt6::Core Qt6::Network)
set_target_properties(rc_music_lidarr PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_lidarr)

# Reuse exactly the same presentation sources for production and the isolated
# scenario-test variant. Each caller owns its plugin and QML output directory.
function(rc_music_add_ui target support qml_output)
    set(qml_files)
    foreach(qml_file IN ITEMS
            Main.qml
            EmptyState.qml
            AccessibleActionButton.qml
            AccessibleTransportButton.qml
            AccessibleScrollBar.qml
            NowPlayingState.qml
            ErrorState.qml
            ErrorText.qml
            PlaybackAccessibilityStatus.qml
            PlaybackAccessibilityText.qml
            TransportBar.qml
            TransportText.qml
            TransportSlider.qml
            LyricsPanel.qml
            LidarrPanel.qml
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
            TrackDetailsText.qml
            DownloadsPanel.qml
            BrowserText.qml
            DownloadText.qml)
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
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LidarrBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LidarrBinding.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PlayerDownloadImport.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PlayerDownloadImport.cpp"
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
        rc_music_lyrics
        rc_music_lidarr
    )
    if(TARGET rc_music_browser_integration AND TARGET rc_music_downloads)
        target_sources(${target} PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadApplicationBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadApplicationBinding.cpp")
        target_link_libraries(${target} PRIVATE rc_music_browser_integration)
        target_compile_definitions(${target} PUBLIC RC_MUSIC_DOWNLOAD_UI_AVAILABLE)
    endif()
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
