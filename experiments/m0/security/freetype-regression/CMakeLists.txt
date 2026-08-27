cmake_minimum_required(VERSION 3.31)
project(m0_freetype_coordinates LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This bounded M0 regression reproduces the pinned Windows/MSVC vendor build only")
endif()

include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(M0_FT_SOURCE_DIR "" CACHE PATH "FreeType vendor directory from the authenticated Qt source archive")
set(M0_FT_FIXTURE_FILE "" CACHE FILEPATH "Qt testfont_variable.ttf from the same source archive")
foreach(input IN ITEMS M0_FT_SOURCE_DIR M0_FT_FIXTURE_FILE)
    if(NOT IS_ABSOLUTE "${${input}}" OR NOT EXISTS "${${input}}")
        message(FATAL_ERROR "${input} must be an existing absolute path")
    endif()
endforeach()

file(SHA256 "${M0_FT_SOURCE_DIR}/src/truetype/ttgxvar.c" vendor_sha256)
set(known_vendor_sha256
    b5ca576f480437e59f84a31ba2b569c7aadd31871f608034f6ea576340b88e10
    9126fee413fabeb2f9f78b60548065dda2a8365ef95da43cb2a0edc55813e346
)
if(NOT vendor_sha256 IN_LIST known_vendor_sha256)
    message(FATAL_ERROR "Unrecognized ttgxvar.c identity")
endif()
file(SHA256 "${M0_FT_SOURCE_DIR}/include/freetype/freetype.h" header_sha256)
if(NOT header_sha256 STREQUAL "28519f6bdbda31718a5ac2715ca0d3765b5594cad6ef20d0a817d780d6a4ae79")
    message(FATAL_ERROR "The vendor version header differs from the pinned Qt source")
endif()
file(SHA256 "${M0_FT_FIXTURE_FILE}" fixture_sha256)
if(NOT fixture_sha256 STREQUAL "8ce9e27075d78bfc284afaa052e133166b5e89bd90498c12effe238c35a8c7a7")
    message(FATAL_ERROR "The licensed variable-font fixture differs from the pinned Qt revision")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${M0_FT_SOURCE_DIR}/src/truetype/ttgxvar.c"
    "${M0_FT_SOURCE_DIR}/include/freetype/freetype.h" "${M0_FT_FIXTURE_FILE}")

# Translation-unit list follows Qt's BSD-3-Clause FreeType build recipe.
# See NOTICE.txt. This standalone check uses FreeType's bundled gzip support,
# without Qt Core, PNG, HarfBuzz, a GUI, installation, or network access.
set(freetype_units
    autofit/afadjust.c autofit/afgsub.c autofit/autofit.c
    base/ftbase.c base/ftbbox.c base/ftbdf.c base/ftbitmap.c base/ftcid.c
    base/ftfstype.c base/ftgasp.c base/ftglyph.c base/ftgxval.c base/ftinit.c
    base/ftmm.c base/ftotval.c base/ftpatent.c base/ftpfr.c base/ftstroke.c
    base/ftsynth.c base/fttype1.c base/ftwinfnt.c base/ftsystem.c
    bdf/bdf.c bzip2/ftbzip2.c cache/ftcache.c cff/cff.c cid/type1cid.c
    gzip/ftgzip.c lzw/ftlzw.c pcf/pcf.c pfr/pfr.c psaux/psaux.c
    pshinter/pshinter.c psnames/psnames.c raster/raster.c sdf/sdf.c
    sfnt/sfnt.c smooth/smooth.c svg/svg.c truetype/truetype.c
    type1/type1.c type42/type42.c winfonts/winfnt.c
)
list(TRANSFORM freetype_units PREPEND "${M0_FT_SOURCE_DIR}/src/")
list(APPEND freetype_units "${M0_FT_SOURCE_DIR}/builds/windows/ftdebug.c")
add_library(m0_freetype STATIC ${freetype_units})
target_include_directories(m0_freetype PUBLIC "${M0_FT_SOURCE_DIR}/include")
target_compile_definitions(m0_freetype PRIVATE FT2_BUILD_LIBRARY)

add_executable(m0_freetype_coordinates coordinates.cpp)
target_link_libraries(m0_freetype_coordinates PRIVATE m0_freetype)
if(MSVC)
    target_compile_options(m0_freetype PRIVATE /W0)
    target_compile_options(m0_freetype_coordinates PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(m0_freetype_coordinates PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

foreach(state IN ITEMS fresh default custom)
    foreach(count RANGE 0 2)
        add_test(NAME "coordinates.${state}.${count}"
            COMMAND m0_freetype_coordinates "${M0_FT_FIXTURE_FILE}" "${state}" "${count}")
        set_tests_properties("coordinates.${state}.${count}" PROPERTIES TIMEOUT 10)
    endforeach()
endforeach()
message(STATUS "FreeType source: ${vendor_sha256}")
message(STATUS "Qt variable-font fixture: ${fixture_sha256}")
