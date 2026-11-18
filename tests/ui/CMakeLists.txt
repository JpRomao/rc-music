# This directory owns a separate QML module with the same public URI. Never
# link the scenario variant into the real application (including testing ON).
if(NOT BUILD_TESTING OR NOT RC_MUSIC_BUILD_DESKTOP)
    message(FATAL_ERROR "UI scenarios require the product desktop test configuration")
endif()
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 QuickLayouts QuickTest Test LinguistTools)
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

add_ui_test(rc_music_lyrics_service_tests rc_music.lyrics.service
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtLyricsServiceTests.cpp" rc_music_lyrics)
set_target_properties(rc_music_lyrics_service_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_youtube_codec_tests rc_music.youtube.codec
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeAudioCodecTests.cpp" rc_music_youtube_codec rc_music_media_data)
target_sources(rc_music_youtube_codec_tests PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters/NativeDownloadCodec.cpp")
target_compile_definitions(rc_music_youtube_codec_tests PRIVATE
    RC_MUSIC_YOUTUBE_PROTOCOL_VECTORS_PATH="${RC_MUSIC_SOURCE_ROOT}/tests/extensions/youtube-protocol-vectors.json")
set_target_properties(rc_music_youtube_codec_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_youtube_resolution_tests rc_music.youtube.resolution
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeAudioResolutionTests.cpp" rc_music_youtube_resolution)
set_target_properties(rc_music_youtube_resolution_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_youtube_broker_protocol_tests rc_music.youtube.broker-protocol
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeBrokerProtocolTests.cpp" rc_music_youtube_resolution)
set_target_properties(rc_music_youtube_broker_protocol_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_youtube_resolver_session_tests rc_music.youtube.resolver-session
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeResolverSessionTests.cpp" rc_music_youtube_resolution)
set_target_properties(rc_music_youtube_resolver_session_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_youtube_links_tests rc_music.youtube.links
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeLinksTests.cpp" rc_music_youtube_codec)
set_target_properties(rc_music_youtube_links_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_youtube_search_tests rc_music.youtube.search
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeSearchTests.cpp" rc_music_youtube_resolution)
set_target_properties(rc_music_youtube_search_tests PROPERTIES AUTOMOC ON)
if(TARGET rc_music_browser_integration AND TARGET rc_music_download_documents)
    add_ui_test(rc_music_youtube_admission_tests rc_music.youtube.admission
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeAudioAdmissionTests.cpp" rc_music_browser_integration)
    target_link_libraries(rc_music_youtube_admission_tests PRIVATE rc_music_download_documents)
    set_target_properties(rc_music_youtube_admission_tests PROPERTIES AUTOMOC ON)
endif()
if(WIN32 AND TARGET rc_music_download_network)
    add_ui_test(rc_music_youtube_execution_tests rc_music.youtube.execution
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/YoutubeDownloadExecutionTests.cpp" rc_music_download_network)
    set_target_properties(rc_music_youtube_execution_tests PROPERTIES AUTOMOC ON)
endif()
add_ui_test(rc_music_lyrics_variants_store_tests rc_music.lyrics.variants-store
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtJsonLyricsVariantsStoreTests.cpp" rc_music_lyrics)
set_target_properties(rc_music_lyrics_variants_store_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_lyrics_romanizer_tests rc_music.lyrics.romanizer
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtLyricsRomanizerTests.cpp" rc_music_lyrics)
set_target_properties(rc_music_lyrics_romanizer_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_playlists_store_tests rc_music.playlists.store
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtJsonPlaylistStoreTests.cpp" rc_music_playlists rc_music_media_data)
set_target_properties(rc_music_playlists_store_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_profile_store_tests rc_music.profile.store
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtJsonProfileStoreTests.cpp" rc_music_profile)
set_target_properties(rc_music_profile_store_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_room_roster_store_tests rc_music.room.roster-store
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtJsonRoomRosterStoreTests.cpp" rc_music_profile)
set_target_properties(rc_music_room_roster_store_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_room_protocol_tests rc_music.room.protocol
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/RoomProtocolCodecTests.cpp" rc_music_room_protocol)
set_target_properties(rc_music_room_protocol_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_room_track_catalog_tests rc_music.room.track-catalog
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtRoomTrackCatalogTests.cpp" rc_music_room_catalog)
set_target_properties(rc_music_room_track_catalog_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_room_host_state_tests rc_music.room.host-state
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/ListeningRoomHostStateTests.cpp" rc_music_room_protocol)
set_target_properties(rc_music_room_host_state_tests PROPERTIES AUTOMOC ON)
if(TARGET rc_music_listening_room)
    add_ui_test(rc_music_room_identity_tests rc_music.room.identity
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsRoomIdentityTests.cpp" rc_music_listening_room)
    set_target_properties(rc_music_room_identity_tests PROPERTIES AUTOMOC ON)
    add_ui_test(rc_music_room_envelope_cipher_tests rc_music.room.envelope-cipher
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/WindowsRoomEnvelopeCipherTests.cpp" rc_music_listening_room)
    set_target_properties(rc_music_room_envelope_cipher_tests PROPERTIES AUTOMOC ON)
    # Two peers in one process over tests/support/LoopbackSignaling.hpp (127.0.0.1, host
    # candidates only): no internet, no audio, no personal files.
    add_ui_test(rc_music_room_transport_tests rc_music.room.transport
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/DataChannelRoomTransportTests.cpp" rc_music_listening_room
        rc_music_room_protocol Qt6::Network)
    set_target_properties(rc_music_room_transport_tests PROPERTIES AUTOMOC ON)
    set_tests_properties(rc_music.room.transport PROPERTIES TIMEOUT 180 LABELS "ui;qt;listening-room")
    rc_music_copy_listening_room_runtime(rc_music_room_transport_tests)
endif()
add_ui_test(rc_music_avatar_image_tests rc_music.profile.avatar-image
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtAvatarImageTests.cpp" rc_music_profile)
set_target_properties(rc_music_avatar_image_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_profile_binding_tests rc_music.profile.binding
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/ProfileBindingTests.cpp" rc_music_profile)
set_target_properties(rc_music_profile_binding_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_library_store_tests rc_music.library.store
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/QtJsonLibraryStoreTests.cpp" rc_music_library rc_music_media_data)
set_target_properties(rc_music_library_store_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_library_binding_tests rc_music.library.binding
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/LibraryBindingTests.cpp" rc_music_library)
set_target_properties(rc_music_library_binding_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_library_artwork_tests rc_music.library.artwork
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/LibraryArtworkTests.cpp" rc_music_library_artwork)
set_target_properties(rc_music_library_artwork_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_online_search_binding_tests rc_music.online-search.binding
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/OnlineSearchBindingTests.cpp" rc_music_online_search)
set_target_properties(rc_music_online_search_binding_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_playlists_binding_tests rc_music.playlists.binding
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/PlaylistsBindingTests.cpp" rc_music_playlists)
set_target_properties(rc_music_playlists_binding_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_playlists_restart_tests rc_music.playlists.restart
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/PlaylistsRestartTests.cpp" rc_music_playlists)
set_target_properties(rc_music_playlists_restart_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_lyrics_binding_tests rc_music.lyrics.binding
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/LyricsBindingTests.cpp" rc_music_lyrics)
target_sources(rc_music_lyrics_binding_tests PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.cpp")
set_target_properties(rc_music_lyrics_binding_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_download_lyrics_lookup_tests rc_music.lyrics.download-lookup
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/DownloadLyricsLookupTests.cpp" rc_music_lyrics)
set_target_properties(rc_music_download_lyrics_lookup_tests PROPERTIES AUTOMOC ON)
add_ui_test(rc_music_download_lyrics_binding_tests rc_music.lyrics.download-binding
    "${RC_MUSIC_SOURCE_ROOT}/tests/qt/DownloadLyricsBindingTests.cpp" rc_music_lyrics)
target_sources(rc_music_download_lyrics_binding_tests PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LyricsBinding.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadLyricsBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/DownloadLyricsBinding.cpp")
set_target_properties(rc_music_download_lyrics_binding_tests PROPERTIES AUTOMOC ON)
add_executable(rc_music_lyrics_online_probe "${RC_MUSIC_SOURCE_ROOT}/tests/qt/LyricsOnlineProbe.cpp")
target_link_libraries(rc_music_lyrics_online_probe PRIVATE rc_music_lyrics)
rc_music_configure_cpp(rc_music_lyrics_online_probe)

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
        qtest/DownloadApplicationBindingTests.cpp rc_music_test_ui rc_music_test_uiplugin
        rc_music_browser_integration rc_music_downloads Qt6::Qml Qt6::Quick)
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
if(WIN32 AND TARGET rc_music_player_downloads)
    rc_music_add_player_downloads(rc_music_test_player_downloads rc_music_test_ui)
    target_link_libraries(rc_music_test_player_downloads PRIVATE rc_music_ui_scenario_support)
    add_ui_test(rc_music_ui_player_downloads_tests rc_music.ui.player-downloads
        qtest/WindowsPlayerDownloadsTests.cpp rc_music_test_player_downloads)
    if(TARGET rc_music_native_messaging)
        # Test consumers exercise IPC explicitly. Never propagate their
        # compile profile into the owner implementation or the UI.
        target_link_libraries(rc_music_ui_player_downloads_tests PRIVATE rc_music_native_messaging)
        target_compile_definitions(rc_music_ui_player_downloads_tests PRIVATE RC_MUSIC_TEST_PLAYER_NATIVE_INGRESS)
    endif()
    set_tests_properties(rc_music.ui.player-downloads PROPERTIES
        LABELS "ui;qt;downloads;persistence;lifecycle;windows;FR-051;FR-053")
    if(RC_MUSIC_DOWNLOAD_TEST_CRT_DIR AND RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN)
        # Exercise the production presenter ABI and owner. Only DNS/transfer
        # come from the framed fixture helper; conversion/publication/import
        # and both persistence acknowledgements use their real adapters.
        add_executable(rc_music_player_downloads_composition_tests qtest/PlayerDownloadsCompositionTests.cpp)
        target_link_libraries(rc_music_player_downloads_composition_tests PRIVATE
            rc_music_player_downloads m0_qt_session_persistence Qt6::Test Qt6::Quick Advapi32)
        rc_music_configure_cpp(rc_music_player_downloads_composition_tests)
        set_target_properties(rc_music_player_downloads_composition_tests PROPERTIES AUTOMOC ON)
        target_compile_definitions(rc_music_player_downloads_composition_tests PRIVATE
            RC_MUSIC_TEST_IO_HELPER_PATH="$<TARGET_FILE:rc_music_download_io_helper>"
            RC_MUSIC_TEST_PAYLOAD_PATH="$<TARGET_FILE:rc_music_download_network_payload>"
            RC_MUSIC_TEST_TLS_CORE_PATH="$<TARGET_FILE:Qt6::Core>"
            RC_MUSIC_TEST_TLS_NETWORK_PATH="$<TARGET_FILE:Qt6::Network>"
            RC_MUSIC_TEST_TLS_PLUGIN_PATH="${RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN}"
            RC_MUSIC_TEST_CRT_DIR="${RC_MUSIC_DOWNLOAD_TEST_CRT_DIR}"
            RC_MUSIC_TEST_MF_WORKER_PATH="$<TARGET_FILE:rc_music_media_foundation_worker>")
        add_dependencies(rc_music_player_downloads_composition_tests
            rc_music_download_io_helper rc_music_download_network_payload rc_music_media_foundation_worker)
        if(TARGET rc_music_native_messaging)
            string(SHA256 player_youtube_native_identity "${CMAKE_BINARY_DIR}")
            string(SUBSTRING "${player_youtube_native_identity}" 0 16 player_youtube_native_identity)
            set(native_config_origin "chrome-extension://cccccccccccccccccccccccccccccccc/")
            set(native_config_endpoint "rc-music-youtube-owner-${player_youtube_native_identity}")
            set(native_config_owner_images "QStringLiteral(\"rc_music_player_downloads_composition_tests.exe\")")
            set(native_config_host_images "QStringLiteral(\"rc_music_youtube_owner_native_host_test.exe\")")
            set(player_youtube_native_config "${CMAKE_CURRENT_BINARY_DIR}/player-youtube-owner-native-host")
            configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/NativeHostBuildConfiguration.hpp.in"
                "${player_youtube_native_config}/NativeHostBuildConfiguration.hpp" @ONLY)
            add_executable(rc_music_youtube_owner_native_host_test
                "${RC_MUSIC_SOURCE_ROOT}/src/app/native_host_main.cpp")
            target_compile_definitions(rc_music_youtube_owner_native_host_test PRIVATE RC_MUSIC_YOUTUBE_NATIVE_HOST)
            target_include_directories(rc_music_youtube_owner_native_host_test PRIVATE "${player_youtube_native_config}")
            target_link_libraries(rc_music_youtube_owner_native_host_test PRIVATE rc_music_native_messaging)
            rc_music_configure_cpp(rc_music_youtube_owner_native_host_test)
            # Peer image profiles resolve siblings of the actual executable,
            # never CWD, ../, or an identity supplied over the protocol.
            set_target_properties(rc_music_youtube_owner_native_host_test rc_music_player_downloads_composition_tests
                PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
            target_link_libraries(rc_music_player_downloads_composition_tests PRIVATE rc_music_native_messaging)
            target_include_directories(rc_music_player_downloads_composition_tests PRIVATE "${player_youtube_native_config}")
            target_compile_definitions(rc_music_player_downloads_composition_tests PRIVATE
                RC_MUSIC_TEST_PLAYER_NATIVE_INGRESS
                RC_MUSIC_TEST_YOUTUBE_OWNER_HOST_PATH="$<TARGET_FILE:rc_music_youtube_owner_native_host_test>"
                RC_MUSIC_TEST_YOUTUBE_FIXTURE_IO_HELPER_PATH="$<TARGET_FILE:rc_music_download_youtube_fixture_io_helper>")
            add_dependencies(rc_music_player_downloads_composition_tests
                rc_music_youtube_owner_native_host_test rc_music_download_youtube_fixture_io_helper)
        endif()
        add_test(NAME rc_music.ui.player-downloads-composition COMMAND rc_music_player_downloads_composition_tests -o -,txt)
        set_tests_properties(rc_music.ui.player-downloads-composition PROPERTIES
            LABELS "ui;qt;downloads;persistence;process;lifecycle;windows;FR-051;FR-053" TIMEOUT 90
            ENVIRONMENT "${ui_environment}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    if(TARGET rc_music_native_messaging)
        set(player_native_ingress_expected ON)
    else()
        set(player_native_ingress_expected OFF)
    endif()
    if(TARGET rc_music_player_downloads_composition_tests)
        set(player_native_composition_available ON)
    else()
        set(player_native_composition_available OFF)
    endif()
    add_test(NAME rc_music.ui.player-native-ingress-build-boundary COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
        "-DNATIVE_INGRESS_EXPECTED=${player_native_ingress_expected}"
        "-DCOMPOSITION_TEST_AVAILABLE=${player_native_composition_available}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/PlayerNativeIngressBuildBoundaryTests.cmake")
    set_tests_properties(rc_music.ui.player-native-ingress-build-boundary PROPERTIES
        LABELS "ui;boundary;native;downloads" TIMEOUT 20)
endif()
if(WIN32 AND TARGET rc_music_download_network AND TARGET rc_music_download_persistence AND
        RC_MUSIC_DOWNLOAD_TEST_FFMPEG AND RC_MUSIC_DOWNLOAD_TEST_FFPROBE)
    add_ui_test(rc_music_ui_download_recovery_validation_tests rc_music.ui.download-recovery-validation
        qtest/DownloadRecoveryValidationCompositionTests.cpp rc_music_test_ui rc_music_test_uiplugin
        rc_music_download_network rc_music_download_persistence m0_qt_session_persistence Qt6::Qml Qt6::Quick Qt6::QuickTest)
    target_link_libraries(rc_music_ui_download_recovery_validation_tests PRIVATE advapi32)
    target_compile_definitions(rc_music_ui_download_recovery_validation_tests PRIVATE
        RC_MUSIC_TEST_FFMPEG_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFMPEG}"
        RC_MUSIC_TEST_FFPROBE_PATH="${RC_MUSIC_DOWNLOAD_TEST_FFPROBE}"
        RC_MUSIC_TEST_AUDIO_FIXTURES="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid")
    set_tests_properties(rc_music.ui.download-recovery-validation PROPERTIES
        LABELS "ui;qt;downloads;persistence;windows;FR-051;FR-053")
endif()
foreach(target IN ITEMS rc_music_ui_fixture_tests rc_music_ui_presenter_tests)
    target_compile_definitions(${target} PRIVATE
        M0_TEST_FIXTURE_FILE="${ui_fixtures}/fixture.v1.json"
        M0_TEST_QUEUE_FILE="${ui_fixtures}/queue.v1.json")
endforeach()

add_ui_test(rc_music_ui_contract_tests rc_music.ui.contract
    qtest/ShellUiContractTests.cpp rc_music_test_ui rc_music_test_uiplugin m0_qt_fixture_data Qt6::Gui Qt6::Qml)
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
    m0_qt_fixture_data m0_qt_system_appearance rc_music_playlists Qt6::QuickControls2 Qt6::QuickTest)
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
