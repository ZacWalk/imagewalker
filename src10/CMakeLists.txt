# ArtMate 1.0, the earliest ancestor of ImageWalker: a thumbnail browser and
# image viewer, and nothing else. Originally MFC; now plain WTL.

set(IW10_SOURCES
    main.cpp
    test.cpp
    MainFrm.cpp
    ImageView.cpp
    ItemsView.cpp
    Jobs.cpp
    Thumb.cpp
    Dib.cpp
    DibDC.cpp
    DibExtra.cpp
    DibMemory.cpp
    Load.cpp
    LoadAny.cpp
    LoadBmp.cpp
    LoadGif.cpp
    LoadJpg.cpp
    LoadPcx.cpp
    LoadPng.cpp
    LoadTiff.cpp
    StdAfx.cpp
)

list(TRANSFORM IW10_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_executable(imagewalker10 WIN32 ${IW10_SOURCES} ArtMate.rc)

target_include_directories(imagewalker10 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(imagewalker10 PRIVATE _MBCS)

target_precompile_headers(imagewalker10 PRIVATE StdAfx.h)

target_link_libraries(imagewalker10 PRIVATE
    iw::flags
    iw::jpeg
    iw::png
    iw::tiff
    iw::gif
    iw::zlib
)

# The app embeds its own manifest as resource 1.
target_link_options(imagewalker10 PRIVATE /MANIFEST:NO)
