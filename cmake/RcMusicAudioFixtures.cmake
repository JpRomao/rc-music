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

function(rc_music_validate_truncated_audio_corpus corpus)
    _rc_music_require_audio_fixture("${corpus}" "hostile-truncation.v1.json" 8812
        "22f48fa95594aa77fce7c8ea742adc1cfc449ac9c68bbef8cd9201b9eadc6dad")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/hostile/truncated-mp3-cbr.mp3" 64
        "f5262c2a9e396f187f802f54804404eaa4357c5379fe169aaf544dfa07f518dd")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/hostile/truncated-m4a-aac.m4a" 64
        "1ae4b833a65b5827c36d828fd03eaa11de4f4a6ab815830287f2928a64984854")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/hostile/truncated-flac.flac" 64
        "b21f4b7ab4d617d6c78f46720b2b753824d47aa44c00bf96b33707c7f72e7c03")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/hostile/truncated-ogg-vorbis.ogg" 64
        "520134b5a783f2b0676fc870ce9aae7e54be5fe80c7e83fdfa8487971a5f5479")
endfunction()

function(rc_music_validate_artwork_audio_corpus corpus)
    _rc_music_require_audio_fixture("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." "scripts/fixtures/generate_artwork_corpus.py" 12184
        "777a490529fb493b26ff166658baa7d38e20ac070e57eaf31cd1a40072ae8a2a")
    _rc_music_require_audio_fixture("${corpus}" "artwork.v1.json" 35129
        "fe5183a82d3ab31ff38ca1416a469f731d8959a9c54df66f16f96aac3691b9af")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-small.mp3" 33408
        "3aa115e7bbabd810d5f0a3dbb20b8e83f74da0ec2c2270d52cb7788c721426d5")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-bytes-below.mp3" 10519100
        "4e0bc923d83b4569ee322c07197e33ba9ba7799d34fa8fc0dae78cd780e71284")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-bytes-at.mp3" 10519101
        "ec2aafaec021ba119e25e60489b4df9cdaf6e07211bac16862587f9c171d29b3")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-bytes-above.mp3" 10519102
        "18d765c9e25d36088644f2338d2f86c8b2075bd6776accbea555055070c0dce9")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-width-below.mp3" 33424
        "84fb86306f045864f0539ea85861ee3cd51ccb6dcab2e99c2ffd6ab77f5a6383")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-width-at.mp3" 33424
        "7bc86ab909b282b4a92c3ef39eec80c24eeb4132171f1bcdf886c952588d81e6")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-width-above.mp3" 33424
        "5febc383540591baa74b3af42200ba2c91c7f03bf22853d5021c9d32c56e6283")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-height-below.mp3" 33429
        "2bca0195537835e5d84bf022a9c05b674ed9dca9c56afe3c3733c9e8c4a58056")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-height-at.mp3" 33429
        "2d5aab05baf44e7ce8add584346ed21ef62700a02b93efe3930008111828bba3")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-height-above.mp3" 33429
        "8140c7e427aa7d59d8371f2d5c21d8ca711981d049421293392a0afd951351f1")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-pixels-below.mp3" 48964
        "3d22c64a774ac1d2a717c5aef297ee301a76175bef74effc035ad64320205827")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-pixels-at.mp3" 48964
        "c558e78e4cfb911b5f4ee37d14c332e6664771e81148ec8993949f0d39c8b221")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-png-pixels-above.mp3" 48970
        "7f0ffd5dea14ac3c0d6d965ffd727867a40eaeaffa52b88eed049a838f61686b")
    _rc_music_require_audio_fixture("${corpus}" "fixtures/artwork/mp3-invalid-art.mp3" 33353
        "28905103380715e5a9c1e184e63582b7f3c777042e2b09559765b87643bacf58")
endfunction()

function(rc_music_validate_audio_corpus corpus)
    rc_music_validate_artwork_audio_corpus("${corpus}")
    rc_music_validate_truncated_audio_corpus("${corpus}")
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
