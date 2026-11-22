# Only 3.0's media player and editor use FFmpeg. Use upstream configure/make so the
# chosen components (including their indirect dependencies) stay upstream's.
include(ExternalProject)
include(FetchContent)

set(IW_FFMPEG_REVISION d35e13b9e05cc20f48373f88e399c8ef0c16fc61)
FetchContent_Declare(iw_ffmpeg_source
    URL "https://github.com/FFmpeg/FFmpeg/archive/${IW_FFMPEG_REVISION}.tar.gz"
    URL_HASH SHA256=2b3182afe5a12736f5e0598d433680b8b87d8237336cacccae84a419d70401f4
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR no-cmake-project)
FetchContent_MakeAvailable(iw_ffmpeg_source)

get_filename_component(_git_cmd_dir "${GIT_EXECUTABLE}" DIRECTORY)
find_program(IW_GIT_BASH bash HINTS "${_git_cmd_dir}/../bin" NO_DEFAULT_PATH REQUIRED)

FetchContent_Declare(iw_msys_make
    URL https://repo.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst
    URL_HASH SHA256=af0bdba17f06fe037f0194069adaa31a8fe45f1a11381501896aea1fae37bd5d
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR no-cmake-project)
FetchContent_MakeAvailable(iw_msys_make)

if(MSVC_CXX_ARCHITECTURE_ID STREQUAL "X86")
    set(_arch x86)
else()
    set(_arch x86_64)
endif()
set(_ff_build "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-${_arch}")
set(_ff_make "${iw_msys_make_SOURCE_DIR}/usr/bin/make.exe")
set(_ff_script "${CMAKE_CURRENT_LIST_DIR}/configure-ffmpeg.sh")
get_filename_component(_ff_tools "${CMAKE_C_COMPILER}" DIRECTORY)

ExternalProject_Add(iw_ffmpeg_build
    SOURCE_DIR "${iw_ffmpeg_source_SOURCE_DIR}"
    BINARY_DIR "${_ff_build}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
    CONFIGURE_COMMAND "${IW_GIT_BASH}" "${_ff_script}" configure "${_ff_tools}" <SOURCE_DIR> "${_arch}"
    BUILD_COMMAND "${IW_GIT_BASH}" "${_ff_script}" make "${_ff_tools}" "${_ff_make}"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${_ff_build}/libavformat/avformat.lib"
                     "${_ff_build}/libavcodec/avcodec.lib"
                     "${_ff_build}/libavutil/avutil.lib"
                     "${_ff_build}/libswresample/swresample.lib"
                     "${_ff_build}/libswscale/swscale.lib"
    EXCLUDE_FROM_ALL TRUE)
ExternalProject_Add_StepDependencies(iw_ffmpeg_build configure "${_ff_script}")

add_library(iw_ffmpeg INTERFACE)
add_dependencies(iw_ffmpeg iw_ffmpeg_build)
target_include_directories(iw_ffmpeg SYSTEM INTERFACE "${_ff_build}" "${iw_ffmpeg_source_SOURCE_DIR}")
target_link_libraries(iw_ffmpeg INTERFACE "${_ff_build}/libavformat/avformat.lib"
    "${_ff_build}/libavcodec/avcodec.lib" "${_ff_build}/libswresample/swresample.lib"
    "${_ff_build}/libswscale/swscale.lib" "${_ff_build}/libavutil/avutil.lib")
add_library(iw::ffmpeg ALIAS iw_ffmpeg)
