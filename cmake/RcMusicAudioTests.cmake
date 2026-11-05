include_guard(GLOBAL)

# The caller authenticates its corpus before registering these tests. Keeping
# the existing compile-time path names lets both entry points share the suites
# without bringing historical fixture bundles into the product application.
function(rc_music_add_audio_tests corpus_root)
    if(NOT BUILD_TESTING)
        return()
    endif()
    find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Multimedia Quick Test Widgets)
    find_package(TagLib 2.3.1 EXACT CONFIG REQUIRED)

    set(audio_common_definitions
        M0_TEST_AUDIO_WAV_FILE="${corpus_root}/fixtures/valid/wav-pcm-mono.wav"
        M0_TEST_AUDIO_MP3_FILE="${corpus_root}/fixtures/valid/mp3-cbr-stereo.mp3"
        M0_TEST_AUDIO_MP3_VBR_FILE="${corpus_root}/fixtures/valid/mp3-vbr-stereo.mp3"
        M0_TEST_AUDIO_MP3_UNICODE_FILE="${corpus_root}/fixtures/valid/mp3-unicode-tags.mp3"
        M0_TEST_AUDIO_M4A_AAC_FILE="${corpus_root}/fixtures/valid/m4a-aac-lc.m4a"
        M0_TEST_AUDIO_FLAC_FILE="${corpus_root}/fixtures/valid/flac-16-stereo.flac"
        M0_TEST_AUDIO_OGG_VORBIS_FILE="${corpus_root}/fixtures/valid/ogg-vorbis.ogg"
        M0_TEST_AUDIO_TRUNCATED_WAV_FILE="${corpus_root}/fixtures/hostile/truncated-wav-pcm-mono.wav"
    )
    get_target_property(audio_runtime Qt6::Multimedia IMPORTED_LOCATION_RELEASE)
    get_target_property(audio_core_runtime Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_target_property(audio_gui_runtime Qt6::Gui IMPORTED_LOCATION_RELEASE)
    get_filename_component(audio_runtime_dir "${audio_runtime}" DIRECTORY)
    get_filename_component(audio_core_runtime_dir "${audio_core_runtime}" DIRECTORY)
    get_filename_component(audio_gui_runtime_dir "${audio_gui_runtime}" DIRECTORY)
    get_filename_component(audio_prefix "${audio_runtime_dir}" DIRECTORY)
    set(audio_environment "QT_MEDIA_BACKEND=ffmpeg;QT_PLUGIN_PATH=${audio_prefix}/plugins")
    set(audio_path_environment
        "PATH=path_list_prepend:${audio_runtime_dir};PATH=path_list_prepend:${audio_core_runtime_dir}")

    _rc_music_add_desktop_test(rc_music_audio_adapter_tests
        rc_music.audio.playback-lifecycle QtMultimediaPlaybackBackendTests.cpp m0_qt_audio_adapters)
    target_compile_definitions(rc_music_audio_adapter_tests PRIVATE
        ${audio_common_definitions}
        M0_TEST_AUDIO_EMPTY_FILE="${corpus_root}/fixtures/hostile/empty.bin"
        M0_TEST_AUDIO_WRONG_EXTENSION_FILE="${corpus_root}/fixtures/edge/wrong-extension-wav-as-mp3.mp3"
    )
    set_tests_properties(rc_music.audio.playback-lifecycle PROPERTIES
        LABELS "desktop;qt;audio;PB-001;PB-002;PB-003;PB-004;PB-006;PB-007;PB-008;PB-009;PB-010;PB-011;PB-012;PB-101;PB-102;PB-103"
        TIMEOUT 120
        ENVIRONMENT_MODIFICATION "${audio_path_environment}"
        ENVIRONMENT "${audio_environment}"
    )

    _rc_music_add_desktop_test(rc_music_local_audio_integration_tests
        rc_music.audio.local-integration LocalAudioIntegrationTests.cpp
        m0_qt_shell_support m0_qt_shell_ui m0_qt_audio_adapters m0_taglib_metadata_extractor
        Qt6::Gui Qt6::Widgets TagLib::TagLib)
    target_compile_definitions(rc_music_local_audio_integration_tests PRIVATE ${audio_common_definitions})
    add_custom_command(TARGET rc_music_local_audio_integration_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:TagLib::tag>"
            "$<TARGET_FILE_DIR:rc_music_local_audio_integration_tests>"
    )
    set_tests_properties(rc_music.audio.local-integration PROPERTIES
        LABELS "desktop;qt;audio;integration;local"
        TIMEOUT 180
        ENVIRONMENT_MODIFICATION "${audio_path_environment}"
        ENVIRONMENT "${audio_environment};QT_QPA_PLATFORM=offscreen"
    )

    add_executable(rc_music_taglib_metadata_extractor_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/audio/TagLibMetadataExtractorTests.cpp")
    target_link_libraries(rc_music_taglib_metadata_extractor_tests PRIVATE
        m0_qt_core m0_taglib_metadata_extractor TagLib::TagLib Catch2::Catch2WithMain Qt6::Gui)
    rc_music_configure_cpp(rc_music_taglib_metadata_extractor_tests)
    target_compile_definitions(rc_music_taglib_metadata_extractor_tests PRIVATE
        M0_TEST_AUDIO_CORPUS_ROOT="${corpus_root}"
        M0_TEST_AUDIO_PRODUCT_CORPUS_ROOT="${corpus_root}")
    add_custom_command(TARGET rc_music_taglib_metadata_extractor_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:TagLib::tag>"
            "$<TARGET_FILE_DIR:rc_music_taglib_metadata_extractor_tests>"
    )
    catch_discover_tests(rc_music_taglib_metadata_extractor_tests
        TEST_PREFIX "rc_music.metadata."
        # Catch's discovery flattens a semicolon list in PROPERTIES. Use one
        # selection label; the prefix preserves the metadata category.
        PROPERTIES LABELS "audio" TIMEOUT 30
        DL_PATHS "${audio_runtime_dir}" "${audio_gui_runtime_dir}"
    )
    add_test(NAME rc_music.audio.test-registration COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/AudioRegistrationTests.cmake")
    set_tests_properties(rc_music.audio.test-registration PROPERTIES LABELS "audio" TIMEOUT 15)
    add_test(NAME rc_music.audio.registration-regression COMMAND "${CMAKE_COMMAND}"
        "-DSCRATCH_DIRECTORY=${CMAKE_BINARY_DIR}/tests/cmake" "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
        -P "${RC_MUSIC_SOURCE_ROOT}/tests/cmake/AudioRegistrationRegressionTests.cmake")
    set_tests_properties(rc_music.audio.registration-regression PROPERTIES LABELS "audio;boundary" TIMEOUT 30)
endfunction()
