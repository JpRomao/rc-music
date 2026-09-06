cmake_minimum_required(VERSION 3.31)
# Read-only probes: never rewrite or delete the product or historical corpus.
foreach(case IN ITEMS valid empty size hash missing directory)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}" -P
        "${CMAKE_CURRENT_LIST_DIR}/AudioFixtureProbe.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(case STREQUAL "valid" OR case STREQUAL "empty")
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Valid audio fixture rejected (${case}): ${output}${error}")
        endif()
    else()
        if(status STREQUAL "0" OR NOT "${output}${error}" MATCHES "Audio fixture ${case}")
            message(FATAL_ERROR "Wrong audio fixture rejection (${case}): ${status} ${output}${error}")
        endif()
    endif()
endforeach()
message(STATUS "Audio fixture authentication: 6 cases passed")
