include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Network Concurrent)
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicMediaData.cmake")
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicYoutube.cmake")

add_library(rc_music_lyrics STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DownloadLyricsLookup.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonLyricsVariantsStore.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonLyricsVariantsStore.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLyricsRomanizer.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLyricsService.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtLyricsService.cpp")
target_include_directories(rc_music_lyrics PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_lyrics PUBLIC rc_music_core Qt6::Core Qt6::Network PRIVATE rc_music_media_data Qt6::Concurrent)
set_target_properties(rc_music_lyrics PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_lyrics)

add_library(rc_music_profile STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonProfileStore.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonProfileStore.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonRoomRosterStore.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonRoomRosterStore.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAvatarImage.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAvatarImage.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ProfileBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ProfileBinding.cpp")
target_include_directories(rc_music_profile PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_profile PUBLIC rc_music_core Qt6::Core Qt6::Gui PRIVATE rc_music_media_data)
set_target_properties(rc_music_profile PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_profile)

# ADR-0022: wire codec of the listening room (control JSON and binary file chunks).
add_library(rc_music_room_protocol STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/RoomProtocolCodec.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/RoomProtocolCodec.cpp")
target_include_directories(rc_music_room_protocol PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_room_protocol PUBLIC rc_music_core Qt6::Core PRIVATE rc_music_media_data)
rc_music_configure_cpp(rc_music_room_protocol)

# ADR-0022: hash-addressed file transfer over a room link and the disposable room cache.
add_library(rc_music_room_transfer STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/RoomCache.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/RoomCache.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/RoomFileTransfer.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/RoomFileTransfer.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtRoomFileOutbox.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtRoomFileOutbox.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtRoomFileInbox.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtRoomFileInbox.cpp")
target_include_directories(rc_music_room_transfer PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_room_transfer PUBLIC rc_music_core Qt6::Core)
rc_music_configure_cpp(rc_music_room_transfer)

# ADR-0022: host tracks described for the listening room (hash, tags, chosen lyrics) off the UI thread.
add_library(rc_music_room_catalog STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtRoomTrackCatalog.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtRoomTrackCatalog.cpp")
target_include_directories(rc_music_room_catalog PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_room_catalog PUBLIC rc_music_core Qt6::Core PRIVATE Qt6::Concurrent)
rc_music_configure_cpp(rc_music_room_catalog)

# ADR-0022: "Ouvir junto" for QML (host and guest use cases); transport-agnostic, no libdatachannel.
add_library(rc_music_room_ui STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ListeningRoomBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ListeningRoomBinding.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomMembersModel.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomMembersModel.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomCatalogModel.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomCatalogModel.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomQueueModel.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomQueueModel.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomRolesModel.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/RoomRolesModel.cpp")
target_include_directories(rc_music_room_ui PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_room_ui PUBLIC rc_music_core Qt6::Core)
set_target_properties(rc_music_room_ui PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_room_ui)

add_library(rc_music_playlists STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonPlaylistStore.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonPlaylistStore.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PlaylistsBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PlaylistsBinding.cpp")
target_include_directories(rc_music_playlists PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_playlists PUBLIC rc_music_core Qt6::Core PRIVATE rc_music_media_data Qt6::Concurrent)
set_target_properties(rc_music_playlists PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_playlists)

# ADR-0019: permanent local library catalog (library.json v1).
add_library(rc_music_library STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonLibraryStore.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonLibraryStore.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryBinding.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryListModel.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryListModel.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryArtworkIndex.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryArtworkIndex.cpp")
target_include_directories(rc_music_library PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_library PUBLIC rc_music_core Qt6::Core PRIVATE rc_music_media_data Qt6::Concurrent)
set_target_properties(rc_music_library PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_library)

# Library covers: embedded art read on demand into an in-memory cache.
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Quick)
add_library(rc_music_library_artwork STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryArtworkProvider.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LibraryArtworkProvider.cpp")
target_link_libraries(rc_music_library_artwork PUBLIC rc_music_library Qt6::Quick)
rc_music_configure_cpp(rc_music_library_artwork)

# ADR-0019: online search page (metadata only; downloads go through admission).
add_library(rc_music_online_search STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/OnlineSearchBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/OnlineSearchBinding.cpp")
target_include_directories(rc_music_online_search PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/include")
target_link_libraries(rc_music_online_search PUBLIC rc_music_youtube_codec Qt6::Core Qt6::Gui)
set_target_properties(rc_music_online_search PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_online_search)

# Reuse exactly the same presentation sources for production and the isolated
# scenario-test variant. Each caller owns its plugin and QML output directory.
function(rc_music_add_ui target support qml_output)
    set(qml_files)
    foreach(qml_file IN ITEMS
            Main.qml
            PageSurface.qml
            TrackArtwork.qml
            NavigationButton.qml
            ShellIcon.qml
            PageHeader.qml
            WorkspaceHeader.qml
            QueuePreview.qml
            SideNavigation.qml
            NavigationText.qml
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
            LyricsText.qml
            LyricsPanel.qml
            LyricsReader.qml
            LyricsVariantsPanel.qml
            PlaylistsText.qml
            PlaylistsPanel.qml
            LibraryText.qml
            LibraryPanel.qml
            SearchText.qml
            SearchPanel.qml
            RoomText.qml
            ListeningRoomPanel.qml
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
            RoomOutputOffsetField.qml
            RoomRolesEditor.qml
            RoomMemberMenu.qml
            TrackDetailsField.qml
            TrackDetailsPanel.qml
            TrackDetailsText.qml
            DownloadsPanel.qml
            DownloadConfirmation.qml
            DownloadLyricsPanel.qml
            BrowserText.qml
            DownloadText.qml
            ProfileText.qml
            ListenerAvatar.qml
            AvatarPicker.qml
            ProfileSetupDialog.qml
            ControlsText.qml
            AuroraFocusRing.qml
            AuroraButton.qml
            AuroraInteraction.qml
            AuroraIconButton.qml
            AuroraTextField.qml
            AuroraSelect.qml
            AuroraSegmented.qml
            AuroraSwitch.qml
            AuroraStepper.qml
            AuroraCheckBox.qml
            AuroraChip.qml
            AuroraAvatar.qml)
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
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PresenterRoomHostPlayer.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/PresenterRoomHostPlayer.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadLyricsBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadLyricsBinding.cpp"
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
        rc_music_playlists
        rc_music_library
        rc_music_online_search
        rc_music_library_artwork
    )
    if(TARGET rc_music_browser_integration AND TARGET rc_music_downloads)
        target_sources(${target} PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadApplicationBinding.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadApplicationBinding.cpp")
        target_link_libraries(${target} PRIVATE rc_music_browser_integration Qt6::Concurrent)
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

    # Aurora line icons (24 px grid, black stroke tinted at runtime by ShellIcon).
    # Kept outside the frozen :/m0 inventory.
    file(GLOB rc_music_icon_files CONFIGURE_DEPENDS "${RC_MUSIC_SOURCE_ROOT}/assets/icons/*.svg")
    foreach(icon_file IN LISTS rc_music_icon_files)
        get_filename_component(icon_name "${icon_file}" NAME)
        set_source_files_properties("${icon_file}" PROPERTIES QT_RESOURCE_ALIAS "icons/${icon_name}")
    endforeach()
    # Listening-room avatars: 20 original geometric shapes in Aurora colors (24 px grid, full color).
    file(GLOB rc_music_avatar_files CONFIGURE_DEPENDS "${RC_MUSIC_SOURCE_ROOT}/assets/avatars/*.svg")
    foreach(avatar_file IN LISTS rc_music_avatar_files)
        get_filename_component(avatar_name "${avatar_file}" NAME)
        set_source_files_properties("${avatar_file}" PROPERTIES QT_RESOURCE_ALIAS "avatars/${avatar_name}")
    endforeach()
    qt_add_resources(${target} rc_music_shell_icons
        PREFIX "/rc_music"
        FILES ${rc_music_icon_files} ${rc_music_avatar_files}
    )
endfunction()
