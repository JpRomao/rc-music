include_guard(GLOBAL)

# This component belongs only to the opt-in Windows download composition.
# The native worker never loads Qt or a separately distributed converter CLI.
add_library(rc_music_media_foundation_protocol STATIC
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/MediaFoundationWorkerProtocol.cpp")
target_include_directories(rc_music_media_foundation_protocol PUBLIC
    "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
rc_music_configure_cpp(rc_music_media_foundation_protocol)

# MF entry points are resolved explicitly from System32 by the codec. Missing
# Windows media components produce a typed result instead of loader failure.
set(rc_music_media_foundation_libraries mfuuid wmcodecdspuuid ole32)
add_library(rc_music_media_foundation_codec STATIC EXCLUDE_FROM_ALL
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationCodec.cpp")
target_link_libraries(rc_music_media_foundation_codec PUBLIC rc_music_media_foundation_protocol
    PRIVATE ${rc_music_media_foundation_libraries})
rc_music_configure_cpp(rc_music_media_foundation_codec)

add_executable(rc_music_media_foundation_worker
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/MediaFoundationWorkerProtocol.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationCodec.cpp"
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationWorkerMain.cpp")
target_include_directories(rc_music_media_foundation_worker PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src" "${RC_MUSIC_SOURCE_ROOT}/src/adapters")
target_link_libraries(rc_music_media_foundation_worker PRIVATE ${rc_music_media_foundation_libraries})
set_target_properties(rc_music_media_foundation_worker PROPERTIES
    OUTPUT_NAME "RCMusicMediaWorker" MSVC_RUNTIME_LIBRARY "MultiThreaded")
rc_music_configure_cpp(rc_music_media_foundation_worker)
target_link_libraries(rc_music_download_network PRIVATE rc_music_media_foundation_protocol)
target_sources(rc_music_download_network PRIVATE
    "${RC_MUSIC_SOURCE_ROOT}/src/adapters/WindowsMediaFoundationLaunch.cpp")

if(BUILD_TESTING)
    add_executable(rc_music_AppContainerRegistryNative_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerRegistryNativeTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerRegistryNative.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSecurity.cpp")
    target_include_directories(rc_music_AppContainerRegistryNative_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerRegistryNative_tests PRIVATE Qt6::Core Qt6::Test advapi32 ntdll)
    set_target_properties(rc_music_AppContainerRegistryNative_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerRegistryNative_tests)
    add_test(NAME rc_music.profile-storage.AppContainerRegistryNative
        COMMAND rc_music_AppContainerRegistryNative_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerRegistryNative PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_AppContainerStorageRegistry_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageRegistryTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageRegistry.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSecurity.cpp")
    target_include_directories(rc_music_AppContainerStorageRegistry_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageRegistry_tests PRIVATE Qt6::Core Qt6::Test advapi32)
    set_target_properties(rc_music_AppContainerStorageRegistry_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageRegistry_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageRegistry
        COMMAND rc_music_AppContainerStorageRegistry_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageRegistry PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_AppContainerStorageNativeSession_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageNativeSessionTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageNativeSession.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSession.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageLiveSource.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageFiles.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageRegistry.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerRegistryNative.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSecurity.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageInventory.cpp")
    target_include_directories(rc_music_AppContainerStorageNativeSession_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageNativeSession_tests PRIVATE
        rc_music_download_documents Qt6::Test advapi32 ntdll)
    set_target_properties(rc_music_AppContainerStorageNativeSession_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageNativeSession_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageNativeSession
        COMMAND rc_music_AppContainerStorageNativeSession_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageNativeSession PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_registry_readonly_preflight
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerRegistryReadOnlyPreflight.cpp")
    target_link_libraries(rc_music_registry_readonly_preflight PRIVATE advapi32 ntdll)
    rc_music_configure_cpp(rc_music_registry_readonly_preflight)
    add_test(NAME rc_music.profile-storage.RegistryReadOnlyPreflightParser
        COMMAND rc_music_registry_readonly_preflight --self-test)
    set_tests_properties(rc_music.profile-storage.RegistryReadOnlyPreflightParser PROPERTIES
        LABELS "profile-storage;security;parser;windows" TIMEOUT 15)
    add_executable(rc_music_AppContainerStorageSession_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageSessionTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSession.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageInventory.cpp")
    target_include_directories(rc_music_AppContainerStorageSession_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageSession_tests PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(rc_music_AppContainerStorageSession_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageSession_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageSession
        COMMAND rc_music_AppContainerStorageSession_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageSession PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_AppContainerStorageFiles_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageFilesTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageFiles.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSecurity.cpp")
    target_include_directories(rc_music_AppContainerStorageFiles_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageFiles_tests PRIVATE Qt6::Core Qt6::Test advapi32 ntdll)
    set_target_properties(rc_music_AppContainerStorageFiles_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageFiles_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageFiles
        COMMAND rc_music_AppContainerStorageFiles_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageFiles PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(rc_music_AppContainerStorageSecurity_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageSecurityTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageSecurity.cpp")
    target_include_directories(rc_music_AppContainerStorageSecurity_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageSecurity_tests PRIVATE Qt6::Core Qt6::Test advapi32)
    set_target_properties(rc_music_AppContainerStorageSecurity_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageSecurity_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageSecurity
        COMMAND rc_music_AppContainerStorageSecurity_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageSecurity PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    # Test-only live-owner bridge. No dependency on the native probe/helper:
    # those executables remain frozen for their separately authorized run.
    add_executable(rc_music_AppContainerStorageLiveSource_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageLiveSourceTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageLiveSource.cpp")
    target_include_directories(rc_music_AppContainerStorageLiveSource_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageLiveSource_tests PRIVATE rc_music_download_documents Qt6::Test)
    set_target_properties(rc_music_AppContainerStorageLiveSource_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageLiveSource_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageLiveSource
        COMMAND rc_music_AppContainerStorageLiveSource_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageLiveSource PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    # Pure storage-inventory seam. Keep it independent of the native probe and
    # media/network libraries while their separate physical run is pending.
    add_executable(rc_music_AppContainerStorageInventory_tests
        "${RC_MUSIC_SOURCE_ROOT}/tests/qt/AppContainerStorageInventoryTests.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerStorageInventory.cpp")
    target_include_directories(rc_music_AppContainerStorageInventory_tests PRIVATE
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
    target_link_libraries(rc_music_AppContainerStorageInventory_tests PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(rc_music_AppContainerStorageInventory_tests PROPERTIES AUTOMOC ON)
    rc_music_configure_cpp(rc_music_AppContainerStorageInventory_tests)
    add_test(NAME rc_music.profile-storage.AppContainerStorageInventory
        COMMAND rc_music_AppContainerStorageInventory_tests -o -,txt)
    set_tests_properties(rc_music.profile-storage.AppContainerStorageInventory PROPERTIES
        LABELS "profile-storage;security;qt;windows;FR-051;FR-053" TIMEOUT 60
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    # Only --self-test is registered with CTest. Host profile creation requires
    # an explicit manual invocation after separate user authorization.
    add_executable(rc_music_media_sandbox_helper
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerProbeHelper.cpp")
    target_link_libraries(rc_music_media_sandbox_helper PRIVATE advapi32 ws2_32)
    set_target_properties(rc_music_media_sandbox_helper PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
    rc_music_configure_cpp(rc_music_media_sandbox_helper)
    add_executable(rc_music_media_sandbox_probe
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerProbeMain.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerNetworkObserver.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProtocol.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverFiles.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverIdentity.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProcess.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverActor.cpp"
        "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverDriver.cpp")
    target_link_libraries(rc_music_media_sandbox_probe PRIVATE rc_music_download_network userenv advapi32 ws2_32 fwpuclnt)
    target_compile_definitions(rc_music_media_sandbox_probe PRIVATE
        RC_MUSIC_TEST_SANDBOX_HELPER_PATH="$<TARGET_FILE:rc_music_media_sandbox_helper>"
        RC_MUSIC_TEST_MF_WORKER_PATH="$<TARGET_FILE:rc_music_media_foundation_worker>"
        RC_MUSIC_TEST_AUDIO_FIXTURES="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid"
        RC_MUSIC_SANDBOX_WORKSPACE_ROOT="${RC_MUSIC_SOURCE_ROOT}")
    rc_music_configure_cpp(rc_music_media_sandbox_probe)
    add_dependencies(rc_music_media_sandbox_probe rc_music_media_sandbox_helper rc_music_media_foundation_worker)
    add_test(NAME rc_music.download-network.MediaSandboxProbeParser COMMAND rc_music_media_sandbox_probe --self-test)
    set_tests_properties(rc_music.download-network.MediaSandboxProbeParser PROPERTIES
        LABELS "download-network;security;appcontainer;parser;windows" TIMEOUT 15
        ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    foreach(unit IN ITEMS AppContainerNetworkObserver AppContainerObserverProtocol AppContainerObserverFiles AppContainerObserverIdentity AppContainerObserverProcess AppContainerObserverDriver AppContainerObserverActor AppContainerProbeProtocol MediaFoundationWorkerProtocol WindowsMediaFoundationCodec WindowsMediaFoundationConversion
                          WindowsMediaFoundationLaunch)
        set(target "rc_music_${unit}_tests")
        add_executable(${target} "${RC_MUSIC_SOURCE_ROOT}/tests/qt/${unit}Tests.cpp")
        target_link_libraries(${target} PRIVATE rc_music_download_network Qt6::Test)
        if(unit STREQUAL "WindowsMediaFoundationCodec")
            target_link_libraries(${target} PRIVATE rc_music_media_foundation_codec)
        endif()
        if(unit STREQUAL "AppContainerNetworkObserver")
            target_sources(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerNetworkObserver.cpp")
            target_link_libraries(${target} PRIVATE fwpuclnt advapi32)
        endif()
        if(unit STREQUAL "AppContainerObserverProtocol")
            target_sources(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProtocol.cpp")
        endif()
        if(unit STREQUAL "AppContainerObserverFiles" OR unit STREQUAL "AppContainerObserverIdentity")
            target_sources(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/${unit}.cpp")
            target_link_libraries(${target} PRIVATE advapi32)
        endif()
        if(unit STREQUAL "AppContainerObserverProcess")
            target_sources(${target} PRIVATE
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProcess.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverIdentity.cpp")
            target_link_libraries(${target} PRIVATE advapi32)
            target_compile_definitions(${target} PRIVATE
                RC_MUSIC_TEST_SANDBOX_HELPER_PATH="$<TARGET_FILE:rc_music_media_sandbox_helper>")
            add_dependencies(${target} rc_music_media_sandbox_helper)
        endif()
        if(unit STREQUAL "AppContainerObserverDriver")
            target_sources(${target} PRIVATE
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverDriver.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProtocol.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverIdentity.cpp")
            target_link_libraries(${target} PRIVATE advapi32)
        endif()
        if(unit STREQUAL "AppContainerObserverActor")
            target_sources(${target} PRIVATE
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverActor.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProcess.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverIdentity.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverProtocol.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerObserverFiles.cpp"
                "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox/AppContainerNetworkObserver.cpp")
            target_link_libraries(${target} PRIVATE advapi32 fwpuclnt)
        endif()
        target_include_directories(${target} PRIVATE "${RC_MUSIC_SOURCE_ROOT}/src/adapters"
            "${RC_MUSIC_SOURCE_ROOT}/tests/windows/sandbox")
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        rc_music_configure_cpp(${target})
        target_compile_definitions(${target} PRIVATE
            RC_MUSIC_TEST_AUDIO_FIXTURES="${RC_MUSIC_SOURCE_ROOT}/tests/audio/corpus/fixtures/valid"
            RC_MUSIC_TEST_MF_WORKER_PATH="$<TARGET_FILE:rc_music_media_foundation_worker>")
        add_dependencies(${target} rc_music_media_foundation_worker)
        add_test(NAME "rc_music.download-network.${unit}" COMMAND ${target} -o -,txt)
        set_tests_properties("rc_music.download-network.${unit}" PROPERTIES
            LABELS "download-network;qt;media-foundation;FR-051;FR-053" TIMEOUT 60
            ENVIRONMENT "QT_LOGGING_TO_CONSOLE=1;QT_FORCE_STDERR_LOGGING=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endforeach()
endif()
