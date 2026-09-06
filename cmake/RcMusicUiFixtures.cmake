include_guard(GLOBAL)

# Frozen synthetic regression inputs, copied under the original private license.
# Only test composition includes this module; no comparative protocol is read.
set(RC_MUSIC_UI_FIXTURE_SIZE 8615)
set(RC_MUSIC_UI_FIXTURE_SHA256 "03fd2a762758c524e226296a63575df5e3972a9a0ea09c1109bb641089a58464")
set(RC_MUSIC_UI_QUEUE_SIZE 582271)
set(RC_MUSIC_UI_QUEUE_SHA256 "a3c2e890f08885af1de319ed330f71a4b657aeaecd7bf7c640dc633095d55ef3")
set(RC_MUSIC_UI_LICENSE_SIZE 366)
set(RC_MUSIC_UI_LICENSE_SHA256 "c77c86115e7da5fff31ab28c903e78a8a12f65f9b2792f7e0d137c458fae5b94")

# Assets stay in the product assets directory. These pins feed the existing
# ShellUiContract resource assertions; they are not additional fixture copies.
set(RC_MUSIC_UI_PLACEHOLDER_SIZE 257)
set(RC_MUSIC_UI_PLACEHOLDER_SHA256 "3027bb88ca4503fc7206a58dd20ddbf460d8662300ec0e1f9321cf11c4acb0a6")
set(RC_MUSIC_UI_TRACK_SIZE 371)
set(RC_MUSIC_UI_TRACK_SHA256 "9d07309f405b6b4d1c2acc9b97e8527eb2ccc295e992ed1b0ffce3aef0737312")

function(_rc_music_require_ui_fixture corpus relative_path expected_size expected_sha256)
    set(path "${corpus}/${relative_path}")
    if(IS_DIRECTORY "${path}")
        message(FATAL_ERROR "UI fixture directory is not a file: ${relative_path}")
    elseif(NOT EXISTS "${path}")
        message(FATAL_ERROR "UI fixture missing: ${relative_path}")
    endif()
    file(SIZE "${path}" actual_size)
    if(NOT actual_size EQUAL expected_size)
        message(FATAL_ERROR "UI fixture size differs: ${relative_path}")
    endif()
    file(SHA256 "${path}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR "UI fixture hash differs: ${relative_path}")
    endif()
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    endif()
endfunction()

function(rc_music_validate_ui_fixtures corpus)
    _rc_music_require_ui_fixture("${corpus}" "fixture.v1.json"
        "${RC_MUSIC_UI_FIXTURE_SIZE}" "${RC_MUSIC_UI_FIXTURE_SHA256}")
    _rc_music_require_ui_fixture("${corpus}" "queue.v1.json"
        "${RC_MUSIC_UI_QUEUE_SIZE}" "${RC_MUSIC_UI_QUEUE_SHA256}")
    _rc_music_require_ui_fixture("${corpus}" "LICENSE.assets.txt"
        "${RC_MUSIC_UI_LICENSE_SIZE}" "${RC_MUSIC_UI_LICENSE_SHA256}")
endfunction()
