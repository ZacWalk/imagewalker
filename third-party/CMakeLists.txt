include(FetchContent)

# Several of these projects predate CMP0077, so their option() calls would wipe the
# settings below. NEW makes option() defer to the variables we set here.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
# Likewise CMP0091: without NEW, CMAKE_MSVC_RUNTIME_LIBRARY is ignored and these
# libraries silently build against the dynamic CRT.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

# Every dependency is fetched and built from source, static, x64, no tools/tests/docs. libfiles is
# pinned by commit; each of the others is the archive of its release tag - the tag's tree - or the
# release tarball, pinned by SHA-256 here.
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

set(IW_LIBFILES_REVISION f5cd78fc6807ece846b92e23eab46b565a50c084)
set(IW_HUNSPELL_VERSION 1.7.6)

# ------------------------------------------------------------------ libfiles
# libfiles, the file library ImageWalker shares with Diffractor, builds the codecs both use, each
# pinned by SHA-256 in its own manifest: libjpeg-turbo, with its SIMD and the libjpeg 8 API, libpng
# over zlib-ng built zlib-compatible, and libwebp. ImageWalker decodes HEIF, JPEG XL and camera raw
# through the Windows codecs installed, so libfiles' own are not built. A sibling checkout at
# ../libfiles is built instead of the pin when one exists, whatever its HEAD; CI and a fresh clone
# build the pin.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_LIBFILES AND EXISTS "${PROJECT_SOURCE_DIR}/../libfiles/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBFILES "${PROJECT_SOURCE_DIR}/../libfiles" CACHE PATH
        "A libfiles checkout to build instead of the pinned commit")
endif()
set(LIBFILES_HEIF OFF)
set(LIBFILES_JXL OFF)
set(LIBFILES_RAW OFF)
set(LIBFILES_GIF ON)
set(LIBFILES_TIFF ON)
FetchContent_Declare(libfiles
    GIT_REPOSITORY https://github.com/ZacWalk/libfiles.git
    GIT_TAG        ${IW_LIBFILES_REVISION}
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libfiles)

if(FETCHCONTENT_SOURCE_DIR_LIBFILES)
    message(STATUS "libfiles: local checkout ${FETCHCONTENT_SOURCE_DIR_LIBFILES}")
else()
    message(STATUS "libfiles: pinned ${IW_LIBFILES_REVISION}")
endif()

add_library(iw::zlib ALIAS libfiles-zlib)
add_library(iw::jpeg ALIAS libfiles-jpeg)
add_library(iw::png  ALIAS libfiles-png)
add_library(iw::webp ALIAS libfiles-webp)
add_library(iw::gif ALIAS libfiles-gif)
add_library(iw::tiff ALIAS libfiles-tiff)

# ------------------------------------------------------------------ hunspell
# Replaces the myspell fork 2.31 vendored. It reads the same .aff/.dic files,
# which is why exe/dic carries over unchanged.
FetchContent_Declare(hunspell
    URL      https://github.com/hunspell/hunspell/releases/download/v${IW_HUNSPELL_VERSION}/hunspell-${IW_HUNSPELL_VERSION}.tar.gz
    URL_HASH SHA256=9c650ace31d4d8cc1da925a9507c3de1d50f1a54857af2f72429f517d5d53467
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${CMAKE_CURRENT_SOURCE_DIR}/patches/hunspell-CMakeLists.txt" CMakeLists.txt
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(hunspell)
add_library(iw::hunspell ALIAS hunspell)

# ---------------------------------------------------------------------------
add_custom_target(third_party
    DEPENDS libfiles-zlib libfiles-jpeg libfiles-png libfiles-webp libfiles-gif tiff hunspell)
