cmake_minimum_required(VERSION 3.31)

project(RCMusic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/RcMusicCore.cmake)

if(BUILD_TESTING)
    set(RC_MUSIC_CATCH2_SOURCE_DIR "" CACHE PATH "Path to the preflight-verified Catch2 3.15.3 source tree")
    include(cmake/RcMusicTests.cmake)
    rc_music_prepare_catch2("${RC_MUSIC_CATCH2_SOURCE_DIR}")
    rc_music_add_core_tests(rc_music_core_tests "rc_music.core." "core")
endif()
