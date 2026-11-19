include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RcMusicShell.cmake")

# Both entry points use the same desktop sources and targets. The caller chooses
# shell/audio support; test resources and test instrumentation belong to it.
if(NOT TARGET rc_music_core OR NOT TARGET rc_music_workers)
    message(FATAL_ERROR "RcMusicCore.cmake must be included before RcMusicDesktop.cmake")
endif()
if(NOT DEFINED RC_MUSIC_DESKTOP_SHELL_SUPPORT OR NOT DEFINED RC_MUSIC_DESKTOP_AUDIO_SUPPORT)
    message(FATAL_ERROR "The desktop entry point must select shell and audio support")
endif()

add_library(m0_qt_core INTERFACE)
target_link_libraries(m0_qt_core INTERFACE rc_music_core rc_music_workers)

if(RC_MUSIC_DESKTOP_SHELL_SUPPORT)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS
        Core
        Gui
        Network
        Qml
        Quick
        QuickControls2
        QuickLayouts
        LinguistTools
        Svg
        Widgets
    )
    qt_standard_project_setup(REQUIRES 6.11)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

    add_library(m0_qt_audio_file_locator STATIC
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/AudioFileLocator.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/AudioFilesPicker.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAudioFileDialog.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAudioFileDialog.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAudioFileLocator.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAudioFileLocator.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAudioFilesPicker.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtAudioFilesPicker.cpp"
    )
    target_include_directories(m0_qt_audio_file_locator PUBLIC
        "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_audio_file_locator PUBLIC rc_music_contracts Qt6::Core Qt6::Gui PRIVATE Qt6::Widgets)
    rc_music_configure_cpp(m0_qt_audio_file_locator)
    set_target_properties(m0_qt_audio_file_locator PROPERTIES AUTOMOC ON)

    add_library(m0_qt_shell_platform STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtShellPlatform.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtShellPlatform.cpp"
    )
    target_include_directories(m0_qt_shell_platform PUBLIC
        "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_shell_platform PUBLIC rc_music_contracts Qt6::Core Qt6::Gui PRIVATE Qt6::Widgets)
    if(WIN32)
        target_sources(m0_qt_shell_platform PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsSourceVolumeEvents.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsSourceVolumeEvents.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsTaskbarEvents.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsTaskbarEvents.cpp"
        )
        target_link_libraries(m0_qt_shell_platform PRIVATE user32)
    endif()
    rc_music_configure_cpp(m0_qt_shell_platform)
    set_target_properties(m0_qt_shell_platform PROPERTIES AUTOMOC ON)

    if(WIN32)
        add_library(m0_qt_windows_smtc STATIC
            "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/SystemMediaControls.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsSmtcAdapter.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsSmtcAdapter.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WinRtSmtcNativeBoundary.cpp"
        )
        target_include_directories(m0_qt_windows_smtc PUBLIC
            "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
        target_link_libraries(m0_qt_windows_smtc PUBLIC rc_music_contracts Qt6::Core Qt6::Gui PRIVATE windowsapp)
        rc_music_configure_cpp(m0_qt_windows_smtc)
        set_target_properties(m0_qt_windows_smtc PROPERTIES AUTOMOC ON)

        add_library(m0_qt_windows_power_events STATIC
            "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/SystemPowerEvents.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsPowerEvents.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsPowerEvents.cpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/Win32SuspendResumeNativeBoundary.cpp"
        )
        target_include_directories(m0_qt_windows_power_events PUBLIC
            "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
        target_link_libraries(m0_qt_windows_power_events PUBLIC rc_music_contracts Qt6::Core Qt6::Gui PRIVATE user32)
        rc_music_configure_cpp(m0_qt_windows_power_events)
        set_target_properties(m0_qt_windows_power_events PROPERTIES AUTOMOC ON)
    endif()

    add_library(m0_qt_system_appearance STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtSystemAppearance.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtSystemAppearance.cpp"
    )
    target_include_directories(m0_qt_system_appearance PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_system_appearance PUBLIC Qt6::Core Qt6::Gui)
    if(WIN32)
        target_link_libraries(m0_qt_system_appearance PRIVATE user32)
    endif()
    rc_music_configure_cpp(m0_qt_system_appearance)
    set_target_properties(m0_qt_system_appearance PROPERTIES AUTOMOC ON)

    if(WIN32)
        add_library(m0_qt_diagnostic_privacy STATIC
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtDiagnosticPrivacy.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtDiagnosticPrivacy.cpp"
        )
        target_include_directories(m0_qt_diagnostic_privacy PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
        target_link_libraries(m0_qt_diagnostic_privacy PUBLIC Qt6::Core)
        rc_music_configure_cpp(m0_qt_diagnostic_privacy)

        add_library(m0_qt_diagnostic_store STATIC
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtWindowsDiagnosticLogStore.cpp")
        target_include_directories(m0_qt_diagnostic_store PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
        target_link_libraries(m0_qt_diagnostic_store PUBLIC m0_qt_core Qt6::Core)
        rc_music_configure_cpp(m0_qt_diagnostic_store)
    endif()

    add_library(m0_qt_preferences_persistence STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/PreferencesJsonCodec.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/PreferencesJsonCodec.cpp"
    )
    if(WIN32)
        target_sources(m0_qt_preferences_persistence PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonWindowsPreferencesStore.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonWindowsPreferencesStore.cpp"
        )
    endif()
    target_include_directories(m0_qt_preferences_persistence PUBLIC
        "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_preferences_persistence PUBLIC m0_qt_core Qt6::Core)
    rc_music_configure_cpp(m0_qt_preferences_persistence)

    add_library(m0_qt_session_persistence STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SessionJsonCodec.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SessionJsonCodec.cpp"
    )
    if(WIN32)
        target_sources(m0_qt_session_persistence PRIVATE
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonWindowsSessionStore.hpp"
            "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtJsonWindowsSessionStore.cpp"
        )
    endif()
    target_include_directories(m0_qt_session_persistence PUBLIC
        "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_session_persistence PUBLIC m0_qt_core Qt6::Core)
    rc_music_configure_cpp(m0_qt_session_persistence)

    add_library(m0_qt_shell_support STATIC
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/presentation/LocalQueueListModel.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/presentation/LocalQueueListModel.cpp"
    )
    target_include_directories(m0_qt_shell_support PUBLIC
        "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_shell_support PUBLIC m0_qt_core Qt6::Core)
    rc_music_configure_cpp(m0_qt_shell_support)
    set_target_properties(m0_qt_shell_support PROPERTIES AUTOMOC ON)

    include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicUi.cmake")
    rc_music_add_ui(m0_qt_shell_ui m0_qt_shell_support "${QT_QML_OUTPUT_DIRECTORY}")
    set(RC_MUSIC_DESKTOP_PLACEHOLDER_ASSET_FILE "${RC_MUSIC_SOURCE_ROOT}/assets/cover-placeholder.svg")
    set(RC_MUSIC_DESKTOP_TRACK_ASSET_FILE "${RC_MUSIC_SOURCE_ROOT}/assets/cover-track.svg")

    add_library(m0_qt_shell_bootstrap STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/app/OpenSourcesCommand.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/OpenSourcesCommand.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/OpenSourcesDispatcher.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/OpenSourcesDispatcher.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/QtSingleInstanceCoordinator.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/QtSingleInstanceCoordinator.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/ShellLaunchOptions.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/ShellLaunchOptions.cpp"
    )
    target_include_directories(m0_qt_shell_bootstrap
        PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src/app"
        PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src"
    )
    target_link_libraries(m0_qt_shell_bootstrap PUBLIC Qt6::Core PRIVATE Qt6::Network)
    rc_music_configure_cpp(m0_qt_shell_bootstrap)
    set_target_properties(m0_qt_shell_bootstrap PROPERTIES AUTOMOC ON)

    qt_add_executable(m0_qt_shell "${RC_MUSIC_SOURCE_ROOT}/src/app/main.cpp")
    set_target_properties(m0_qt_shell PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    if(WIN32)
        set_target_properties(m0_qt_shell PROPERTIES WIN32_EXECUTABLE TRUE)
        enable_language(RC)
        add_executable(m0_qt_windows_icon_generator EXCLUDE_FROM_ALL
            "${RC_MUSIC_SOURCE_ROOT}/tools/BuildWindowsIcon.cpp")
        target_link_libraries(m0_qt_windows_icon_generator PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
        rc_music_configure_cpp(m0_qt_windows_icon_generator)
        set_target_properties(m0_qt_windows_icon_generator PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        set(M0_WINDOWS_ICON_FILE "${CMAKE_BINARY_DIR}/rc-music.ico")
        add_custom_command(OUTPUT "${M0_WINDOWS_ICON_FILE}"
            COMMAND "${CMAKE_COMMAND}" -E env "PATH=$<TARGET_FILE_DIR:Qt6::Core>"
                "$<TARGET_FILE:m0_qt_windows_icon_generator>"
                "${RC_MUSIC_DESKTOP_TRACK_ASSET_FILE}" "${M0_WINDOWS_ICON_FILE}"
            DEPENDS m0_qt_windows_icon_generator "${RC_MUSIC_DESKTOP_TRACK_ASSET_FILE}"
            COMMENT "Generate the Windows icon from the existing tray artwork"
            VERBATIM)
        add_custom_target(m0_qt_windows_icon DEPENDS "${M0_WINDOWS_ICON_FILE}")
        add_dependencies(m0_qt_shell m0_qt_windows_icon)
        set(m0_windows_resource "${CMAKE_CURRENT_BINARY_DIR}/WindowsResources.rc")
        configure_file("${RC_MUSIC_SOURCE_ROOT}/src/app/WindowsResources.rc.in" "${m0_windows_resource}" @ONLY)
        set_source_files_properties("${m0_windows_resource}" PROPERTIES OBJECT_DEPENDS "${M0_WINDOWS_ICON_FILE}")
        target_sources(m0_qt_shell PRIVATE "${m0_windows_resource}")
    endif()

    add_library(m0_qt_diagnostic_binding STATIC "${RC_MUSIC_SOURCE_ROOT}/src/presentation/ShellDiagnosticBinding.cpp")
    target_include_directories(m0_qt_diagnostic_binding PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(m0_qt_diagnostic_binding PUBLIC m0_qt_shell_support m0_qt_shell_ui Qt6::Quick)
    rc_music_configure_cpp(m0_qt_diagnostic_binding)

    rc_music_link_shell(m0_qt_shell m0_qt_shell_ui m0_qt_shell_support m0_qt_diagnostic_binding)
    include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicTestProfileRoot.cmake")
endif()

if(RC_MUSIC_DESKTOP_AUDIO_SUPPORT)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Multimedia)
    find_package(TagLib 2.3.2 EXACT CONFIG REQUIRED)

    add_library(m0_qt_audio_adapters STATIC
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/adapters/QtMultimediaPlaybackBackend.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/QtMultimediaPlaybackBackend.cpp"
    )
    target_include_directories(m0_qt_audio_adapters PUBLIC "${RC_MUSIC_SOURCE_ROOT}/include")
    target_link_libraries(m0_qt_audio_adapters PUBLIC m0_qt_core Qt6::Multimedia)
    rc_music_configure_cpp(m0_qt_audio_adapters)
    set_target_properties(m0_qt_audio_adapters PROPERTIES AUTOMOC ON)

    add_library(m0_taglib_metadata_extractor STATIC
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/adapters/TagLibMetadataExtractor.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/domain/ArtworkLimits.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/MetadataExtractor.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/adapters/TagLibMetadataExtractor.cpp"
    )
    target_include_directories(m0_taglib_metadata_extractor PUBLIC "${RC_MUSIC_SOURCE_ROOT}/include")
    target_link_libraries(m0_taglib_metadata_extractor
        PUBLIC rc_music_contracts PRIVATE m0_qt_core TagLib::TagLib Qt6::Gui)
    rc_music_configure_cpp(m0_taglib_metadata_extractor)

    if(RC_MUSIC_DESKTOP_SHELL_SUPPORT)
        rc_music_link_shell_audio(m0_qt_shell)
    endif()
endif()

# Only the product entry point turns RC_MUSIC_LISTENING_ROOM on (default ON there);
# find_package(LibDataChannel) happens inside the module and nowhere else.
include("${RC_MUSIC_SOURCE_ROOT}/cmake/RcMusicListeningRoom.cmake")
if(TARGET rc_music_listening_room AND TARGET m0_qt_shell)
    rc_music_copy_listening_room_runtime(m0_qt_shell)
    # Only the product executable reaches the real transport; test shells keep the room unavailable.
    if(NOT RC_MUSIC_ROOM_SIGNALING_ORIGIN STREQUAL "" AND
       NOT RC_MUSIC_ROOM_SIGNALING_ORIGIN MATCHES "^(https://[A-Za-z0-9.-]+(:[0-9]+)?|http://127[.]0[.]0[.]1:[0-9]+)$")
        message(FATAL_ERROR "RC_MUSIC_ROOM_SIGNALING_ORIGIN must be https://<host> (or http://127.0.0.1:<port>)")
    endif()
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Concurrent)
    target_link_libraries(m0_qt_shell PRIVATE rc_music_listening_room Qt6::Concurrent)
    target_compile_definitions(m0_qt_shell PRIVATE RC_MUSIC_LISTENING_ROOM_AVAILABLE
        "RC_MUSIC_ROOM_SIGNALING_ORIGIN=\"${RC_MUSIC_ROOM_SIGNALING_ORIGIN}\"")
endif()
