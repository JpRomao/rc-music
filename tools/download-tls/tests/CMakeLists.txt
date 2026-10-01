cmake_minimum_required(VERSION 3.22)
project(RcMusicRestrictedTlsHandshakeTests LANGUAGES CXX)
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Network Test)
get_filename_component(RC_MUSIC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(rc_music_restricted_tls_tests RestrictedTlsTests.cpp
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/PinnedDownloadStream.cpp")
set_target_properties(rc_music_restricted_tls_tests PROPERTIES AUTOMOC ON)
target_compile_features(rc_music_restricted_tls_tests PRIVATE cxx_std_20)
target_include_directories(rc_music_restricted_tls_tests PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
target_link_libraries(rc_music_restricted_tls_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
if(MSVC)
    target_compile_options(rc_music_restricted_tls_tests PRIVATE /W4 /WX /permissive-)
endif()
enable_testing()
add_test(NAME rc_music.download-tls.handshake COMMAND rc_music_restricted_tls_tests -o -,txt)
set_tests_properties(rc_music.download-tls.handshake PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
