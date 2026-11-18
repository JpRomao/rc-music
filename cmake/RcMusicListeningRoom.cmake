include_guard(GLOBAL)

# The listening room transport is the only part of the product that sees
# libdatachannel (ADR-0022). The domain core and the Qt adapters stay outside it;
# tests/cmake/ListeningRoomBuildBoundaryTests.cmake keeps that boundary.
if(NOT RC_MUSIC_LISTENING_ROOM)
    return()
endif()
if(NOT TARGET rc_music_core)
    message(FATAL_ERROR "RcMusicCore.cmake must be included before RcMusicListeningRoom.cmake")
endif()

find_package(LibDataChannel 0.24.6 EXACT CONFIG REQUIRED)

add_library(rc_music_listening_room STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DataChannelBuildContract.cpp"
)
target_include_directories(rc_music_listening_room PUBLIC
    "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_listening_room
    PUBLIC rc_music_contracts PRIVATE LibDataChannel::LibDataChannel)
rc_music_configure_cpp(rc_music_listening_room)
set_target_properties(rc_music_listening_room PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Release runtime: datachannel.dll (Mbed TLS linked in statically) sits beside the executable.
function(rc_music_copy_listening_room_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:LibDataChannel::LibDataChannel>" "$<TARGET_FILE_DIR:${target}>"
    )
endfunction()

if(BUILD_TESTING)
    add_test(NAME rc_music.listening-room.build-boundary COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/ListeningRoomBuildBoundaryTests.cmake")
    set_tests_properties(rc_music.listening-room.build-boundary PROPERTIES
        LABELS "listening-room;boundary" TIMEOUT 60)
endif()
