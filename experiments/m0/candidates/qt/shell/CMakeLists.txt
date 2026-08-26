cmake_minimum_required(VERSION 3.31)

project(m0_qt_shell_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(M0_CATCH2_SOURCE_DIR "" CACHE PATH "Path to the authenticated Catch2 source tree")
if(NOT IS_ABSOLUTE "${M0_CATCH2_SOURCE_DIR}")
    message(FATAL_ERROR "M0_CATCH2_SOURCE_DIR must be an absolute path")
endif()
if(NOT EXISTS "${M0_CATCH2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "M0_CATCH2_SOURCE_DIR does not contain Catch2")
endif()

set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${M0_CATCH2_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/catch2-build"
    EXCLUDE_FROM_ALL
)

add_library(m0_qt_core STATIC
    src/application/PlayerController.cpp
)
target_include_directories(m0_qt_core PUBLIC include)
target_compile_features(m0_qt_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(m0_qt_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(m0_qt_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_executable(m0_qt_core_tests
        tests/catch2/PlayerControllerTests.cpp
    )
    target_link_libraries(m0_qt_core_tests PRIVATE m0_qt_core Catch2::Catch2WithMain)
    target_compile_features(m0_qt_core_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(m0_qt_core_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(m0_qt_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    include("${M0_CATCH2_SOURCE_DIR}/extras/Catch.cmake")
    catch_discover_tests(
        m0_qt_core_tests
        TEST_PREFIX "m0.qt.core."
        PROPERTIES LABELS "m0;qt;core"
    )
endif()
