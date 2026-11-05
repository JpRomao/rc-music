include_guard(GLOBAL)

get_filename_component(RC_MUSIC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(rc_music_configure_cpp target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(rc_music_contracts INTERFACE)
target_include_directories(rc_music_contracts INTERFACE "${RC_MUSIC_SOURCE_ROOT}/include")
target_compile_features(rc_music_contracts INTERFACE cxx_std_20)

add_library(rc_music_core STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/Lyrics.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/LocalTrackSession.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/PlayerController.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/PreferencesAutosave.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/application/SessionAutosave.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/LocalAudioFilePolicy.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/LocalFileQueue.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/MetadataSanitization.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/DiagnosticRecord.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/QueueNavigation.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/SessionSnapshot.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/domain/WindowPlacement.cpp"
)
target_link_libraries(rc_music_core PUBLIC rc_music_contracts)
rc_music_configure_cpp(rc_music_core)

find_package(Threads REQUIRED)
add_library(rc_music_workers STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SingleWorkerLocalSourceDiscovery.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SingleWorkerMetadataReader.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SingleWorkerPreferencesWriter.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SingleWorkerSessionWriter.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/SingleWorkerDiagnosticWriter.cpp"
)
target_link_libraries(rc_music_workers PUBLIC rc_music_core Threads::Threads)
rc_music_configure_cpp(rc_music_workers)

# Only tests and the historical fixture shell request this target.
function(rc_music_add_test_support)
    if(NOT TARGET rc_music_test_support)
        add_library(rc_music_test_support STATIC EXCLUDE_FROM_ALL
            "${RC_MUSIC_SOURCE_ROOT}/tests/support/DeterministicPlaybackBackend.cpp"
        )
        target_include_directories(rc_music_test_support PUBLIC "${RC_MUSIC_SOURCE_ROOT}/tests/support/include")
        target_link_libraries(rc_music_test_support PUBLIC rc_music_core)
        rc_music_configure_cpp(rc_music_test_support)
    endif()
endfunction()
