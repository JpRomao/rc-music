cmake_minimum_required(VERSION 3.31)
if(NOT DEFINED BUILD_DIRECTORY OR NOT DEFINED NINJA_COMMAND)
    message(FATAL_ERROR "Listening room boundary requires BUILD_DIRECTORY and NINJA_COMMAND")
endif()
cmake_path(ABSOLUTE_PATH BUILD_DIRECTORY NORMALIZE OUTPUT_VARIABLE build_root)

# libdatachannel reaches a command through its usage requirements (RTC_ENABLE_*
# definitions, the installed include directory of libdatachannel) or through datachannel.lib/dll.
# Source files named DataChannel*.cpp (the transport and its test) are not libdatachannel itself.
set(datachannel_pattern "RTC_ENABLE_|[Ll]ib[Dd]ata[Cc]hannel|[Dd]ata[Cc]hannel[.](lib|dll)")
set(dll_path "(\"[^\"]*|[^ ]*)[Dd]ata[Cc]hannel[.]dll\"?")
set(runtime_copy_pattern "-E copy ${dll_path} ${dll_path}")

function(read_commands out)
    execute_process(COMMAND "${NINJA_COMMAND}" -C "${build_root}" -t commands ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE graph ERROR_VARIABLE error)
    if(NOT status STREQUAL "0" OR graph STREQUAL "")
        message(FATAL_ERROR "Cannot inspect the command closure of '${ARGN}': ${error}")
    endif()
    string(REPLACE "\\" "/" graph "${graph}")
    set(${out} "${graph}" PARENT_SCOPE)
endfunction()

# The listening room library is the single owner of LibDataChannel::LibDataChannel.
read_commands(owner rc_music_listening_room)
if(NOT owner MATCHES "RTC_ENABLE_WEBSOCKET=1" OR NOT owner MATCHES "RTC_ENABLE_MEDIA=0")
    message(FATAL_ERROR "rc_music_listening_room does not consume LibDataChannel::LibDataChannel")
endif()

# The domain core never sees libdatachannel, neither in its own build nor in its tests.
read_commands(core rc_music_core rc_music_core_tests)
if(core MATCHES "${datachannel_pattern}")
    message(FATAL_ERROR "rc_music_core reaches libdatachannel outside rc_music_listening_room")
endif()

# Across the whole default build (every other library, adapter, executable and test),
# a command may mention libdatachannel only when it compiles rc_music_listening_room,
# links that library, or copies the runtime DLL.
read_commands(everything)
string(REPLACE ";" "\\;" lines "${everything}")
string(REPLACE "\n" ";" lines "${lines}")
list(FILTER lines INCLUDE REGEX "${datachannel_pattern}")
foreach(line IN LISTS lines)
    string(REGEX REPLACE "${runtime_copy_pattern}" "" inspected "${line}")
    if(NOT inspected MATCHES "${datachannel_pattern}")
        continue()
    endif()
    if(inspected MATCHES " [-/]Fo" AND inspected MATCHES "/rc_music_listening_room[.]dir/")
        continue()
    endif()
    if(inspected MATCHES "rc_music_listening_room[.]lib" AND NOT inspected MATCHES "RTC_ENABLE_")
        continue()
    endif()
    message(FATAL_ERROR "A command reaches libdatachannel outside rc_music_listening_room: ${line}")
endforeach()

# The local Release runtime carries datachannel.dll beside the product executable.
read_commands(product m0_qt_shell)
if(NOT product MATCHES "${runtime_copy_pattern}")
    message(FATAL_ERROR "m0_qt_shell does not copy datachannel.dll beside the executable")
endif()
string(REGEX REPLACE "${runtime_copy_pattern}" "" product "${product}")
if(product MATCHES "${datachannel_pattern}")
    message(FATAL_ERROR "m0_qt_shell links or compiles libdatachannel directly")
endif()
message(STATUS "libdatachannel stays behind rc_music_listening_room; datachannel.dll ships beside m0_qt_shell")
