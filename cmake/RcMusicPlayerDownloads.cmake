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
    if(TARGET rc_music_native_messaging)
        # Only this owner implementation acquires the optional ingress. Keep
        # its public ABI and both UI variants independent of this build flag.
        target_link_libraries(${target} PRIVATE rc_music_native_messaging)
        target_compile_definitions(${target} PRIVATE RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE)
    endif()
    rc_music_configure_cpp(${target})
endfunction()

# Explicit opt-in for one executable, after its owner and UI ABI exist. This is
# a reviewed C++ provider, not a runtime manifest/CLI/environment authority.
function(rc_music_configure_player_downloads target profile_header)
    cmake_parse_arguments(PARSE_ARGV 2 native_profile "" "NATIVE_PROFILE_HEADER" "")
    if(native_profile_UNPARSED_ARGUMENTS OR native_profile_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Unknown or incomplete player Native profile argument")
    endif()
    if("${profile_header}" STREQUAL "")
        return()
    endif()
    if(NOT TARGET ${target} OR NOT TARGET rc_music_player_downloads)
        message(FATAL_ERROR "A player download profile requires its executable and Windows owner")
    endif()
    if(NOT IS_ABSOLUTE "${profile_header}" OR IS_DIRECTORY "${profile_header}")
        message(FATAL_ERROR "Player download profile must name an absolute reviewed C++ header")
    endif()
    get_source_file_property(profile_generated "${profile_header}" GENERATED)
    if(NOT EXISTS "${profile_header}" AND NOT profile_generated)
        message(FATAL_ERROR "Player download profile header does not exist")
    endif()
    set(compiled_native_header "")
    if(DEFINED native_profile_NATIVE_PROFILE_HEADER)
        set(compiled_native_header "${native_profile_NATIVE_PROFILE_HEADER}")
    elseif(TARGET rc_music_native_messaging AND DEFINED RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER)
        set(compiled_native_header "${RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER}")
    endif()
    if(NOT "${compiled_native_header}" STREQUAL "")
        if(NOT TARGET rc_music_native_messaging OR NOT IS_ABSOLUTE "${compiled_native_header}" OR
           IS_DIRECTORY "${compiled_native_header}")
            message(FATAL_ERROR "Player Native profiles require Native support and an absolute reviewed header")
        endif()
        get_source_file_property(native_generated "${compiled_native_header}" GENERATED)
        if(NOT EXISTS "${compiled_native_header}" AND NOT native_generated)
            message(FATAL_ERROR "Player Native profile header does not exist")
        endif()
    endif()
    get_target_property(already_configured ${target} RC_MUSIC_PLAYER_DOWNLOAD_PROFILE)
    if(already_configured)
        message(FATAL_ERROR "Player download profile may be configured only once per executable")
    endif()
    set_property(TARGET ${target} PROPERTY RC_MUSIC_PLAYER_DOWNLOAD_PROFILE "${profile_header}")
    target_sources(${target} PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/src/app/PlayerDownloadsBuildProfile.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/PlayerDownloadsBuildProfile.cpp"
        "${profile_header}")
    target_link_libraries(${target} PRIVATE rc_music_player_downloads)
    target_compile_definitions(${target} PRIVATE RC_MUSIC_PLAYER_DOWNLOADS_CONFIGURED
        RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER="${profile_header}")
    if(NOT "${compiled_native_header}" STREQUAL "")
        target_sources(${target} PRIVATE "${compiled_native_header}")
        target_compile_definitions(${target} PRIVATE
            RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER="${compiled_native_header}")
    endif()
endfunction()
