# Run only after the exact fixture targets have built. Never hash stale outputs
# during configure, and never perform artifact discovery/hashing on the UI.
foreach(name IN ITEMS OUTPUT TEMPLATE BOOTSTRAP PAYLOAD CORE NETWORK CRT_DIR PLUGIN MEDIA ENDPOINT)
    if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        message(FATAL_ERROR "Missing fixture build input ${name}")
    endif()
endforeach()
set(inputs "${BOOTSTRAP}" "${PAYLOAD}" "${CORE}" "${NETWORK}"
    "${CRT_DIR}/msvcp140.dll" "${CRT_DIR}/msvcp140_1.dll"
    "${CRT_DIR}/vcruntime140.dll" "${CRT_DIR}/vcruntime140_1.dll" "${PLUGIN}")
set(fixture_artifacts "")
foreach(input IN LISTS inputs)
    file(SHA256 "${input}" hash)
    cmake_path(CONVERT "${input}" TO_CMAKE_PATH_LIST path NORMALIZE)
    string(APPEND fixture_artifacts "        {QStringLiteral(\"${path}\"), QByteArrayLiteral(\"${hash}\")},\n")
endforeach()
file(SHA256 "${MEDIA}" fixture_media_hash)
cmake_path(CONVERT "${MEDIA}" TO_CMAKE_PATH_LIST fixture_media NORMALIZE)
set(fixture_endpoint "${ENDPOINT}")
configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)
