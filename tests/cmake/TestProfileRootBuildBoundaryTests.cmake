cmake_minimum_required(VERSION 3.31)
foreach(required IN ITEMS BUILD_DIRECTORY NINJA_COMMAND SOURCE_ROOT EXPECTED SHELL_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "The test profile root boundary requires ${required}")
    endif()
endforeach()
if(NOT EXPECTED MATCHES "^(ON|OFF)$")
    message(FATAL_ERROR "EXPECTED must be ON or OFF")
endif()
cmake_path(ABSOLUTE_PATH BUILD_DIRECTORY NORMALIZE OUTPUT_VARIABLE build_root)

# S13.1: `--profile-root <dir>` belongs to test builds. The product configuration keeps
# RC_MUSIC_TEST_PROFILE_ROOT at its OFF default, and an OFF m0_qt_shell neither compiles, links nor
# contains the option. An ON build carries it only in m0_qt_shell.
set(option_pattern "RC_MUSIC_TEST_PROFILE_ROOT|TestProfileRoot|rc_music_test_profile_root")
set(definition_pattern " [-/]DRC_MUSIC_TEST_PROFILE_ROOT( |$)")

# The option defaults to OFF; the product runner forces it OFF even on a reused cache, and no other
# script that configures or packages the product sets it. (The runtime and installer plans refuse
# an ON build without the explicit local -TestProfileRoot opt-in; their PowerShell tests cover that.)
file(READ "${SOURCE_ROOT}/CMakeLists.txt" root_lists)
if(NOT root_lists MATCHES "\noption\\(RC_MUSIC_TEST_PROFILE_ROOT \"[^\"]*\" OFF\\)")
    message(FATAL_ERROR "RC_MUSIC_TEST_PROFILE_ROOT must be declared with an OFF default")
endif()
file(READ "${SOURCE_ROOT}/scripts/DevelopmentCommands.psm1" runner)
string(REGEX MATCHALL "RC_MUSIC_TEST_PROFILE_ROOT[^']*" runner_values "${runner}")
if(NOT runner_values STREQUAL "RC_MUSIC_TEST_PROFILE_ROOT=OFF")
    message(FATAL_ERROR "scripts/DevelopmentCommands.psm1 must configure only -DRC_MUSIC_TEST_PROFILE_ROOT=OFF")
endif()
foreach(script IN ITEMS scripts/develop.ps1 packaging/windows/New-RcMusicInstaller.ps1
        packaging/windows/RcMusicInstallerPlan.psm1)
    file(READ "${SOURCE_ROOT}/${script}" content)
    if(content MATCHES "RC_MUSIC_TEST_PROFILE_ROOT")
        message(FATAL_ERROR "${script} configures RC_MUSIC_TEST_PROFILE_ROOT; the product keeps it OFF")
    endif()
endforeach()

# This build was configured with the value the test was registered with.
file(STRINGS "${build_root}/CMakeCache.txt" cached REGEX "^RC_MUSIC_TEST_PROFILE_ROOT:BOOL=")
if(NOT cached STREQUAL "RC_MUSIC_TEST_PROFILE_ROOT:BOOL=${EXPECTED}")
    message(FATAL_ERROR "CMakeCache records '${cached}', expected RC_MUSIC_TEST_PROFILE_ROOT:BOOL=${EXPECTED}")
endif()

function(read_commands out)
    execute_process(COMMAND "${NINJA_COMMAND}" -C "${build_root}" -t commands ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE graph ERROR_VARIABLE error)
    if(NOT status STREQUAL "0" OR graph STREQUAL "")
        message(FATAL_ERROR "Cannot inspect the command closure of '${ARGN}': ${error}")
    endif()
    string(REPLACE "\\" "/" graph "${graph}")
    string(REPLACE ";" "\\;" graph "${graph}")
    string(REPLACE "\n" ";" graph "${graph}")
    set(${out} "${graph}" PARENT_SCOPE)
endfunction()

# The product executable's command closure. With OFF nothing in it mentions the option; with ON it
# compiles main.cpp with the definition and archives the parser library it depends on. (Link inputs
# sit in a response file, so the dependency shows through the library's own archive command.)
read_commands(product m0_qt_shell)
set(product_mentions "${product}")
list(FILTER product_mentions INCLUDE REGEX "${option_pattern}")
if(EXPECTED STREQUAL "OFF")
    if(product_mentions)
        list(GET product_mentions 0 first)
        message(FATAL_ERROR "m0_qt_shell reaches the test profile root with the option OFF: ${first}")
    endif()
else()
    set(compiles_main FALSE)
    set(builds_parser FALSE)
    foreach(line IN LISTS product_mentions)
        if(line MATCHES "${definition_pattern}" AND line MATCHES "/m0_qt_shell[.]dir/" AND line MATCHES "/main[.]cpp")
            set(compiles_main TRUE)
        endif()
        if(line MATCHES "[-/][Oo][Uu][Tt]:[^ ]*rc_music_test_profile_root[.]lib")
            set(builds_parser TRUE)
        endif()
    endforeach()
    if(NOT compiles_main OR NOT builds_parser)
        message(FATAL_ERROR "The ON build must compile main.cpp with RC_MUSIC_TEST_PROFILE_ROOT and link the parser")
    endif()
endif()

# Across the whole build, the definition reaches only sources of the ON m0_qt_shell target (its
# main.cpp and generated moc/resource sources), never a test shell or library.
read_commands(everything)
foreach(line IN LISTS everything)
    if(line MATCHES "${definition_pattern}" AND
       NOT (EXPECTED STREQUAL "ON" AND line MATCHES " [-/][Ff][Oo] ?[^ ]*/m0_qt_shell[.]dir/"))
        message(FATAL_ERROR "RC_MUSIC_TEST_PROFILE_ROOT is defined outside the ON m0_qt_shell: ${line}")
    endif()
endforeach()

# Only the parser's own unit test and the ON m0_qt_shell link the parser library.
execute_process(COMMAND "${NINJA_COMMAND}" -C "${build_root}" -t query src/rc_music_test_profile_root.lib
    RESULT_VARIABLE status OUTPUT_VARIABLE query ERROR_VARIABLE error)
if(NOT status STREQUAL "0" OR NOT query MATCHES "\n  outputs:\n")
    message(FATAL_ERROR "Cannot inspect the consumers of rc_music_test_profile_root.lib: ${error}")
endif()
string(REGEX MATCHALL "[^\n /\\]+[.]exe" consumers "${query}")
list(REMOVE_DUPLICATES consumers)
list(SORT consumers)
if(EXPECTED STREQUAL "ON")
    set(expected_consumers m0_qt_shell.exe rc_music_test_profile_root_tests.exe)
else()
    set(expected_consumers rc_music_test_profile_root_tests.exe)
endif()
if(NOT consumers STREQUAL expected_consumers)
    message(FATAL_ERROR "rc_music_test_profile_root.lib is linked by '${consumers}', expected '${expected_consumers}'")
endif()

# The built executable itself: an OFF m0_qt_shell holds no trace of the option.
if(NOT EXISTS "${SHELL_EXECUTABLE}")
    message(FATAL_ERROR "Build m0_qt_shell before this test: ${SHELL_EXECUTABLE} is missing")
endif()
file(STRINGS "${SHELL_EXECUTABLE}" narrow REGEX "--profile-root")
file(STRINGS "${SHELL_EXECUTABLE}" wide ENCODING UTF-16LE REGEX "--profile-root")
if(EXPECTED STREQUAL "OFF" AND (narrow OR wide))
    message(FATAL_ERROR "The OFF m0_qt_shell executable contains --profile-root")
endif()
if(EXPECTED STREQUAL "ON" AND NOT wide)
    message(FATAL_ERROR "The ON m0_qt_shell executable does not recognise --profile-root")
endif()
message(STATUS "RC_MUSIC_TEST_PROFILE_ROOT=${EXPECTED}: --profile-root reaches m0_qt_shell only in ON test builds")
