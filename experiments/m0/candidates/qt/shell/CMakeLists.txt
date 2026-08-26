cmake_minimum_required(VERSION 3.31)

project(m0_qt_shell_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(M0_QT_SHELL_SUPPORT "Build the Qt fixture repository and queue model" OFF)

set(M0_CATCH2_SOURCE_DIR "" CACHE PATH "Path to the authenticated Catch2 source tree")
if(NOT IS_ABSOLUTE "${M0_CATCH2_SOURCE_DIR}")
    message(FATAL_ERROR "M0_CATCH2_SOURCE_DIR must be an absolute path")
endif()

if(M0_QT_SHELL_SUPPORT)
    set(M0_PROTOCOL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../protocol/protocol.v1.json")
    set(M0_FIXTURE_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/manifest.v1.json")
    set(M0_FIXTURE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/fixture.v1.json")
    set(M0_QUEUE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/queue.v1.json")
    foreach(input_path IN ITEMS M0_PROTOCOL_FILE M0_FIXTURE_MANIFEST_FILE M0_FIXTURE_FILE M0_QUEUE_FILE)
        cmake_path(NORMAL_PATH ${input_path})
        if(NOT EXISTS "${${input_path}}")
            message(FATAL_ERROR "${input_path} does not exist")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${M0_PROTOCOL_FILE}"
        "${M0_FIXTURE_MANIFEST_FILE}"
        "${M0_FIXTURE_FILE}"
        "${M0_QUEUE_FILE}"
    )

    file(READ "${M0_PROTOCOL_FILE}" m0_protocol_json)
    string(JSON m0_expected_manifest_sha256 GET "${m0_protocol_json}" fixture_manifest_sha256)
    string(TOLOWER "${m0_expected_manifest_sha256}" m0_expected_manifest_sha256)
    file(SHA256 "${M0_FIXTURE_MANIFEST_FILE}" m0_actual_manifest_sha256)
    if(NOT m0_actual_manifest_sha256 STREQUAL m0_expected_manifest_sha256)
        message(FATAL_ERROR "The frozen fixture manifest is not anchored by protocol.v1.json")
    endif()

    file(READ "${M0_FIXTURE_MANIFEST_FILE}" m0_fixture_manifest_json)
    string(JSON m0_fixture_manifest_file_count LENGTH "${m0_fixture_manifest_json}" files)
    math(EXPR m0_fixture_manifest_last_index "${m0_fixture_manifest_file_count} - 1")
    function(m0_read_fixture_manifest_entry relative_path output_sha256 output_size)
        foreach(index RANGE 0 ${m0_fixture_manifest_last_index})
            string(JSON candidate_relative_path GET "${m0_fixture_manifest_json}" files ${index} relative_path)
            if(candidate_relative_path STREQUAL relative_path)
                string(JSON expected_sha256 GET "${m0_fixture_manifest_json}" files ${index} sha256)
                string(JSON expected_size GET "${m0_fixture_manifest_json}" files ${index} size_bytes)
                set(${output_sha256} "${expected_sha256}" PARENT_SCOPE)
                set(${output_size} "${expected_size}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
        message(FATAL_ERROR "The frozen fixture manifest does not contain ${relative_path}")
    endfunction()

    m0_read_fixture_manifest_entry("fixture.v1.json" m0_expected_fixture_sha256 m0_expected_fixture_size)
    m0_read_fixture_manifest_entry("queue.v1.json" m0_expected_queue_sha256 m0_expected_queue_size)
    foreach(input_id IN ITEMS fixture queue)
        string(TOUPPER "${input_id}" input_id_upper)
        file(SHA256 "${M0_${input_id_upper}_FILE}" actual_sha256)
        file(SIZE "${M0_${input_id_upper}_FILE}" actual_size)
        if(NOT actual_sha256 STREQUAL m0_expected_${input_id}_sha256 OR
           NOT actual_size EQUAL m0_expected_${input_id}_size)
            message(FATAL_ERROR "${input_id}.v1.json does not match the protocol-anchored fixture manifest")
        endif()
    endforeach()

    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Test)

    add_library(m0_qt_shell_support STATIC
        include/m0/adapters/fixture/FixtureRepository.hpp
        include/m0/presentation/QueueListModel.hpp
        src/adapters/fixture/FixtureRepository.cpp
        src/presentation/QueueListModel.cpp
    )
    target_include_directories(m0_qt_shell_support PUBLIC include)
    target_link_libraries(m0_qt_shell_support PUBLIC Qt6::Core)
    target_compile_features(m0_qt_shell_support PUBLIC cxx_std_20)
    target_compile_definitions(m0_qt_shell_support PRIVATE
        M0_FROZEN_FIXTURE_SHA256="${m0_expected_fixture_sha256}"
        M0_FROZEN_QUEUE_SHA256="${m0_expected_queue_sha256}"
    )
    set_target_properties(m0_qt_shell_support PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(m0_qt_shell_support PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(m0_qt_shell_support PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(BUILD_TESTING)
        add_executable(m0_qt_fixture_tests
            tests/qtest/FixtureRepositoryTests.cpp
        )
        target_link_libraries(m0_qt_fixture_tests PRIVATE m0_qt_shell_support Qt6::Test)
        target_compile_features(m0_qt_fixture_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_fixture_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_fixture_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_fixture_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_fixture_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.fixture.repository-and-model COMMAND m0_qt_fixture_tests)
        get_target_property(m0_qt_core_runtime Qt6::Core IMPORTED_LOCATION_RELEASE)
        get_filename_component(m0_qt_runtime_dir "${m0_qt_core_runtime}" DIRECTORY)
        set_tests_properties(m0.qt.fixture.repository-and-model PROPERTIES
            LABELS "m0;qt;fixture;model"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
    endif()
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
    src/adapters/DeterministicPlaybackBackend.cpp
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
        tests/catch2/DeterministicPlaybackBackendTests.cpp
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
