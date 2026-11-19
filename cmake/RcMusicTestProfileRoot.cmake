include_guard(GLOBAL)

# S13.1: `--profile-root <dir>` lets two RC Music instances run side by side on one computer, each
# with its own isolated profile, for the listening room test. Test builds only: with
# RC_MUSIC_TEST_PROFILE_ROOT=OFF (the default, and the only value runtime packaging accepts without
# an explicit opt-in) m0_qt_shell neither compiles nor links the option and treats `--profile-root`
# as any other argument. tests/cmake/TestProfileRootBuildBoundaryTests.cmake keeps that boundary.
if(NOT TARGET m0_qt_shell OR NOT TARGET m0_qt_shell_bootstrap)
    message(FATAL_ERROR "RcMusicDesktop.cmake must create m0_qt_shell before RcMusicTestProfileRoot.cmake")
endif()

# The parser also exists in BUILD_TESTING builds for its unit test; only the ON option links it
# into the product executable.
if(RC_MUSIC_TEST_PROFILE_ROOT OR BUILD_TESTING)
    add_library(rc_music_test_profile_root STATIC
        "${RC_MUSIC_SOURCE_ROOT}/src/app/TestProfileRoot.hpp"
        "${RC_MUSIC_SOURCE_ROOT}/src/app/TestProfileRoot.cpp"
    )
    target_include_directories(rc_music_test_profile_root PUBLIC "${RC_MUSIC_SOURCE_ROOT}/src/app")
    target_link_libraries(rc_music_test_profile_root PUBLIC Qt6::Core PRIVATE m0_qt_shell_bootstrap)
    rc_music_configure_cpp(rc_music_test_profile_root)
endif()

if(RC_MUSIC_TEST_PROFILE_ROOT)
    target_link_libraries(m0_qt_shell PRIVATE rc_music_test_profile_root)
    target_compile_definitions(m0_qt_shell PRIVATE RC_MUSIC_TEST_PROFILE_ROOT)
endif()

if(BUILD_TESTING)
    if(RC_MUSIC_TEST_PROFILE_ROOT)
        set(rc_music_test_profile_root_expected ON)
    else()
        set(rc_music_test_profile_root_expected OFF)
    endif()
    add_test(NAME rc_music.test-profile-root.build-boundary COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DNINJA_COMMAND=${CMAKE_MAKE_PROGRAM}"
        "-DSOURCE_ROOT=${RC_MUSIC_SOURCE_ROOT}" "-DEXPECTED=${rc_music_test_profile_root_expected}"
        "-DSHELL_EXECUTABLE=$<TARGET_FILE:m0_qt_shell>"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/TestProfileRootBuildBoundaryTests.cmake")
    set_tests_properties(rc_music.test-profile-root.build-boundary PROPERTIES
        LABELS "desktop;boundary;test-profile-root" TIMEOUT 120)
endif()
