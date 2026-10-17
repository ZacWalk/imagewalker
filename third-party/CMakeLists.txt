include(FetchContent)
include(ExternalProject)

# Several of these projects predate CMP0077, so their option() calls would wipe the
# settings below. NEW makes option() defer to the variables we set here.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
# Likewise CMP0091: without NEW, CMAKE_MSVC_RUNTIME_LIBRARY is ignored and these
# libraries silently build against the dynamic CRT.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

# Every dependency is fetched and built from source, static, x64, no tools/tests/docs.
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

set(IW_ZLIB_VERSION   1.3.1)
set(IW_JPEG_VERSION   3.1.2)
set(IW_PNG_VERSION    1.6.50)
set(IW_TIFF_VERSION   4.7.0)
set(IW_GIF_VERSION    5.2.2)
set(IW_EXPAT_VERSION  2.7.1)
set(IW_EXIF_VERSION   0.6.26)
set(IW_HUNSPELL_VERSION 1.7.3)

# --------------------------------------------------------------------- zlib
# zlib 1.3.1 still says cmake_minimum_required(VERSION 2.4.4), which CMake 4 rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(ZLIB_BUILD_EXAMPLES OFF)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v${IW_ZLIB_VERSION}
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(zlib)

add_library(iw_zlib INTERFACE)
target_link_libraries(iw_zlib INTERFACE zlibstatic)
target_include_directories(iw_zlib SYSTEM INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
add_library(ZLIB::ZLIB ALIAS iw_zlib)
add_library(iw::zlib  ALIAS iw_zlib)

# From here on, find_package(ZLIB) must resolve to what we just built.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/find-shims")

# ------------------------------------------------------------ libjpeg-turbo
# libjpeg-turbo refuses to be add_subdirectory()'d, so it gets its own build tree.
# SIMD is off so the build does not need NASM; decode speed is irrelevant here.
set(IW_JPEG_ROOT    "${CMAKE_BINARY_DIR}/_deps/jpeg")
set(IW_JPEG_INSTALL "${IW_JPEG_ROOT}/install")
set(IW_JPEG_LIB     "${IW_JPEG_INSTALL}/lib/jpeg-static.lib")
file(MAKE_DIRECTORY "${IW_JPEG_INSTALL}/include")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_jpeg_runtime MultiThreadedDebug)
else()
    set(_jpeg_runtime MultiThreaded)
endif()

ExternalProject_Add(libjpegturbo_ep
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG        ${IW_JPEG_VERSION}
    GIT_SHALLOW    TRUE
    PREFIX         "${IW_JPEG_ROOT}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${IW_JPEG_INSTALL}
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${_jpeg_runtime}
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF
        -DWITH_SIMD=OFF
        -DREQUIRE_SIMD=OFF
        -DWITH_CRT_DLL=OFF
    BUILD_BYPRODUCTS "${IW_JPEG_LIB}"
    UPDATE_DISCONNECTED TRUE)

add_library(iw_jpeg_imp STATIC IMPORTED GLOBAL)
set_target_properties(iw_jpeg_imp PROPERTIES IMPORTED_LOCATION "${IW_JPEG_LIB}")
add_library(iw_jpeg INTERFACE)
add_dependencies(iw_jpeg libjpegturbo_ep)
target_link_libraries(iw_jpeg INTERFACE iw_jpeg_imp)
target_include_directories(iw_jpeg SYSTEM INTERFACE "${IW_JPEG_INSTALL}/include")
add_library(JPEG::JPEG ALIAS iw_jpeg)
add_library(iw::jpeg   ALIAS iw_jpeg)

# -------------------------------------------------------------------- libpng
set(PNG_SHARED OFF)
set(PNG_STATIC ON)
set(PNG_TESTS OFF)
set(PNG_TOOLS OFF)
set(PNG_FRAMEWORK OFF)
set(SKIP_INSTALL_ALL ON)
FetchContent_Declare(libpng
    GIT_REPOSITORY https://github.com/pnggroup/libpng.git
    GIT_TAG        v${IW_PNG_VERSION}
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libpng)

add_library(iw_png INTERFACE)
target_link_libraries(iw_png INTERFACE png_static iw_zlib)
target_include_directories(iw_png SYSTEM INTERFACE "${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}")
add_library(iw::png ALIAS iw_png)

# ------------------------------------------------------------------- libtiff
# JPEG-compressed TIFF is dropped: supporting it would make libtiff require
# libjpeg's headers at configure time, which the ExternalProject cannot provide.
set(tiff-tools OFF)
set(tiff-tests OFF)
set(tiff-contrib OFF)
set(tiff-docs OFF)
set(tiff-install OFF)
set(tiff-deprecated OFF)
set(cxx OFF)
foreach(codec IN ITEMS jpeg old-jpeg webp zstd lzma jbig lerc libdeflate)
    set(${codec} OFF)
endforeach()
FetchContent_Declare(libtiff
    GIT_REPOSITORY https://gitlab.com/libtiff/libtiff.git
    GIT_TAG        v${IW_TIFF_VERSION}
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libtiff)

add_library(iw_tiff INTERFACE)
target_link_libraries(iw_tiff INTERFACE tiff)
add_library(iw::tiff ALIAS iw_tiff)

# -------------------------------------------------------------------- giflib
# giflib ships no build system for Windows, so we inject one. The tarball comes
# from the Debian pool rather than SourceForge, whose mirror redirects defeat
# CMake's downloader.
FetchContent_Declare(giflib
    URL      https://deb.debian.org/debian/pool/main/g/giflib/giflib_${IW_GIF_VERSION}.orig.tar.gz
    URL_HASH SHA256=be7ffbd057cadebe2aa144542fd90c6838c6a083b5e8a9048b8ee3b66b29d5fb
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${CMAKE_CURRENT_SOURCE_DIR}/patches/giflib-CMakeLists.txt" CMakeLists.txt
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(giflib)
add_library(iw::gif ALIAS giflib)

# --------------------------------------------------------------------- expat
set(EXPAT_BUILD_TOOLS OFF)
set(EXPAT_BUILD_EXAMPLES OFF)
set(EXPAT_BUILD_TESTS OFF)
set(EXPAT_BUILD_DOCS OFF)
set(EXPAT_BUILD_PKGCONFIG OFF)
set(EXPAT_SHARED_LIBS OFF)
set(EXPAT_ENABLE_INSTALL OFF)
set(EXPAT_MSVC_STATIC_CRT ON)
string(REPLACE "." "_" _expat_tag "${IW_EXPAT_VERSION}")
FetchContent_Declare(expat
    GIT_REPOSITORY https://github.com/libexpat/libexpat.git
    GIT_TAG        R_${_expat_tag}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  expat
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(expat)
add_library(iw::expat ALIAS expat)

# ------------------------------------------------------------------- libexif
# 2.31's PropertyEXIF is a thin wrapper over libexif; the earlier versions
# parse EXIF by hand and do not need this.
FetchContent_Declare(libexif
    GIT_REPOSITORY https://github.com/libexif/libexif.git
    GIT_TAG        v${IW_EXIF_VERSION}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${CMAKE_CURRENT_SOURCE_DIR}/patches/libexif-CMakeLists.txt" CMakeLists.txt
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libexif)
add_library(iw::exif ALIAS libexif)

# ------------------------------------------------------------------ hunspell
# Replaces the myspell fork 2.31 vendored. It reads the same .aff/.dic files,
# which is why exe/dic carries over unchanged.
FetchContent_Declare(hunspell
    GIT_REPOSITORY https://github.com/hunspell/hunspell.git
    GIT_TAG        v${IW_HUNSPELL_VERSION}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${CMAKE_CURRENT_SOURCE_DIR}/patches/hunspell-CMakeLists.txt" CMakeLists.txt
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(hunspell)
add_library(iw::hunspell ALIAS hunspell)

include("${CMAKE_CURRENT_SOURCE_DIR}/webp.cmake")

# ---------------------------------------------------------------------------
add_custom_target(third_party
    DEPENDS libjpegturbo_ep zlibstatic png_static tiff giflib expat
            libexif hunspell webp)
