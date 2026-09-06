include_guard(GLOBAL)

# Pinned synthetic regression inputs, copied byte-for-byte under the existing
# private-development license. Production never includes this test module.
function(_rc_music_require_audio_fixture corpus relative_path expected_size expected_sha256)
    set(path "${corpus}/${relative_path}")
    if(IS_DIRECTORY "${path}")
        message(FATAL_ERROR "Audio fixture directory is not a file: ${relative_path}")
    elseif(NOT EXISTS "${path}")
        message(FATAL_ERROR "Audio fixture missing: ${relative_path}")
    endif()
    file(SIZE "${path}" actual_size)
    if(NOT actual_size EQUAL expected_size)
        message(FATAL_ERROR "Audio fixture size differs: ${relative_path}")
    endif()
    file(SHA256 "${path}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR "Audio fixture hash differs: ${relative_path}")
    endif()
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    endif()
endfunction()

function(rc_music_validate_audio_corpus corpus)
    _rc_music_require_audio_fixture("${corpus}" "LICENSE.assets.txt" 663
        "54b7a0cbbf8050b2ac4e24cf8afd74c68a2c6d6c6b22c0487599dd84d23b8b13")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/hostile/empty.bin" 0
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/flac-16-stereo.flac" 60531
        "103577f9c6b37eea391b86f9a085dc7fb9b0a26571aa907b9d37e47d0b543a1c")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/m4a-aac-lc.m4a" 33400
        "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/candidate/m4a-alac-candidate.m4a" 78938
        "5d729fbe90a0d98a081d1fe590b24021a8001871f516529d0f8d8392109ad339")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/mp3-cbr-stereo.mp3" 33318
        "b7d0f99cf733c7458dbec3ab62ad397cd98cc6b069db5c12cbb9834885e66b62")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/mp3-unicode-tags.mp3" 24966
        "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/mp3-vbr-stereo.mp3" 14406
        "74a5c55e50a8a73198525878f1a0463de918b0490910a5f7681bb933c31b29c9")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/ogg-vorbis.ogg" 12605
        "abab4db40eb8380974e1c6ea69eb4c24bfd8eb2803b67b79e2a1a49523a7096f")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/candidate/opus-candidate.opus" 35706
        "341e25361371f851d85d34af950fb10279db36fbd67d91b324a9c08e40791e15")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/hostile/truncated-wav-pcm-mono.wav" 40
        "58751e20c4e8403194736fd0f1be0769a09441e73c4a2e0858265465a23e7bff")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/valid/wav-pcm-mono.wav" 96044
        "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/edge/wrong-extension-wav-as-mp3.mp3" 96044
        "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f")
endfunction()
