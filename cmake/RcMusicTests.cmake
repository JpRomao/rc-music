include_guard(GLOBAL)

function(rc_music_prepare_catch2 source_dir)
    if(NOT IS_ABSOLUTE "${source_dir}" OR NOT EXISTS "${source_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Tests require an absolute path to the preflight-verified Catch2 3.15.3 source tree")
    endif()
    if(NOT TARGET Catch2::Catch2WithMain)
        set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${source_dir}" "${CMAKE_BINARY_DIR}/_deps/catch2-build" EXCLUDE_FROM_ALL)
    endif()
    get_target_property(catch2_version Catch2::Catch2 VERSION)
    if(NOT catch2_version VERSION_EQUAL "3.15.3")
        message(FATAL_ERROR "The core tests require Catch2 3.15.3; found ${catch2_version}")
    endif()
    include("${source_dir}/extras/Catch.cmake")
endfunction()

function(rc_music_add_core_tests target test_prefix test_labels)
    rc_music_add_test_support()
    add_executable(${target}
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/LyricsVariantsTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/AcquiredLyricsTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/PlaylistLibraryTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/ListenerProfileTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/RoomClockTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/RoomInviteLinkTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/RoomRosterTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/ListeningRoomTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/FakeRoomTransportTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/ListeningRoomHostTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/ListeningRoomGuestTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/RoomPlaybackFollowerTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/LocalLibraryTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/LyricsTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/ArtworkLimitsTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/DeterministicPlaybackBackendTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/LocalAudioFilePolicyTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/LocalFileQueueTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/MetadataSanitizationTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/DiagnosticLogTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SingleWorkerLocalSourceDiscoveryTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/LocalTrackSessionTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/PlayerControllerTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/PreferencesAutosaveTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/QueueNavigationTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SessionAutosaveTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SessionStorePortTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SessionSnapshotTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SingleWorkerMetadataReaderTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SingleWorkerPreferencesWriterTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/SingleWorkerSessionWriterTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/core/WindowPlacementTests.cpp"
    )
    target_link_libraries(${target} PRIVATE
        rc_music_core rc_music_workers rc_music_test_support Catch2::Catch2WithMain
    )
    rc_music_configure_cpp(${target})
    catch_discover_tests(${target}
        TEST_PREFIX "${test_prefix}"
        PROPERTIES LABELS "${test_labels}" TIMEOUT 15
    )
endfunction()
