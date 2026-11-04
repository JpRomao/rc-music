include_guard(GLOBAL)

if(NOT WIN32 OR NOT TARGET m0_qt_shell)
    message(FATAL_ERROR "The optional embedded browser requires the Windows desktop target")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The experimental WebView2 composition currently requires x64")
endif()
set(RC_MUSIC_WEBVIEW2_SDK_ROOT "" CACHE PATH "Explicit local WebView2 SDK root; never fetched or installed")
if(NOT IS_ABSOLUTE "${RC_MUSIC_WEBVIEW2_SDK_ROOT}")
    message(FATAL_ERROR "RC_MUSIC_WEBVIEW2_SDK_ROOT must be an explicit absolute SDK path")
endif()
foreach(input IN ITEMS build/native/include/WebView2.h build/native/include/WebView2EnvironmentOptions.h
        build/native/x64/WebView2LoaderStatic.lib LICENSE.txt NOTICE.txt)
    if(NOT EXISTS "${RC_MUSIC_WEBVIEW2_SDK_ROOT}/${input}")
        message(FATAL_ERROR "The explicit WebView2 SDK is missing ${input}")
    endif()
endforeach()

add_library(rc_music_embedded_browser_binding STATIC
    "${RC_MUSIC_SOURCE_ROOT}/include/m0/ports/EmbeddedBrowserWindow.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/BrowserNavigationPolicy.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/BrowserNavigationPolicy.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/EmbeddedBrowserBinding.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/presentation/EmbeddedBrowserBinding.cpp")
target_include_directories(rc_music_embedded_browser_binding PUBLIC
    "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/include")
target_link_libraries(rc_music_embedded_browser_binding PUBLIC Qt6::Core)
set_target_properties(rc_music_embedded_browser_binding PROPERTIES AUTOMOC ON)
rc_music_configure_cpp(rc_music_embedded_browser_binding)

add_library(rc_music_webview2_loader STATIC IMPORTED)
set_target_properties(rc_music_webview2_loader PROPERTIES IMPORTED_LOCATION
    "${RC_MUSIC_WEBVIEW2_SDK_ROOT}/build/native/x64/WebView2LoaderStatic.lib")
add_library(rc_music_embedded_browser STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsWebView2Browser.hpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsWebView2Browser.cpp")
target_include_directories(rc_music_embedded_browser SYSTEM PRIVATE
    "${RC_MUSIC_WEBVIEW2_SDK_ROOT}/build/native/include")
target_link_libraries(rc_music_embedded_browser PUBLIC rc_music_embedded_browser_binding
    PRIVATE Qt6::Widgets rc_music_webview2_loader ole32 user32 shlwapi version)
rc_music_configure_cpp(rc_music_embedded_browser)
target_link_libraries(m0_qt_shell PRIVATE rc_music_embedded_browser)
target_compile_definitions(m0_qt_shell PRIVATE RC_MUSIC_EMBEDDED_BROWSER_AVAILABLE)

if(BUILD_TESTING)
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)
    foreach(unit IN ITEMS BrowserNavigationPolicy EmbeddedBrowserBinding WindowsWebView2Browser)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_link_libraries(${target} PRIVATE rc_music_embedded_browser_binding Qt6::Test)
        if(unit STREQUAL "WindowsWebView2Browser")
            target_link_libraries(${target} PRIVATE rc_music_embedded_browser)
        endif()
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        add_test(NAME "rc_music.embedded-browser.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.embedded-browser.${unit}" PROPERTIES
            LABELS "embedded-browser;qt;FR-051;FR-053" TIMEOUT 30
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endforeach()
endif()
