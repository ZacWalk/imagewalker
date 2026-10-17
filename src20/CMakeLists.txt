# ImageWalker 2.0 -> imagewalker20.exe
#
# Standalone x64 WTL port. Removed historical subsystems:
# the web gallery and its template parser, the iw:// pluggable protocol and
# embedded browser, screen capture, MAPI email, TWAIN/STI scanning, DirectShow
# video thumbnails and the COM automation server. Also gone from this tree:
# the thumbnail cache, zip, toolbar and accelerator customisation, wallpaper
# and file associations.

set(IW20_SOURCES
    App.cpp
    AppCommands.cpp
    AppMainFrame.cpp
    FileFormatAny.cpp
    FileJpeg.cpp
    Imaging.cpp
    ImagingDataObject.cpp
    ImagingFilter.cpp
    main.cpp
    test.cpp
    Metadata.cpp
    Settings.cpp
    StdAfx.cpp
    ToolContactSheet.cpp
    ToolConvert.cpp
    ToolJpeg.cpp
    ToolResize.cpp
    UtilCore.cpp
    UtilMd5.cpp
    ViewDialogs.cpp
    ViewModelItems.cpp
    ViewModelSearch.cpp
    ViewModelState.cpp
    ViewOptionsDlg.cpp
    ViewPrintFolder.cpp
    ViewRender.cpp
    ViewSkin.cpp
    ViewSpellEdit.cpp
)

# CMake's RelWithDebInfo downgrades /O2's implied /Ob2 to /Ob1, which limits
# inlining to functions the source marked inline -- and /Ob governs LTCG's
# cross-module inlining too, so calls into libiw's line converters would stay
# out of line even with /GL on. libiw makes the same replacement for its own
# sources. The preset builds RelWithDebInfo, so this is the release config.
# Replaced rather than appended: appending /Ob2 leaves both on the command line
# and every compile then reports D9025. Directory scope, so 2.0 only.
string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

add_executable(imagewalker20 WIN32 ${IW20_SOURCES} ImageWalker.rc)
target_include_directories(imagewalker20 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/libiw/compat")
target_compile_definitions(imagewalker20 PRIVATE UNICODE _UNICODE)
target_precompile_headers(imagewalker20 PRIVATE StdAfx.h)

# /GL + /LTCG on the optimised configs. The views and tools here call libiw's
# surface locks and line converters, and libiw is compiled with /GL too, so
# whole-program optimisation is what lets those calls inline across the
# application and library boundary. Debug is left alone: /GL makes the link slow
# and the stepping worse for no benefit at /Od.
set_target_properties(imagewalker20 PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)

target_link_libraries(imagewalker20 PRIVATE
    iw::flags
    iw::libiw
    iw::jpeg
    iw::png
    iw::tiff
    iw::gif
    iw::expat
    iw::zlib
    iw::exif
    iw::hunspell
)

# The app embeds its own manifest as resource 1.
target_link_options(imagewalker20 PRIVATE /MANIFEST:NO)
