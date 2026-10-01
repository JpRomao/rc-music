cmake_minimum_required(VERSION 3.22)
project(RcMusicRestrictedTls VERSION 1.0 LANGUAGES CXX)
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Network CorePrivate NetworkPrivate)
qt_add_plugin(qrcmusicschanneloffline SHARED PLUGIN_TYPE tls CLASS_NAME RcMusicSchannelBackend
    schannel/qtls_schannel.cpp schannel/qtls_schannel_p.h schannel/qtlsbackend_schannel_p.h
    schannel/qtlskey_schannel.cpp schannel/qtlskey_schannel_p.h
    schannel/qx509_schannel.cpp schannel/qx509_schannel_p.h
    shared/qtlskey_base.cpp shared/qtlskey_base_p.h shared/qtlskey_generic.cpp shared/qtlskey_generic_p.h
    shared/qx509_base.cpp shared/qx509_base_p.h shared/qx509_generic.cpp shared/qx509_generic_p.h
    shared/qsslsocket_qt.cpp shared/qwincrypt_p.h shared/qasn1element.cpp shared/qasn1element_p.h)
set_target_properties(qrcmusicschanneloffline PROPERTIES AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/tls"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/tls")
target_compile_features(qrcmusicschanneloffline PRIVATE cxx_std_20)
target_compile_definitions(qrcmusicschanneloffline PRIVATE
    "QT_ENABLE_STRICT_MODE_UP_TO=QT_VERSION_CHECK(6,11,0)" QT_NO_CAST_FROM_ASCII)
target_link_libraries(qrcmusicschanneloffline PRIVATE Qt6::CorePrivate Qt6::NetworkPrivate
    crypt32 secur32 bcrypt ncrypt)
if(MSVC)
    # Qt's Q_UNREACHABLE() deliberately precedes fallback return statements.
    # C4702 fires in those original Qt headers/sources under /O2; retain all
    # other /W4 diagnostics and errors rather than editing upstream fallbacks.
    target_compile_options(qrcmusicschanneloffline PRIVATE /W4 /WX /wd4702 /permissive-)
    # The upstream PKCS#12 parser deliberately shadows p/v in an inner scope.
    set_source_files_properties(shared/qsslsocket_qt.cpp PROPERTIES COMPILE_OPTIONS /wd4456)
endif()
enable_testing()
add_executable(rc_music_tls_input_limits_tests tests/TlsInputLimitsTests.cpp)
target_compile_features(rc_music_tls_input_limits_tests PRIVATE cxx_std_20)
target_include_directories(rc_music_tls_input_limits_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME rc_music.download-tls.input-limits COMMAND rc_music_tls_input_limits_tests)
