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
# Global so rc_music_copy_listening_room_runtime can name the DLL from other directories (the
# transport test in tests/ui); linking it stays limited to rc_music_listening_room.
set_target_properties(LibDataChannel::LibDataChannel PROPERTIES IMPORTED_GLOBAL TRUE)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core)

# The device identity and the signaling envelope cipher use only CNG/NCrypt/Crypt32/DPAPI, and the
# relay WebSocket uses WinHTTP (rtc::WebSocket cannot send X-Room-Host-Token); they live here because
# the transport (S6.3) is their only consumer. Only DataChannelRoomTransport.cpp includes rtc headers.
add_library(rc_music_listening_room STATIC
    "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/RoomTransport.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DataChannelBuildContract.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DataChannelRoomTransport.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/DataChannelRoomTransport.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRoomSignalingSocket.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRoomSignalingSocket.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRoomEnvelopeCipher.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRoomEnvelopeCipher.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRoomIdentity.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsRoomIdentity.cpp"
)
target_include_directories(rc_music_listening_room PUBLIC
    "${RC_MUSIC_SOURCE_ROOT}/include" "${RC_MUSIC_SOURCE_ROOT}/src")
target_link_libraries(rc_music_listening_room
    PUBLIC rc_music_contracts Qt6::Core
    PRIVATE LibDataChannel::LibDataChannel rc_music_room_protocol bcrypt ncrypt crypt32 winhttp)
rc_music_configure_cpp(rc_music_listening_room)
set_target_properties(rc_music_listening_room PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Release runtime: datachannel.dll (Mbed TLS linked in statically) sits beside the executable.
# The copy depends on the installed DLL, so a rebuilt libdatachannel refreshes it without a relink.
function(rc_music_copy_listening_room_runtime target)
    get_target_property(directory ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT directory)
        set(directory "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    set(runtime "${directory}/datachannel.dll")
    add_custom_command(OUTPUT "${runtime}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:LibDataChannel::LibDataChannel>" "${runtime}"
        DEPENDS "$<TARGET_FILE:LibDataChannel::LibDataChannel>"
        COMMENT "Copy datachannel.dll beside ${target}"
        VERBATIM)
    add_custom_target(${target}_listening_room_runtime DEPENDS "${runtime}")
    add_dependencies(${target} ${target}_listening_room_runtime)
endfunction()

if(BUILD_TESTING)
    add_test(NAME rc_music.listening-room.build-boundary COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/ListeningRoomBuildBoundaryTests.cmake")
    set_tests_properties(rc_music.listening-room.build-boundary PROPERTIES
        LABELS "listening-room;boundary" TIMEOUT 60)
endif()
