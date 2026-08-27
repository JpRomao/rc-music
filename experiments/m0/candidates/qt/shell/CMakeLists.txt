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
    set(M0_PLACEHOLDER_ASSET_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/assets/cover-placeholder.svg")
    set(M0_TRACK_ASSET_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/shell-fixture/assets/cover-track.svg")
    foreach(input_path IN ITEMS
            M0_PROTOCOL_FILE
            M0_FIXTURE_MANIFEST_FILE
            M0_FIXTURE_FILE
            M0_QUEUE_FILE
            M0_PLACEHOLDER_ASSET_FILE
            M0_TRACK_ASSET_FILE)
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
        "${M0_PLACEHOLDER_ASSET_FILE}"
        "${M0_TRACK_ASSET_FILE}"
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
    m0_read_fixture_manifest_entry("assets/cover-placeholder.svg" m0_expected_placeholder_sha256 m0_expected_placeholder_size)
    m0_read_fixture_manifest_entry("assets/cover-track.svg" m0_expected_track_sha256 m0_expected_track_size)
    foreach(input_id IN ITEMS fixture queue)
        string(TOUPPER "${input_id}" input_id_upper)
        file(SHA256 "${M0_${input_id_upper}_FILE}" actual_sha256)
        file(SIZE "${M0_${input_id_upper}_FILE}" actual_size)
        if(NOT actual_sha256 STREQUAL m0_expected_${input_id}_sha256 OR
           NOT actual_size EQUAL m0_expected_${input_id}_size)
            message(FATAL_ERROR "${input_id}.v1.json does not match the protocol-anchored fixture manifest")
        endif()
    endforeach()
    foreach(asset_id IN ITEMS placeholder track)
        string(TOUPPER "${asset_id}" asset_id_upper)
        if(NOT EXISTS "${M0_${asset_id_upper}_ASSET_FILE}" OR IS_DIRECTORY "${M0_${asset_id_upper}_ASSET_FILE}")
            message(FATAL_ERROR "${asset_id} SVG is not a regular file")
        endif()
        file(SHA256 "${M0_${asset_id_upper}_ASSET_FILE}" actual_sha256)
        file(SIZE "${M0_${asset_id_upper}_ASSET_FILE}" actual_size)
        if(NOT actual_sha256 STREQUAL m0_expected_${asset_id}_sha256 OR
           NOT actual_size EQUAL m0_expected_${asset_id}_size)
            message(FATAL_ERROR "${asset_id} SVG does not match the authenticated manifest")
        endif()
    endforeach()

    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS
        Core
        Gui
        Qml
        Quick
        QuickControls2
        QuickLayouts
        QuickTest
        Svg
        Test
    )
    qt_standard_project_setup(REQUIRES 6.11)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")

    add_library(m0_qt_shell_support STATIC
        include/m0/adapters/fixture/FixtureRepository.hpp
        include/m0/presentation/QueueListModel.hpp
        src/adapters/fixture/FixtureDecoder.hpp
        src/adapters/fixture/FixtureRepository.cpp
        src/presentation/QueueListModel.cpp
    )
    target_include_directories(m0_qt_shell_support PUBLIC include PRIVATE src)
    target_link_libraries(m0_qt_shell_support PUBLIC m0_qt_core Qt6::Core)
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

    set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS "Main.qml")
    set_source_files_properties(qml/EmptyState.qml PROPERTIES QT_RESOURCE_ALIAS "EmptyState.qml")
    set_source_files_properties(qml/AccessibleActionButton.qml PROPERTIES QT_RESOURCE_ALIAS "AccessibleActionButton.qml")
    qt_add_library(m0_qt_shell_ui STATIC)
    qt_add_qml_module(m0_qt_shell_ui
        URI MusicPlayer.M0
        VERSION 1.0
        OUTPUT_DIRECTORY "${QT_QML_OUTPUT_DIRECTORY}/MusicPlayer/M0"
        SOURCES
            src/presentation/ShellUiContract.hpp
            src/presentation/ShellUiContract.cpp
            src/presentation/PlayerPresenter.hpp
            src/presentation/PlayerPresenter.cpp
            src/presentation/PlayerPresenterScenarioControl.hpp
        QML_FILES
            qml/Main.qml
            qml/EmptyState.qml
            qml/AccessibleActionButton.qml
            qml/AccessibleTransportButton.qml
            qml/NowPlayingState.qml
            qml/ErrorState.qml
            qml/TransportBar.qml
    )
    target_include_directories(m0_qt_shell_ui PRIVATE src src/presentation)
    target_link_libraries(m0_qt_shell_ui PRIVATE
        m0_qt_shell_support
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickLayouts
    )
    target_compile_features(m0_qt_shell_ui PUBLIC cxx_std_20)
    set_source_files_properties("${M0_FIXTURE_FILE}" PROPERTIES QT_RESOURCE_ALIAS "data/fixture.v1.json")
    set_source_files_properties("${M0_QUEUE_FILE}" PROPERTIES QT_RESOURCE_ALIAS "data/queue.v1.json")
    set_source_files_properties("${M0_PLACEHOLDER_ASSET_FILE}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/cover-placeholder.svg"
    )
    set_source_files_properties("${M0_TRACK_ASSET_FILE}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/cover-track.svg"
    )
    qt_add_resources(m0_qt_shell_ui m0_qt_shell_assets
        PREFIX "/m0"
        FILES
            "${M0_FIXTURE_FILE}"
            "${M0_QUEUE_FILE}"
            "${M0_PLACEHOLDER_ASSET_FILE}"
            "${M0_TRACK_ASSET_FILE}"
    )
    if(MSVC)
        target_compile_options(m0_qt_shell_ui PRIVATE /W4 /WX /permissive- /wd4702)
        target_compile_options(m0_qt_shell_uiplugin PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(m0_qt_shell_ui PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(m0_qt_shell_uiplugin PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_library(m0_qt_shell_bootstrap STATIC
        app/ShellLaunchOptions.hpp
        app/ShellLaunchOptions.cpp
    )
    target_include_directories(m0_qt_shell_bootstrap PUBLIC app)
    target_link_libraries(m0_qt_shell_bootstrap PUBLIC Qt6::Core)
    target_compile_features(m0_qt_shell_bootstrap PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(m0_qt_shell_bootstrap PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(m0_qt_shell_bootstrap PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    qt_add_executable(m0_qt_shell app/main.cpp)
    target_link_libraries(m0_qt_shell PRIVATE
        m0_qt_shell_bootstrap
        m0_qt_shell_support
        m0_qt_shell_ui
        m0_qt_shell_uiplugin
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
    )
    target_include_directories(m0_qt_shell PRIVATE src)
    target_compile_features(m0_qt_shell PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(m0_qt_shell PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(m0_qt_shell PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(BUILD_TESTING)
        target_compile_definitions(m0_qt_shell PRIVATE M0_QT_SHELL_TEST_HOOKS)
        add_executable(m0_qt_shell_launch_tests tests/qtest/ShellLaunchOptionsTests.cpp)
        target_link_libraries(m0_qt_shell_launch_tests PRIVATE m0_qt_shell_bootstrap Qt6::Test)
        target_compile_features(m0_qt_shell_launch_tests PRIVATE cxx_std_20)
        set_target_properties(m0_qt_shell_launch_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_launch_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_launch_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.shell-launch-options COMMAND m0_qt_shell_launch_tests)
        add_executable(m0_qt_fixture_tests
            tests/qtest/FixtureRepositoryTests.cpp
        )
        target_link_libraries(m0_qt_fixture_tests PRIVATE m0_qt_shell_support Qt6::Test)
        target_include_directories(m0_qt_fixture_tests PRIVATE src)
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
        add_executable(m0_qt_presenter_tests tests/qtest/PlayerPresenterTests.cpp)
        target_link_libraries(m0_qt_presenter_tests PRIVATE m0_qt_shell_support m0_qt_shell_ui Qt6::Test)
        target_include_directories(m0_qt_presenter_tests PRIVATE src)
        target_compile_features(m0_qt_presenter_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_presenter_tests PRIVATE
            M0_TEST_FIXTURE_FILE="${M0_FIXTURE_FILE}"
            M0_TEST_QUEUE_FILE="${M0_QUEUE_FILE}"
        )
        set_target_properties(m0_qt_presenter_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_presenter_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_presenter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.presenter.initial-load COMMAND m0_qt_presenter_tests)
        add_executable(m0_qt_shell_contract_tests tests/qtest/ShellUiContractTests.cpp)
        target_link_libraries(m0_qt_shell_contract_tests PRIVATE
            m0_qt_shell_support
            m0_qt_shell_ui
            m0_qt_shell_uiplugin
            Qt6::Test
        )
        target_include_directories(m0_qt_shell_contract_tests PRIVATE src)
        target_compile_definitions(m0_qt_shell_contract_tests PRIVATE
            M0_FIXTURE_SHA256="${m0_expected_fixture_sha256}"
            M0_FIXTURE_SIZE=${m0_expected_fixture_size}
            M0_QUEUE_SHA256="${m0_expected_queue_sha256}"
            M0_QUEUE_SIZE=${m0_expected_queue_size}
            M0_PLACEHOLDER_SHA256="${m0_expected_placeholder_sha256}"
            M0_PLACEHOLDER_SIZE=${m0_expected_placeholder_size}
            M0_TRACK_SHA256="${m0_expected_track_sha256}"
            M0_TRACK_SIZE=${m0_expected_track_size}
        )
        set_target_properties(m0_qt_shell_contract_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_shell_contract_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_shell_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.shell-ui-contract COMMAND m0_qt_shell_contract_tests)
        add_executable(m0_qt_quick_tests tests/quick/quick_main.cpp)
        target_link_libraries(m0_qt_quick_tests PRIVATE
            m0_qt_shell_support
            m0_qt_shell_ui
            m0_qt_shell_uiplugin
            Qt6::QuickControls2
            Qt6::QuickTest
        )
        target_include_directories(m0_qt_quick_tests PRIVATE src)
        target_compile_features(m0_qt_quick_tests PRIVATE cxx_std_20)
        target_compile_definitions(m0_qt_quick_tests PRIVATE
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/quick"
        )
        set_target_properties(m0_qt_quick_tests PROPERTIES AUTOMOC ON)
        if(MSVC)
            target_compile_options(m0_qt_quick_tests PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options(m0_qt_quick_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME m0.qt.quick.shell-states COMMAND m0_qt_quick_tests
            -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/quick"
            -import "${QT_QML_OUTPUT_DIRECTORY}"
        )
        get_target_property(m0_qt_core_runtime Qt6::Core IMPORTED_LOCATION_RELEASE)
        get_filename_component(m0_qt_runtime_dir "${m0_qt_core_runtime}" DIRECTORY)
        set_tests_properties(m0.qt.fixture.repository-and-model PROPERTIES
            LABELS "m0;qt;fixture;model"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.shell-launch-options PROPERTIES
            LABELS "m0;qt;shell-launch"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set_tests_properties(m0.qt.presenter.initial-load PROPERTIES
            LABELS "m0;qt;presenter"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
        )
        set(m0_qt_quick_test_environment
            "QT_QPA_PLATFORM=offscreen"
            "QSG_RHI_BACKEND=software"
            "QT_QUICK_CONTROLS_STYLE=Basic"
        )
        if(WIN32 AND IS_DIRECTORY "$ENV{WINDIR}/Fonts")
            cmake_path(CONVERT "$ENV{WINDIR}/Fonts" TO_CMAKE_PATH_LIST m0_windows_font_dir NORMALIZE)
            list(APPEND m0_qt_quick_test_environment "QT_QPA_FONTDIR=${m0_windows_font_dir}")
        endif()
        add_test(NAME m0.qt.shell-executable.error-recovery COMMAND m0_qt_shell
            --m0-scenario=unavailable
            --m0-smoke=error-recovery
        )
        set_tests_properties(m0.qt.shell-executable.error-recovery PROPERTIES
            LABELS "m0;qt;shell-executable;error-recovery"
            TIMEOUT 5
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.quick.shell-states PROPERTIES
            LABELS "m0;qt;quick;shell-states"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${m0_qt_runtime_dir}"
            ENVIRONMENT "${m0_qt_quick_test_environment}"
        )
        set_tests_properties(m0.qt.shell-ui-contract PROPERTIES
            LABELS "m0;qt;shell-ui-contract"
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
