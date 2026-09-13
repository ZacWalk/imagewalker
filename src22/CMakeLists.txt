# ImageWalker 2.2 -> imagewalker22.exe
#
# Standalone x64 WTL port. Removed historical subsystems:
# the web gallery and its template parser, the iw:// pluggable protocol and
# embedded browser, screen capture, MAPI email, TWAIN/STI scanning, DirectShow
# video thumbnails and the COM automation server. Also gone from this tree:
# the thumbnail cache, zip, toolbar and accelerator customisation, wallpaper
# and file associations.

set(IW22_SOURCES
    App.cpp
    AppCommands.cpp
    AppMainFrame.cpp
    FileBmp.cpp
    FileCompoundDoc.cpp
    FileFormat.cpp
    FileFormatAny.cpp
    FileGif.cpp
    FileJpeg.cpp
    FileJpegTran.cpp
    FileMetaFile.cpp
    FilePcx.cpp
    FilePng.cpp
    FilePsd.cpp
    FileTiff.cpp
    FileWpg.cpp
    Imaging.cpp
    ImagingConvert.cpp
    ImagingDataObject.cpp
    ImagingEdits.cpp
    ImagingFilter.cpp
    ImagingLock.cpp
    ImagingQuantize.cpp
    ImagingRotate.cpp
    ImagingScale.cpp
    ImagingSimd.cpp
    main.cpp
    test.cpp
    Metadata.cpp
    MetadataExif.cpp
    MetadataIPTC.cpp
    MetadataXMP.cpp
    Settings.cpp
    StdAfx.cpp
    ToolContactSheet.cpp
    ToolConvert.cpp
    ToolJpeg.cpp
    ToolResize.cpp
    UtilCore.cpp
    UtilMd5.cpp
    UtilStrings.cpp
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
# cross-module inlining too, so the line converters would stay out of line even
# with /GL on. The preset builds RelWithDebInfo, so this is the release config.
# Replaced rather than appended: appending /Ob2 leaves both on the command line
# and every compile then reports D9025. Directory scope, so 2.2 only.
string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

add_executable(imagewalker22 WIN32 ${IW22_SOURCES} ImageWalker.rc)
target_include_directories(imagewalker22 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(imagewalker22 PRIVATE UNICODE _UNICODE)
target_precompile_headers(imagewalker22 PRIVATE StdAfx.h)

# /GL + /LTCG on the optimised configs. The imaging pipeline is split across
# translation units by design -- the surface locks call ImagingConvert's line
# converters, and every caller of those lives somewhere else again -- so without
# whole-program optimisation none of it can inline across a .cpp boundary no
# matter how small the function is. Debug is left alone: /GL makes the link slow
# and the stepping worse for no benefit at /Od.
set_target_properties(imagewalker22 PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)

target_link_libraries(imagewalker22 PRIVATE
    iw::flags
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
target_link_options(imagewalker22 PRIVATE /MANIFEST:NO)
