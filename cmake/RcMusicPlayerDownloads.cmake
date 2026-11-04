include_guard(GLOBAL)

# A composition depends on exactly one UI ABI; the UI never links back to it.
# Call explicitly after that UI exists. This does not activate the app workflow.
function(rc_music_add_player_downloads target ui)
    if(NOT WIN32 OR NOT TARGET ${ui} OR NOT TARGET rc_music_download_network OR
       NOT TARGET rc_music_download_persistence)
        message(FATAL_ERROR "Player downloads requires Windows, one UI, download execution and persistence")
    endif()
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Qml Quick)
    add_library(${target} STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/presentation/WindowsPlayerDownloads.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/presentation/WindowsPlayerDownloads.cpp")
    target_include_directories(${target} PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src")
    target_link_libraries(${target} PUBLIC ${ui} rc_music_download_network rc_music_download_persistence)
    target_link_libraries(${target} PRIVATE Qt6::Qml Qt6::Quick)
    rc_music_configure_cpp(${target})
endfunction()
