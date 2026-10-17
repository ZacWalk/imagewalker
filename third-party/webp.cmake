# ImageWalker 3.0 owns a portable static WebP codec; no WIC extension or command-line tools.
set(IW_WEBP_VERSION 1.6.0)
set(WEBP_LINK_STATIC ON)
set(WEBP_BUILD_ANIM_UTILS OFF)
set(WEBP_BUILD_CWEBP OFF)
set(WEBP_BUILD_DWEBP OFF)
set(WEBP_BUILD_GIF2WEBP OFF)
set(WEBP_BUILD_IMG2WEBP OFF)
set(WEBP_BUILD_VWEBP OFF)
set(WEBP_BUILD_WEBPINFO OFF)
set(WEBP_BUILD_WEBPMUX OFF)
set(WEBP_BUILD_EXTRAS OFF)
set(WEBP_BUILD_LIBWEBPMUX OFF)
set(WEBP_BUILD_WEBP_JS OFF)
FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v${IW_WEBP_VERSION}
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libwebp)
add_library(iw_webp INTERFACE)
target_link_libraries(iw_webp INTERFACE webp)
target_include_directories(iw_webp SYSTEM INTERFACE "${libwebp_SOURCE_DIR}/src")
add_library(iw::webp ALIAS iw_webp)
